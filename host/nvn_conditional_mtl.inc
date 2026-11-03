static p_render_enable z_render = {.enabled = 1};
static uint64_t z_predicate_hits, z_predicate_waits, z_predicate_prefetched;
#if DM_NVN_M3
static int zeldaM3PredicateAsyncEnabled(void) {
    static int asyncEnabled = -1;
    if (asyncEnabled < 0) {
        const char *val = getenv("ZELDA_M3_PREDICATE_ASYNC");
        asyncEnabled = val ? atoi(val) != 0 : 1;
    }
    return asyncEnabled;
}
static int zelda_m3_predicate_reuse_enabled(void) {
    static int reuse = -1;
    if (reuse < 0) { const char *value = getenv("ZELDA_M3_PREDICATE_REUSE"); reuse = !value || atoi(value); }
    return reuse;
}
static int zelda_predicate_find(uint64_t address) {
    zelda_buffer_cache_sync();
    uint64_t canonical = nvn_canonical_address(address, 32);
    for (unsigned i = 0; canonical && i < z_predicate_count; i++)
        if (z_predicate_cache[i].source == canonical && z_predicate_cache[i].valid == 3) return (int)i;
    return -1;
}
static void zelda_predicate_store(uint64_t address, const uint64_t words[4]) {
    if (!zelda_m3_predicate_reuse_enabled()) return;
    zelda_buffer_cache_sync();
    uint64_t canonical = nvn_canonical_address(address, 32);
    if (!canonical) return;
    unsigned i;
    for (i = 0; i < z_predicate_count; i++) if (z_predicate_cache[i].source == canonical) break;
    if (i == z_predicate_count) { if (z_predicate_count == ZELDA_PREDICATE_CACHE_CAPACITY) return; z_predicate_count++; }
    z_predicate_cache[i].source = canonical;
    memcpy(z_predicate_cache[i].words, words, 32);
    z_predicate_cache[i].valid = 3;
}
#endif
static int zelda_render_enabled(void) {
    if (!z_render.address) return z_render.enabled;
#if DM_NVN_M3
    int found = zelda_m3_predicate_reuse_enabled() ? zelda_predicate_find(z_render.address) : -1;
    if (found >= 0) { z_predicate_hits++; return nvn_render_test(&z_render, z_predicate_cache[found].words); }
    if (zeldaM3PredicateAsyncEnabled()) {
        void *bufferPtr = NULL;
        uint64_t bufferOffset = 0;
        if (mtl_addr(z_render.address, 32, &bufferPtr, &bufferOffset)) {
            const void *contents = [(__bridge id<MTLBuffer>)bufferPtr contents];
            if (contents) {
                uint64_t counters[4];
                memcpy(counters, (const uint8_t *)contents + bufferOffset, sizeof counters);
                int enabled = nvn_render_test(&z_render, counters);
                zelda_predicate_store(z_render.address, counters);
                return enabled;
            }
        }
        return 1;
    }
#endif
    // Unsupported draws and clear formats retain the CPU fallback.
    uint64_t fence = mtl_sub_commit();
    if (!mtl_wait(fence, 5000000000ull)) MDIE("Conditional render GPU wait timed out");
    void *buffer; uint64_t offset;
    if (!mtl_addr(z_render.address, 32, &buffer, &offset)) MDIE("Conditional render address is not in a memory pool");
    const void *contents = [(__bridge id<MTLBuffer>)buffer contents];
    if (!contents) MDIE("Conditional render buffer is not CPU visible");
    uint64_t counters[4]; memcpy(counters, (const uint8_t *)contents + offset, sizeof counters);
    int enabled = nvn_render_test(&z_render, counters);
    z_predicate_waits++;
    PD("W test addr=%#llx mode=%d values=%llu,%llu render=%d", (unsigned long long)z_render.address, z_render.mode,
       (unsigned long long)counters[0], (unsigned long long)counters[2], enabled);
    mtl_sub_begin(); args_dirty_all(); g_pso_dirty = g_dss_dirty = 1;
#if DM_NVN_M3
    // Begin may expire a previous submission's cache. Register only after that expiry, and only
    // words proven complete by the wait above. The shared write tracker handles every later writer.
    zelda_predicate_store(z_render.address, counters);
#endif
    return enabled;
}
