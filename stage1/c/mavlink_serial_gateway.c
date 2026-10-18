/*
 * mavlink_serial_gateway.c
 * 串口 <-> UDP 的 MAVLink 双向网关（C，零第三方依赖）。
 *
 * 拓扑：
 *   [Pixhawk] --UART 57600--> /dev/ttyAMA0
 *        --(本程序)--> sendto 127.0.0.1:14550   (遥测出口, worker 监听)
 *        --(本程序)--> bind  127.0.0.1:14540   (指令入口, 地面指令 -> 串口 -> 飞控)
 *
 * 特性：
 *   1. termios 8N1 / 57600 / 非阻塞读取（VMIN=0, VTIME=1）
 *   2. poll 同时监听串口与 UDP 指令口，双向转发不阻塞
 *   3. 非阻塞断线重连：串口异常（飞控断电/驱动错误）不崩溃、不 sleep，
 *      记录下次重连时间戳，期间继续轮询 UDP 指令；
 *      指数退避 1s -> 2s -> 4s -> ... -> 30s
 *
 * 编译: gcc -O2 -o mavlink_serial_gateway mavlink_serial_gateway.c
 * 运行: sudo ./mavlink_serial_gateway [/dev/ttyAMA0]
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

#define TELEMETRY_PORT  14550   /* 遥测出口：sendto 到这里，worker 监听此端口收包 */
#define CMD_PORT        14540   /* 指令入口：本程序 bind，地面指令经此写回串口 */

#define BUF_SIZE        4096
#define RECONNECT_MIN   1       /* 重连最小间隔（秒） */
#define RECONNECT_MAX   30      /* 重连最大间隔（秒） */

/* ---------- 串口 ---------- */

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

    /* 波特率 57600 */
    cfsetispeed(&tty, SERIAL_BAUD);
    cfsetospeed(&tty, SERIAL_BAUD);

    /* 原始模式：关闭 ICANON/ECHO/ISIG 等行处理与信号 */
    cfmakeraw(&tty);

    /* 8N1：8 数据位，1 停止位，无奇偶校验 */
    tty.c_cflag |= (CLOCAL | CREAD);   /* 忽略 modem 控制线；允许读取 */
    tty.c_cflag &= ~CSIZE;
    tty.c_cflag |= CS8;                /* 8 数据位 */
    tty.c_cflag &= ~CSTOPB;            /* 1 停止位 */
    tty.c_cflag &= ~PARENB;            /* 无奇偶 */

    /*
     * 非阻塞语义：
     *  VMIN=0, VTIME=1 -> read 最多阻塞 100ms 后返回（即使无数据返回 0）
     *  配合 O_NONBLOCK：read 立即返回；无数据时返回 -1/EAGAIN
     */
    tty.c_cc[VMIN] = 0;
    tty.c_cc[VTIME] = 1;

    if (tcsetattr(fd, TCSANOW, &tty) != 0) {
        perror("tcsetattr");
        close(fd);
        return -1;
    }
    return fd;
}

/* 串口写重试上限：每次 EAGAIN 等待 1ms，最多约 1s，防止飞控不读串口时无限转圈 */
#define WRITE_MAX_TRIES 1000

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

/* 指数退避：翻倍并封顶。 */
static unsigned int next_backoff(unsigned int cur)
{
    if (cur * 2 > RECONNECT_MAX)
        return RECONNECT_MAX;
    return cur * 2;
}

/* ---------- 主循环 ---------- */

/* 单调时钟毫秒：用于非阻塞重连调度 */
static long long now_ms(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (long long)ts.tv_sec * 1000 + ts.tv_nsec / 1000000;
}

int main(int argc, char **argv)
{
    const char *dev = (argc > 1) ? argv[1] : SERIAL_DEV;

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

        /* ---- 飞控 -> 地面：先读 POLLIN，避免 POLLIN|POLLHUP 同时置位时丢最后一帧 ---- */
        if (serial >= 0 && (fds[0].revents & POLLIN)) {
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

        /* ---- 串口异常（设备失效）：读完最后数据后再关闭重连 ---- */
        if (serial >= 0 && (fds[0].revents & (POLLERR | POLLHUP | POLLNVAL))) {
            printf("[gw] serial error (revents=0x%x), retry in %us (UDP still alive)\n",
                   fds[0].revents, backoff);
            close(serial);
            serial = -1;
            backoff = next_backoff(backoff);
            next_reconnect = now_ms() + (long long)backoff * 1000;
        }

        /* ---- 地面 -> 飞控：UDP 14540 收到的指令写回串口（断开时丢弃并提示） ---- */
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

        /* ---- 非阻塞重连：串口断开期间每 100ms 轮询一次，到点尝试重开 ---- */
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
