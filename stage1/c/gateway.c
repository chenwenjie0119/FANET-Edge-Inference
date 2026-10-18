/*
 * gateway.c — 二合一 MAVLink 网关（C，零第三方依赖）。
 *
 * 一个二进制替代两个程序：
 *   - udp 模式    = 原 udp_gateway.c（Stage 0：SITL UDP 14540 -> 14550 纯转发）
 *   - serial 模式 = 原 mavlink_serial_gateway.c（Stage 1：串口 <-> UDP 双向桥）
 *
 * 用法:
 *   ./gateway udp                       # Stage 0 仿真转发
 *   ./gateway serial [/dev/ttyAMA0]     # Stage 1 真机串口桥（默认 /dev/ttyAMA0 @57600）
 *
 * 编译: gcc -O2 -o gateway gateway.c
 */
#include <errno.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <poll.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <termios.h>
#include <time.h>
#include <unistd.h>

#define SERIAL_DEV      "/dev/ttyAMA0"
#define SERIAL_BAUD     B57600

#define TELEMETRY_PORT  14550   /* 遥测出口：worker 监听 */
#define CMD_PORT        14540   /* 指令入口：地面指令 */

#define BUF_SIZE        4096
#define RECONNECT_MIN   1       /* 重连最小间隔（秒） */
#define RECONNECT_MAX   30      /* 重连最大间隔（秒） */
#define WRITE_MAX_TRIES 1000    /* 串口写重试上限（每次 1ms，约 1s） */

/* ---------- 工具 ---------- */

/* 单调时钟毫秒：用于非阻塞重连调度 */
static long long now_ms(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (long long)ts.tv_sec * 1000 + ts.tv_nsec / 1000000;
}

/* 指数退避：翻倍并封顶。 */
static unsigned int next_backoff(unsigned int cur)
{
    if (cur * 2 > RECONNECT_MAX)
        return RECONNECT_MAX;
    return cur * 2;
}

/* ---------- 串口（serial 模式） ---------- */

/* 打开串口并配置为 8N1 / 指定波特率 / 非阻塞读。成功返回 fd，失败返回 -1。 */
static int serial_open(const char *path)
{
    int fd = open(path, O_RDWR | O_NOCTTY | O_NONBLOCK);
    if (fd < 0) {
        perror("serial open");
        return -1;
    }

    struct termios tty;
    if (tcgetattr(fd, &tty) != 0) {
        perror("tcgetattr");
        close(fd);
        return -1;
    }

    cfsetispeed(&tty, SERIAL_BAUD);
    cfsetospeed(&tty, SERIAL_BAUD);
    cfmakeraw(&tty);

    tty.c_cflag |= (CLOCAL | CREAD);
    tty.c_cflag &= ~CSIZE;
    tty.c_cflag |= CS8;
    tty.c_cflag &= ~CSTOPB;
    tty.c_cflag &= ~PARENB;

    tty.c_cc[VMIN] = 0;
    tty.c_cc[VTIME] = 1;

    if (tcsetattr(fd, TCSANOW, &tty) != 0) {
        perror("tcsetattr");
        close(fd);
        return -1;
    }
    return fd;
}

/* 写满 len 字节；串口缓冲满时短暂重试（EAGAIN），超时/失败返回 -1。 */
static int serial_write_all(int fd, const char *buf, size_t len)
{
    size_t off = 0;
    int tries = 0;
    while (off < len) {
        ssize_t w = write(fd, buf + off, len - off);
        if (w > 0) {
            off += (size_t)w;
            continue;
        }
        if (errno == EAGAIN || errno == EINTR) {
            if (++tries >= WRITE_MAX_TRIES) {
                fprintf(stderr, "[gw] serial write timeout, drop %zu bytes\n", len - off);
                return -1;
            }
            usleep(1000);
            continue;
        }
        perror("serial write");
        return -1;
    }
    return 0;
}

/* ---------- UDP ---------- */

/* 创建 UDP server 并 bind 到 0.0.0.0:port（指令入口）。 */
static int udp_server_init(int port)
{
    int fd = socket(AF_INET, SOCK_DGRAM, 0);
    if (fd < 0) {
        perror("socket");
        return -1;
    }
    int opt = 1;
    setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_ANY);
    addr.sin_port = htons((uint16_t)port);

    if (bind(fd, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
        perror("bind");
        close(fd);
        return -1;
    }
    return fd;
}

/* ---------- 模式 1：纯 UDP 转发（Stage 0 SITL） ---------- */

static int run_udp_mode(void)
{
    int fd = udp_server_init(CMD_PORT);
    if (fd < 0)
        return 1;

    struct sockaddr_in dst;
    memset(&dst, 0, sizeof(dst));
    dst.sin_family = AF_INET;
    dst.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    dst.sin_port = htons(TELEMETRY_PORT);

    printf("gateway udp: %d -> 127.0.0.1:%d\n", CMD_PORT, TELEMETRY_PORT);

    char buf[BUF_SIZE];
    for (;;) {
        /* 纯转发不需要源地址，传 NULL,NULL（也避免 src_len 未重置导致 EINVAL） */
        ssize_t n = recvfrom(fd, buf, sizeof(buf), 0, NULL, NULL);
        if (n < 0) {
            perror("recvfrom");
            continue;
        }
        if (sendto(fd, buf, (size_t)n, 0,
                   (struct sockaddr *)&dst, sizeof(dst)) < 0) {
            perror("sendto");
        }
    }
    close(fd);
    return 0;
}

