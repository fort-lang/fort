// C11 helper linked into test/lang/run/stdlib/008_str_hash.ft: a second,
// independent witness to the 64-bit FNV-1a that stdlib.md 2.5 fixes for
// str.hash, so that the fort side is held against something other than
// itself. The signature takes a pointer and a length only, which is what an
// extern signature may use.
// D9.8
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
