#import <Foundation/Foundation.h>
#import <Metal/Metal.h>
#include <assert.h>
#include <stdatomic.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "nvn_obj.h"
#include "nvn_refl.h"

#define DM_NVN_M3 1
static id<MTLCommandQueue> queue;
static id<MTLCommandBuffer> command, committed;
static id<MTLBuffer> queries;
static uint64_t g_sub_serial, z_queue_submit;
static _Atomic uint64_t z_pool_generation = 1;
static int g_pso_dirty, g_dss_dirty;
static struct { p_bind_buf ssbo[6][16]; } S;
static void defer_obj_release(id buffer) { assert(!buffer); }
static void args_dirty_all(void) {}
uint64_t nvn_canonical_address(uint64_t address, uint64_t length) {
    if (!address || length > UINT64_MAX-address) return 0;
    return address >= 0x2000 && address+length <= 0x3000 ? address-0x1000 : address;
}
static int mtl_addr(uint64_t address, uint64_t length, void **buffer, uint64_t *offset) {
    address = nvn_canonical_address(address, length);
    if (address < 0x1000 || address+length > 0x1000+queries.length) return 0;
    *buffer = (__bridge void *)queries; *offset = address-0x1000; return 1;
}
static void mtl_sub_begin(void) { command = [queue commandBuffer]; g_sub_serial++; z_queue_submit++; }
static uint64_t mtl_sub_commit(void) { committed = command; [command commit]; command = nil; return g_sub_serial; }
static int mtl_wait(uint64_t fence, uint64_t timeout) {
    (void)fence; (void)timeout; [committed waitUntilCompleted]; return committed.status == MTLCommandBufferStatusCompleted;
}
#define PD(...) ((void)0)
#define MDIE(...) do { fprintf(stderr, __VA_ARGS__); abort(); } while (0)
#include "../host/nvn_buffer_cache.inc"
#include "../host/nvn_buffer_writes.inc"
#include "../host/nvn_conditional_mtl.inc"
@interface NvShader : NSObject { @public nvn_refl rf; } @end
@implementation NvShader @end
@interface NvProg : NSObject { @public NvShader *sh[6]; } @end
@implementation NvProg @end
static NvProg *prog_get(const prog_t *program) { return (__bridge NvProg *)(const void *)program; }
static struct { p_set_rt rt; } R;
static const uint8_t *g_exec_cur, *g_exec_next;
typedef struct { int kind; volatile uint32_t *val; uint32_t mode,v; uint64_t addr; uint32_t type; void *vis; } post_op;
static pthread_mutex_t g_mu = PTHREAD_MUTEX_INITIALIZER;
static uint64_t g_done;
static post_op g_post[4];
static int g_npost;
#include "m3_conditional_plan.inc"
#include "../host/nvn_conditional_cpu.inc"

static size_t emit(uint8_t *where, unsigned op, const void *data, size_t size) {
    size_t bytes = (size+15)&~(size_t)7;
    op_hdr header = {.op=op,.size=bytes}; memcpy(where, &header, 8);
    if (size) memcpy(where+8, data, size);
    return bytes;
}
static void check_batch(void) {
    NvShader *shader = [NvShader new]; shader->rf.route_b = 1;
    NvProg *program = [NvProg new]; program->sh[0] = program->sh[1] = shader;
    zelda_conditional_state state = {.program=(__bridge const void *)program,
        .predicate={.address=0x1000,.mode=0},.line_width=1};
    uint8_t commands[256] __attribute__((aligned(8))); size_t used = 0;
    p_render_enable next = {.address=0x2040,.mode=1};
    p_draw draw = {.prim=3,.count=3,.instances=1};
    used += emit(commands+used, OP_RENDER_ENABLE, &next, sizeof next);
    used += emit(commands+used, OP_DRAW, &draw, sizeof draw);
    emit(commands+used, OP_END, NULL, 0);
    g_exec_cur = commands; g_exec_next = commands;
    uint64_t words[] = {3, 0, 3, 0, 0, 0, 0, 0, 4, 0, 9, 0};
    id<MTLBuffer> upload = [queue.device newBufferWithBytes:words length:sizeof words options:MTLResourceStorageModeShared];
    zelda_buffer_forget();
    id<MTLBlitCommandEncoder> encoder = [command blitCommandEncoder];
    [encoder copyFromBuffer:upload sourceOffset:0 toBuffer:queries destinationOffset:0 size:sizeof words]; [encoder endEncoding];
    uint64_t waits = z_predicate_waits;
    z_render = state.predicate;
    assert(zelda_conditional_cpu_draw(&draw, NULL, &state));
    assert(z_predicate_waits == waits+1 && z_predicate_prefetched == 1);
    z_render = next; state.predicate = next;
    assert(zelda_conditional_cpu_draw(&draw, NULL, &state) && z_predicate_waits == waits+1);
    // Counter writes, unknown commands, writable shaders and target aliases stop lookahead.
    state.predicate = (p_render_enable){.address=0x1000,.mode=0};
    zelda_conditional_input input; zelda_conditional_input_make(&draw, NULL, &state, &input);
    assert(zelda_conditional_collect(&input, state) == 2);
    op_hdr original; memcpy(&original, commands, 8);
    ((op_hdr *)commands)->op = OP_REPORT_COUNTER; assert(zelda_conditional_collect(&input, state) == 1);
    ((op_hdr *)commands)->op = OP_COUNT_; assert(zelda_conditional_collect(&input, state) == 1);
    memcpy(commands, &original, 8);
    shader->rf.nb=1; shader->rf.b[0]=(nvn_rbind){.kind=NVN_RB_SBUFP,.rw=1};
    assert(zelda_conditional_collect(&input, state) == 1); shader->rf.nb=0;
    tex_t target={.mem=(void *)0x2000,.L={.size=128}}; R.rt.n=1; R.rt.color[0]=&target;
    assert(zelda_conditional_collect(&input, state) == 1); R.rt.n=0;
    puts("Metal 3 conditional batch passed: two different GPU predicates use one wait; writes, unknown commands and target aliases stop snapshots");
}

