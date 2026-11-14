// Gives its argument back. A test declares it with the result type that it needs, so the result
// can be an owner whose address starts no allocation.
void* oracle_pass(void* p) {
    return p;
}

// Reads the byte at `p`. The access is an access of C code with no system call.
unsigned char oracle_read(const unsigned char* p) {
    return *p;
}

// Stores `value` into the byte at `p`. The access is an access of C code with no system call.
void oracle_write(unsigned char* p, unsigned char value) {
    *p = value;
}
