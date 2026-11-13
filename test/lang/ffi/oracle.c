// Gives its argument back. A test declares it with the result type that it needs, so the result
// can be an owner whose address starts no allocation.
void* oracle_pass(void* p) {
    return p;
}