static void write_query(uint64_t left, uint64_t right) {
    uint64_t words[] = {left, 0, right, 0};
    id<MTLBuffer> upload = [queue.device newBufferWithBytes:words length:sizeof words options:MTLResourceStorageModeShared];
    uint64_t copy[] = {0, 0x2000, sizeof words}; zelda_buffer_write_op(OP_COPY_B2B, copy);
    id<MTLBlitCommandEncoder> encoder = [command blitCommandEncoder];
    [encoder copyFromBuffer:upload sourceOffset:0 toBuffer:queries destinationOffset:0 size:sizeof words];
    [encoder endEncoding];
}
int main(void) { @autoreleasepool {
    setenv("ZELDA_M3_PREDICATE_ASYNC", "0", 1);
    id<MTLDevice> device = MTLCreateSystemDefaultDevice(); assert(device);
    queue = [device newCommandQueue]; queries = [device newBufferWithLength:4096 options:MTLResourceStorageModeShared];
    mtl_sub_begin(); write_query(7, 7);
    z_render = (p_render_enable){.address=0x1000,.mode=0};
    assert(zelda_render_enabled() && z_predicate_waits == 1);
    z_render.address = 0x2000;
    assert(zelda_render_enabled() && z_predicate_hits == 1 && z_predicate_waits == 1);
    z_render.mode = 1;
    assert(!zelda_render_enabled() && z_predicate_hits == 2); // Compare current mode against cached words.
    uint64_t unrelated[] = {0, 0x2080, 4}; zelda_buffer_write_op(OP_COPY_B2B, unrelated);
    assert(!zelda_render_enabled() && z_predicate_waits == 1);
    write_query(7, 9);
    assert(zelda_render_enabled() && z_predicate_waits == 2);
    // A shader write must invalidate even when no index/uniform cache entry exists.
    nvn_refl reflection = {.nb=1}; reflection.b[0] = (nvn_rbind){.kind=NVN_RB_SBUFP,.rw=1,.reg=2};
    S.ssbo[5][2] = (p_bind_buf){.addr=0x2010,.size=8};
    zelda_buffer_shader_written(&reflection, 5); assert(zelda_predicate_find(z_render.address) < 0);
    assert(zelda_render_enabled() && z_predicate_waits == 3);
    tex_t texture = {.mem=(void *)0x2000,.L={.size=32}};
    p_clear_tex clear = {.tex=&texture}; zelda_buffer_write_op(OP_CLEAR_TEX, &clear); assert(!z_predicate_count);
    assert(zelda_render_enabled());
    uint64_t report[] = {NVN_COUNTER_TYPE_TIMESTAMP_TOP, 0x2000};
    zelda_buffer_write_op(OP_REPORT_COUNTER, report);
    assert(z_predicate_count && z_predicate_forwarded == 1);
    // Model a query written by the CPU completion callback, after earlier GPU work completes.
    [command addCompletedHandler:^(id<MTLCommandBuffer> cb) {
        (void)cb; uint64_t zero = 0; memcpy(queries.contents, &zero, 8);
    }];
    uint64_t before = z_predicate_waits;
    assert(zelda_render_enabled() && z_predicate_waits == before); // Forwarded 0 != 9, callback still pending.
    assert(z_predicate_cache[0].words[0] == 0);
    assert(((const uint64_t *)queries.contents)[0] == 7);
    mtl_sub_commit(); mtl_wait(g_sub_serial, 0); mtl_sub_begin();
    assert(((const uint64_t *)queries.contents)[0] == 0);
    zelda_buffer_cache_sync(); assert(!z_predicate_count);
    assert(zelda_render_enabled());
    // Unknown sample counts and even a one-byte write cannot retain a previously known word.
    uint64_t sample[] = {NVN_COUNTER_TYPE_SAMPLES_PASSED, 0x2000};
    zelda_buffer_write_op(OP_REPORT_COUNTER, sample); assert(zelda_predicate_find(0x1000) < 0);
    assert(zelda_render_enabled());
    uint64_t partial[] = {0, 0x2004, 1};
    zelda_buffer_write_op(OP_COPY_B2B, partial); assert(zelda_predicate_find(0x1000) < 0);
    assert(zelda_render_enabled());
    z_pool_generation++; zelda_buffer_cache_sync(); assert(!z_predicate_count);
    assert(zelda_render_enabled());
    reflection.b[0].kind = NVN_RB_IMG; zelda_buffer_shader_written(&reflection, 5); assert(!z_predicate_count);
    check_batch();
    z_render.address = 0; z_render.enabled = 0; assert(!zelda_render_enabled());
    z_render.enabled = 1; assert(zelda_render_enabled());
    mtl_sub_commit(); mtl_wait(g_sub_serial, 0);
    puts("Metal 3 predicate reuse passed: GPU/CPU writes, aliases, comparison modes, shader/texture invalidation and submit expiry");
} }
