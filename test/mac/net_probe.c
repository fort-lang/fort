// test/mac/net_probe.c measures Darwin IPv4 constants and address layout.
// The native Mac network gate compares these results with std/mac/net.ft.
// D3.8, D13.2
#include <errno.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>

#include <netinet/in.h>
#include <sys/socket.h>

int main(void) {
    printf("AF_INET %d\n", AF_INET);
    printf("SOCK_STREAM %d\n", SOCK_STREAM);
    printf("SOL_SOCKET %d\n", SOL_SOCKET);
    printf("SO_REUSEADDR %d\n", SO_REUSEADDR);
    printf("INADDR_ANY %u\n", (unsigned)INADDR_ANY);
    printf("sizeof_sockaddr_in %zu\n", sizeof(struct sockaddr_in));
    printf("sizeof_socklen_t %zu\n", sizeof(socklen_t));
    printf("offset_len %zu\n", offsetof(struct sockaddr_in, sin_len));
    printf("offset_family %zu\n", offsetof(struct sockaddr_in, sin_family));
    printf("offset_port %zu\n", offsetof(struct sockaddr_in, sin_port));
    printf("offset_addr %zu\n", offsetof(struct sockaddr_in, sin_addr));
    printf("EINVAL %d\n", EINVAL);
    printf("ECONNREFUSED %d\n", ECONNREFUSED);
    printf("ENOTSOCK %d\n", ENOTSOCK);
}
