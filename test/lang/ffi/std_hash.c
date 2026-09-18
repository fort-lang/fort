// Provides an independent 64-bit FNV-1a result for the fort hash test.
// The extern signature uses only a pointer and a length.
#include <stdint.h>

static const uint64_t FNV_OFFSET_BASIS = 0xCBF29CE484222325ULL;
static const uint64_t FNV_PRIME = 0x100000001B3ULL;

uint64_t std_fnv1a(const unsigned char* bytes, uint64_t len) {
    uint64_t h = FNV_OFFSET_BASIS;
    for (uint64_t i = 0; i < len; i++) {
        h ^= (uint64_t)bytes[i];
        h *= FNV_PRIME;
    }
    return h;
}
