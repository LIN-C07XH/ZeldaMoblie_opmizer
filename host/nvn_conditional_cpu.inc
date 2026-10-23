// Snapshot a proven readonly segment after one CPU-fallback fence. The planner stops before any
// write, target change, CALL/JUMP or unknown command. Normal invalidation still guards every reader.
static int zelda_conditional_cpu_draw(const p_draw *draw, const p_draw_idx *indexed, const zelda_conditional_state *state) {
    if (!zelda_m3_predicate_reuse_enabled() || zelda_predicate_find(z_render.address) >= 0)
        return zelda_render_enabled();
    zelda_conditional_input input;
    zelda_conditional_input_make(draw, indexed, state, &input);
    unsigned snapshots = zelda_conditional_collect(&input, *state);
    int enabled = zelda_render_enabled();
    for (unsigned i = 1; i < snapshots; i++) {
        void *buffer; uint64_t offset;
        uint64_t address = z_conditional_inputs[i].predicate;
        if (!mtl_addr(address, 32, &buffer, &offset)) continue;
        const void *bytes = [(__bridge id<MTLBuffer>)buffer contents];
        if (bytes) {
            uint64_t words[4]; memcpy(words, (const uint8_t *)bytes + offset, sizeof words);
            zelda_predicate_store(address, words); z_predicate_prefetched++;
        }
    }
    return enabled;
}
