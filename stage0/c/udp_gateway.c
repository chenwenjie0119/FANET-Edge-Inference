/*
 * udp_gateway: 极简 UDP 转发网关（C，零第三方库）。
 *
 * bind 14540 接收 PX4 SITL 发来的 MAVLink 原始字节流，
 * 原封不动转发到 127.0.0.1:14550（Python worker 监听端口）。
 *
 * 编译运行:
 *   gcc -O2 -o udp_gateway udp_gateway.c
 *   ./udp_gateway
 *
 * 可替代 mavlink-router 完成 Stage 0 的转发角色。
 * 协议无状态：不解析 MAVLink 内容，纯字节搬运。
 */
#include <arpa/inet.h>
#include <netinet/in.h>
#include <stdio.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>

#define BUF_SIZE 4096   /* MAVLink 单包远小于此，缓冲区按 MTU 给足 */
#define SRC_PORT 14540  /* 监听 PX4 SITL 的输出端口 */
#define DST_IP "127.0.0.1"
#define DST_PORT 14550  /* 转发目标（Python worker 监听端口） */

int main(void) {
    int fd;
    struct sockaddr_in src_addr, dst_addr;
    char buf[BUF_SIZE];

    /* 1. socket 初始化：AF_INET + SOCK_DGRAM = UDP */
    fd = socket(AF_INET, SOCK_DGRAM, 0);
    if (fd < 0) {
        perror("socket");
        return 1;
    }

    /* 允许端口快速复用，避免重启时 TIME_WAIT 阻塞 bind */
    int opt = 1;
    setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

    /* 2. bind：绑定任意本地地址 + SRC_PORT */
    memset(&src_addr, 0, sizeof(src_addr));
    src_addr.sin_family = AF_INET;
    src_addr.sin_addr.s_addr = htonl(INADDR_ANY);
    src_addr.sin_port = htons(SRC_PORT);

    if (bind(fd, (struct sockaddr *)&src_addr, sizeof(src_addr)) < 0) {
        perror("bind");
        close(fd);
        return 1;
    }

    /* 转发目标地址 */
    memset(&dst_addr, 0, sizeof(dst_addr));
    dst_addr.sin_family = AF_INET;
    dst_addr.sin_addr.s_addr = inet_addr(DST_IP);
    dst_addr.sin_port = htons(DST_PORT);

    printf("udp gateway: %d -> %s:%d\n", SRC_PORT, DST_IP, DST_PORT);

    /* 3. recvfrom 循环：收到多少转发多少 */
    for (;;) {
        /* 纯转发不需要源地址，传 NULL,NULL（也避免 src_len 未重置导致 EINVAL） */
        ssize_t n = recvfrom(fd, buf, sizeof(buf), 0, NULL, NULL);
        if (n < 0) {
            perror("recvfrom");
            continue;
        }
        if (sendto(fd, buf, (size_t)n, 0,
                   (struct sockaddr *)&dst_addr, sizeof(dst_addr)) < 0) {
            perror("sendto");
        }
    }

    close(fd);
    return 0;
}
