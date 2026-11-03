// Uniform and predicate bytes are submission-local; owned index results can survive slices.
static int zelda_index_reuse_enabled(void) {
    static int enabled = -1;
    if (enabled < 0) { const char *e = getenv("ZELDA_INDEX_REUSE"); enabled = !e || atoi(e) != 0; }
    return enabled;
}
static struct { uint64_t source, length, expanded; id<MTLBuffer> buffer; } z_index_cache[64];
static struct { uint64_t source, length; uint8_t bytes[256]; } z_uniform_cache[64];
static unsigned z_index_count, z_uniform_count;
// CPU conditional-render fallback: words sampled after a completed GPU fence stay valid until a
// writer touches them. Keeping the words (rather than a bool) also handles changes in comparison mode.
enum { ZELDA_PREDICATE_CACHE_CAPACITY = 512 };
static struct { uint64_t source, words[4]; unsigned valid; } z_predicate_cache[ZELDA_PREDICATE_CACHE_CAPACITY];
static unsigned z_predicate_count;
static uint64_t z_predicate_forwarded;
static uint64_t z_buffer_submit, z_buffer_queue, z_buffer_registry;
static void zelda_index_remove(unsigned i) {
    defer_obj_release(z_index_cache[i].buffer);
    z_index_cache[i] = z_index_cache[--z_index_count];
    z_index_cache[z_index_count].buffer = nil;
}
static void zelda_index_forget(void) { while (z_index_count) zelda_index_remove(z_index_count - 1); }
static void zelda_buffer_forget(void) { zelda_index_forget(); z_uniform_count = z_predicate_count = 0; }
static void zelda_buffer_cache_sync(void) {
    uint64_t registry = atomic_load(&z_pool_generation);
    // A new guest submit may contain CPU writes; a slice only changes command-buffer scratch storage.
    if (z_buffer_queue != z_queue_submit || z_buffer_registry != registry) zelda_buffer_forget();
    if (z_buffer_submit != g_sub_serial) {
        for (unsigned i = 0; i < z_index_count;) {
            if (!z_index_cache[i].buffer) zelda_index_remove(i); else i++;
        }
    }
    z_buffer_submit = g_sub_serial; z_buffer_queue = z_queue_submit; z_buffer_registry = registry;
}
static void zelda_buffer_invalidate(uint64_t address, uint64_t length) {
    zelda_buffer_cache_sync();
    if (!length) return;
    uint64_t source = nvn_canonical_address(address, length);
    if (!source) { zelda_buffer_forget(); return; }
    for (unsigned i = 0; i < z_index_count;) {
        if (source < z_index_cache[i].source + z_index_cache[i].length && z_index_cache[i].source < source + length)
            zelda_index_remove(i);
        else i++;
    }
    for (unsigned i = 0; i < z_uniform_count;) {
        if (source < z_uniform_cache[i].source + z_uniform_cache[i].length && z_uniform_cache[i].source < source + length)
            z_uniform_cache[i] = z_uniform_cache[--z_uniform_count];
        else i++;
    }
    for (unsigned i = 0; i < z_predicate_count;) {
        for (unsigned word = 0; word < 2; word++) {
            uint64_t at = z_predicate_cache[i].source + 16 * word;
            if (source < at + 8 && at < source + length) z_predicate_cache[i].valid &= ~(1u << word);
        }
        if (!z_predicate_cache[i].valid) z_predicate_cache[i] = z_predicate_cache[--z_predicate_count];
        else i++;
    }
}
static void zelda_predicate_timestamp(uint64_t address) {
    uint64_t source = nvn_canonical_address(address, 16);
    if (!source) return;
    // A timestamp report's counter payload is exactly zero; its separate time word stays unknown.
    // This forwards the command's value, without reading GPU storage or waiting for its CPU callback.
    for (unsigned i = 0; i < z_predicate_count; i++) for (unsigned word = 0; word < 2; word++)
        if (source == z_predicate_cache[i].source + 16 * word) {
            z_predicate_cache[i].words[2 * word] = 0;
            z_predicate_cache[i].valid |= 1u << word; z_predicate_forwarded++;
        }
}
static void zelda_buffer_texture_written(const tex_t *texture) {
    if (!texture || !texture->mem || !texture->L.size) zelda_buffer_forget();
    else zelda_buffer_invalidate((uint64_t)texture->mem, texture->L.size);
}
static int zelda_buffer_write_op(unsigned op, const void *data) {
    uint64_t address, length;
    switch (op) {
        case OP_UPDATE_UB:
            memcpy(&address, data, 8); memcpy(&length, (const uint8_t *)data + 8, 8); break;
        case OP_COPY_B2B:
            memcpy(&address, (const uint8_t *)data + 8, 8); memcpy(&length, (const uint8_t *)data + 16, 8); break;
        case OP_REPORT_COUNTER:
            memcpy(&address, (const uint8_t *)data + 8, 8); length = 16; break;
        case OP_SIGNAL_EVENT:
            memcpy(&address, data, 8); length = 4; break;
        case OP_COPY_B2T:
            zelda_buffer_texture_written(((const p_copy_b2t *)data)->tex); return 0;
        case OP_COPY_T2T:
            zelda_buffer_texture_written(((const p_copy_t2t *)data)->dst); return 0;
        case OP_CLEAR_TEX:
            zelda_buffer_texture_written(((const p_clear_tex *)data)->tex); return 0;
        case OP_DISPATCH:
            if (!zelda_index_reuse_enabled()) zelda_buffer_forget();
            z_uniform_count = 0; return 0; // Actual shader writes are checked at dispatch.
        case OP_COPY_T2B:
            zelda_buffer_forget(); return 0;
        default: return 0;
    }
    zelda_buffer_cache_sync();
    uint64_t source = nvn_canonical_address(address, length);
    const uint8_t *bytes = op == OP_UPDATE_UB ? (const uint8_t *)data + 16 : NULL;
    if (op == OP_UPDATE_UB && source && length && length <= 256)
        for (unsigned i = 0; i < z_uniform_count; i++)
            if (source >= z_uniform_cache[i].source && source - z_uniform_cache[i].source <= z_uniform_cache[i].length &&
                length <= z_uniform_cache[i].length - (source - z_uniform_cache[i].source) &&
                !memcmp(z_uniform_cache[i].bytes + source - z_uniform_cache[i].source, bytes, length)) return 1;
    zelda_buffer_invalidate(address, length);
    if (op == OP_REPORT_COUNTER) {
        uint64_t type; memcpy(&type, data, 8);
        if (type == NVN_COUNTER_TYPE_TIMESTAMP || type == NVN_COUNTER_TYPE_TIMESTAMP_TOP) zelda_predicate_timestamp(address);
    }
    if (op == OP_UPDATE_UB && source && length && length <= 256 && z_uniform_count < 64) {
        unsigned i = z_uniform_count++;
        z_uniform_cache[i].source = source; z_uniform_cache[i].length = length;
        memcpy(z_uniform_cache[i].bytes, bytes, length);
    }
    return 0;
}
