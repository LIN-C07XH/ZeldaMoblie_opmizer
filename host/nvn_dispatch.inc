// Compute dispatches share the open copy encoder instead of each opening its own: every encoder is a separate GPU
// kick with fixed start-up cost (v1.4.3 post-cutscene cave: 61 dispatch encoders per frame, 49 of them back to back,
// each a few microseconds of work behind 10-30 us of idle GPU). The encoder barriers on both sides keep the order the
// one-encoder-per-dispatch path had: earlier copies and dispatches finish before it, later ones wait for it.
#if !DM_NVN_M3
static id<MTL4ComputeCommandEncoder> zelda_dispatch_encoder(void) {
    id<MTL4ComputeCommandEncoder> encoder = blit();
    zelda_blit_barrier();
    [encoder setArgumentTable:g_ct];
    args_dirty_all();
    return encoder;
}
static void zelda_dispatch_end(void) {
    zelda_blit_barrier();
    args_dirty_all();
}
#else
static uint64_t z_m3_dispatches, z_m3_dispatch_encoders;
static int z_m3_dispatch_batch = -1;
static id<MTLComputeCommandEncoder> zelda_dispatch_encoder(void) {
    if (z_m3_dispatch_batch < 0) {
        const char *value = getenv("ZELDA_M3_DISPATCH_BATCH");
        z_m3_dispatch_batch = !value || atoi(value);
    }
    z_m3_dispatches++;
    if (g_m3_dispatch_ce) {
        args_dirty_all();
        return g_m3_dispatch_ce;
    }
    rp_end();
    if (g_blit) { enc_b_end(g_blit); g_blit = nil; }
    args_dirty_all();
    g_m3_dispatch_ce = enc_c_begin(NULL);
    z_m3_dispatch_encoders++;
    return g_m3_dispatch_ce;
}
static void zelda_dispatch_end(void) {
    // Keep serial dispatches together. Render/blit/standalone-compute entry and submit close this
    // encoder; the barrier also preserves writes through aliased resources between dispatches.
    if (z_m3_dispatch_batch) [g_m3_dispatch_ce memoryBarrierWithScope:(MTLBarrierScopeBuffers | MTLBarrierScopeTextures)];
    else zelda_dispatch_ensure_ended();
    args_dirty_all();
}
#endif