/* ---------- 模式 2：串口 + UDP 双向桥（Stage 1 真机） ---------- */

static int run_serial_mode(const char *dev)
{
    int udp_cmd = udp_server_init(CMD_PORT);
    if (udp_cmd < 0)
        return 1;

    int udp_tele = socket(AF_INET, SOCK_DGRAM, 0);
    if (udp_tele < 0) {
        perror("socket telemetry");
        return 1;
    }

    /* 遥测目标：本机 worker（127.0.0.1:14550）。改这里可转发到地面站 IP。 */
    struct sockaddr_in tele_dst;
    memset(&tele_dst, 0, sizeof(tele_dst));
    tele_dst.sin_family = AF_INET;
    tele_dst.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    tele_dst.sin_port = htons(TELEMETRY_PORT);

    unsigned int backoff = RECONNECT_MIN;
    int serial = -1;
    long long next_reconnect = 0;   /* 串口断开后下次重连时间戳(ms)，期间不阻塞 */

    for (;;) {
        struct pollfd fds[2];
        fds[0].fd = serial;
        fds[0].events = (serial >= 0) ? POLLIN : 0;
        fds[0].revents = 0;
        fds[1].fd = udp_cmd;
        fds[1].events = POLLIN;
        fds[1].revents = 0;

        /* 动态 poll 超时：默认 100ms 轮询；串口断开且未到重连时刻时，
         * 精确等到重连点，避免空转 */
        int timeout = 100;
        if (serial < 0) {
            long long diff = next_reconnect - now_ms();
            if (diff > 0 && diff < timeout)
                timeout = (int)diff;
        }
        int pr = poll(fds, 2, timeout);
        if (pr < 0) {
            if (errno == EINTR)
                continue;
            perror("poll");
            break;
        }

        if (serial >= 0) {
            /* 飞控 -> 地面：先读 POLLIN，避免 POLLIN|POLLHUP 同时置位时丢最后一帧 */
            if (fds[0].revents & POLLIN) {
                char buf[BUF_SIZE];
                ssize_t n = read(serial, buf, sizeof(buf));
                if (n > 0) {
                    if (sendto(udp_tele, buf, (size_t)n, 0,
                               (struct sockaddr *)&tele_dst, sizeof(tele_dst)) < 0) {
                        perror("sendto telemetry");
                    }
                } else if (n < 0 && errno != EAGAIN && errno != EINTR) {
                    /* 读错误（如 EIO/ENODEV）：飞控断电或驱动异常，进入重连 */
                    perror("serial read");
                    close(serial);
                    serial = -1;
                    backoff = next_backoff(backoff);
                    next_reconnect = now_ms() + (long long)backoff * 1000;
                    printf("[gw] serial lost, retry in %us (UDP still alive)\n", backoff);
                }
            }

            /* 串口异常（设备失效）：读完最后数据后再关闭重连 */
            if (fds[0].revents & (POLLERR | POLLHUP | POLLNVAL)) {
                printf("[gw] serial error (revents=0x%x), retry in %us (UDP still alive)\n",
                       fds[0].revents, backoff);
                close(serial);
                serial = -1;
                backoff = next_backoff(backoff);
                next_reconnect = now_ms() + (long long)backoff * 1000;
            }
        }

        /* 地面 -> 飞控：UDP 14540 收到的指令写回串口（断开时丢弃并提示） */
        if (fds[1].revents & POLLIN) {
            char buf[BUF_SIZE];
            ssize_t n = recvfrom(udp_cmd, buf, sizeof(buf), 0, NULL, NULL);
            if (n > 0) {
                if (serial >= 0) {
                    serial_write_all(serial, buf, (size_t)n);
                } else {
                    printf("[gw] cmd dropped (%zd bytes), serial down\n", n);
                }
            }
        }

        /* 非阻塞重连：串口断开期间每 100ms 轮询一次，到点尝试重开 */
        if (serial < 0 && now_ms() >= next_reconnect) {
            serial = serial_open(dev);
            if (serial >= 0) {
                backoff = RECONNECT_MIN;
                printf("[gw] serial %s open @57600, streaming -> udp %d\n",
                       dev, TELEMETRY_PORT);
            } else {
                backoff = next_backoff(backoff);
                next_reconnect = now_ms() + (long long)backoff * 1000;
                printf("[gw] open failed, retry in %us (UDP still alive)\n", backoff);
            }
        }
    }

    close(udp_tele);
    close(udp_cmd);
    return 0;
}

/* ---------- 入口 ---------- */

int main(int argc, char **argv)
{
    if (argc < 2) {
        fprintf(stderr, "用法: %s {udp|serial} [serial-device]\n", argv[0]);
        return 1;
    }
    if (strcmp(argv[1], "udp") == 0)
        return run_udp_mode();
    if (strcmp(argv[1], "serial") == 0)
        return run_serial_mode((argc > 2) ? argv[2] : SERIAL_DEV);
    fprintf(stderr, "未知模式: %s (可用 udp | serial)\n", argv[1]);
    return 1;
}
