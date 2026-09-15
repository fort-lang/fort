// test/mac/net_probe.c measures Darwin IPv4 constants and address layout.
// The native Mac network gate compares these results with std/mac/net.ft.
// The gate also reads a fort address through Darwin C fields.
// D3.8, D13.2
#include <errno.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>

#include <netinet/in.h>
#include <sys/socket.h>

#ifdef FORT_NET_LAYOUT_HELPER
int mac_net_address_matches(const void* raw, unsigned int port, unsigned int addr) {
    const struct sockaddr_in* socket_addr = raw;
    if (socket_addr->sin_len != 16 || socket_addr->sin_family != AF_INET ||
        socket_addr->sin_port != port || socket_addr->sin_addr.s_addr != addr) {
        return 0;
    }
    for (size_t i = 0; i < sizeof socket_addr->sin_zero; i++) {
        if (socket_addr->sin_zero[i] != 0) {
            return 0;
        }
    }
    return 1;
}
#else
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
#endif
