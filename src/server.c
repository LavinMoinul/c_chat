#include <arpa/inet.h>
#include <errno.h>
#include <netinet/in.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>

static int parse_port(const char *s) {
    char *end = NULL;
    long val = strtol(s, &end, 10);
    if (end == s || *end != '\0') return -1;
    if (val < 1 || val > 65535) return -1;
    return (int)val;
}

static int send_all(int fd, const char *buf, size_t len) {
    size_t sent = 0;
    while (sent < len) {
        ssize_t n = send(fd, buf + sent, len - sent, 0);
        if (n < 0) {
            if (errno == EINTR) continue;
            return -1;
        }
        sent += (size_t)n;
    }
    return 0;
}

int main(int argc, char **argv) {
    signal(SIGPIPE, SIG_IGN);

    if (argc != 2) {
        fprintf(stderr, "usage: %s <port>\n", argv[0]);
        return 1;
    }

    int port = parse_port(argv[1]);
    if (port < 0) {
        fprintf(stderr, "bad port: %s\n", argv[1]);
        return 1;
    }

    int listen_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (listen_fd < 0) {
        perror("socket");
        return 1;
    }

    int yes = 1;
    if (setsockopt(listen_fd, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof(yes)) < 0) {
        perror("setsockopt");
        close(listen_fd);
        return 1;
    }

    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_ANY);
    addr.sin_port = htons((uint16_t)port);

    if (bind(listen_fd, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
        perror("bind");
        close(listen_fd);
        return 1;
    }

    if (listen(listen_fd, 16) < 0) {
        perror("listen");
        close(listen_fd);
        return 1;
    }

    printf("server listening on port %d\n", port);
    printf("test client: nc 127.0.0.1 %d\n", port);

    while (1) {
        struct sockaddr_in client_addr;
        socklen_t client_len = sizeof(client_addr);

        int client_fd = accept(listen_fd, (struct sockaddr *)&client_addr, &client_len);
        if (client_fd < 0) {
            if (errno == EINTR) continue;
            perror("accept");
            break;
        }

        char ip[INET_ADDRSTRLEN];
        const char *ip_str = inet_ntop(AF_INET, &client_addr.sin_addr, ip, sizeof(ip));
        if (!ip_str) ip_str = "unknown";

        int client_port = ntohs(client_addr.sin_port);
        printf("client connected from %s:%d\n", ip_str, client_port);
        printf("waiting for messages\n");

        char buf[1024];

        while (1) {
            ssize_t n = recv(client_fd, buf, sizeof(buf) - 1, 0);
            if (n == 0) {
                printf("client disconnected %s:%d\n", ip_str, client_port);
                break;
            }
            if (n < 0) {
                if (errno == EINTR) continue;
                perror("recv");
                break;
            }

            buf[n] = '\0';
            printf("recv %zd bytes: %s", n, buf);

            if (send_all(client_fd, buf, (size_t)n) < 0) {
                perror("send");
                break;
            }
        }

        close(client_fd);
        printf("ready for next client\n");
    }

    close(listen_fd);
    return 0;
}
