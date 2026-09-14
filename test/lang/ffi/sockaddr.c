// C11 helpers linked into test/lang/run/ffi/013_sockaddr_layout.ft, which holds
// std.net's sockaddr_in against the one in <netinet/in.h> on the target itself.
// D3.8, D9.9
//
// The reason this file exists is that nothing inside fort can see a wrong field
// offset. Under opaque pointers a struct laid out wrongly but used consistently
// agrees with itself, so every fort-only test of std.net passes over a family
// field that overlaps the port. C is the second opinion: these helpers read and
// write the platform's own `struct sockaddr_in` through offsetof, and the fort
// side reads and writes its own declaration of the same memory.
//
// The helpers also report the five socket constants and the two byte-order
// macros from the system headers. htons and htonl are macros as well as
// functions in glibc, and a macro has no symbol an `extern fn` could declare, so
// std.net writes the swap in fort and this file wraps the macros in real
// functions that fort can call.
// D9.8
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include <netinet/in.h>
#include <sys/socket.h>

// The values net_fill_sockaddr_in writes, one distinct value per field, so that
// a field read at the wrong offset cannot come back right by accident. The port
// is above 32767, where a signed 16-bit swap differs from an unsigned one, and
// the address is 127.0.0.1. The eight bytes C names sin_zero carry a pattern
// here rather than the zeros a real call needs, because a length that stops
// short of them is the mistake this checks for.
enum {
    FILL_PORT = 40000,
    FILL_ADDR = 0x7F000001,
    FILL_ZERO_BASE = 200,
};

uint64_t net_sizeof_sockaddr_in(void) {
    return sizeof(struct sockaddr_in);
}

uint64_t net_alignof_sockaddr_in(void) {
    return _Alignof(struct sockaddr_in);
}

// 0 is sin_family, 1 sin_port, 2 sin_addr and 3 sin_zero; any other index is
// SIZE_MAX, which no offset equals, so a miscounted index fails the test.
uint64_t net_offsetof_sockaddr_in(int32_t field) {
    switch (field) {
    case 0:
        return offsetof(struct sockaddr_in, sin_family);
    case 1:
        return offsetof(struct sockaddr_in, sin_port);
    case 2:
        return offsetof(struct sockaddr_in, sin_addr);
    case 3:
        return offsetof(struct sockaddr_in, sin_zero);
    default:
        return UINT64_MAX;
    }
}

// The width of the length argument bind(2), connect(2), accept(2) and
// getsockname(2) take, which std.libc declares as u32.
uint64_t net_sizeof_socklen(void) {
    return sizeof(socklen_t);
}

// C writes each field through its own member name and fort reads them back.
void net_fill_sockaddr_in(struct sockaddr_in* a) {
    unsigned char* zero = (unsigned char*)a->sin_zero;
    a->sin_family = AF_INET;
    a->sin_port = htons(FILL_PORT);
    a->sin_addr.s_addr = htonl((uint32_t)FILL_ADDR);
    for (int i = 0; i < 8; i++) {
        zero[i] = (unsigned char)(FILL_ZERO_BASE + i);
    }
}

// And the other way round: fort writes the same values through std.net's
// declaration and C reads them through the platform's.
bool net_check_sockaddr_in(const struct sockaddr_in* a) {
    const unsigned char* zero = (const unsigned char*)a->sin_zero;
    if (a->sin_family != AF_INET || a->sin_port != htons(FILL_PORT)) {
        return false;
    }
    if (a->sin_addr.s_addr != htonl((uint32_t)FILL_ADDR)) {
        return false;
    }
    for (int i = 0; i < 8; i++) {
        if (zero[i] != (unsigned char)(FILL_ZERO_BASE + i)) {
            return false;
        }
    }
    return true;
}

int32_t net_af_inet(void) {
    return AF_INET;
}

int32_t net_sock_stream(void) {
    return SOCK_STREAM;
}

int32_t net_sol_socket(void) {
    return SOL_SOCKET;
}

int32_t net_so_reuseaddr(void) {
    return SO_REUSEADDR;
}

uint32_t net_inaddr_any(void) {
    return INADDR_ANY;
}

uint16_t net_htons(uint16_t v) {
    return htons(v);
}

uint32_t net_htonl(uint32_t v) {
    return htonl(v);
}
