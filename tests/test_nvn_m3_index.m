#import <Foundation/Foundation.h>
#import <Metal/Metal.h>
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdatomic.h>
#include "nvn_obj.h"

#define DM_NVN_M3 1
static id<MTLDevice> g_dev;
static id<MTLCommandBuffer> g_cb;
static id<MTLBlitCommandEncoder> g_blit;
static id<MTLBuffer> source_buffer, arena;
static size_t used;
static unsigned pass_ends;
static id<MTLRenderPipelineState> render_pipeline;
static uint64_t g_sub_serial, z_queue_submit;
static _Atomic uint64_t z_pool_generation = 1;
static void defer_obj_release(id buffer) { assert(!buffer); }
uint64_t nvn_canonical_address(uint64_t address, uint64_t length) {
    if (!address || length > UINT64_MAX - address) return 0;
    return address >= 0x2000 && address + length <= 0x3000 ? address - 0x1000 : address;
}
static void rp_end(void) { pass_ends++; }
static void zelda_dispatch_ensure_ended(void) {}
static void args_dirty_all(void) {}
static void enc_b_end(id<MTLBlitCommandEncoder> encoder) { [encoder endEncoding]; }
static id<MTLComputeCommandEncoder> enc_c_begin(MTLComputePassDescriptor *descriptor) {
    return descriptor ? [g_cb computeCommandEncoderWithDescriptor:descriptor] : [g_cb computeCommandEncoder];
}
static void enc_c_end(id<MTLComputeCommandEncoder> encoder) { [encoder endEncoding]; }
static int mtl_gpu_addr(uint64_t address, uint64_t length, void **buffer, uint64_t *offset, uint64_t *gpu) {
    address = nvn_canonical_address(address, length);
    if (address < 0x1000 || address - 0x1000 > source_buffer.length ||
        length > source_buffer.length - (address - 0x1000)) return 0;
    *buffer = (__bridge void *)source_buffer;
    *offset = address - 0x1000;
    *gpu = source_buffer.gpuAddress + *offset;
    return 1;
}
static size_t arena_alloc(size_t bytes, id<MTLBuffer> *buffer) {
    size_t offset = used;
    used += (bytes + 255) & ~(size_t)255;
    assert(used <= arena.length);
    *buffer = arena;
    return offset;
}
void shader_status_note(unsigned event, unsigned total) { (void)event; (void)total; }
#define MDIE(...) do { fprintf(stderr, __VA_ARGS__); abort(); } while (0)
#include "../host/nvn_buffer_cache.inc"
#include "../host/nvn_index.inc"

static void check(id<MTLCommandQueue> queue, MTLResourceOptions storage) {
    g_sub_serial++; z_queue_submit++;
    source_buffer = [g_dev newBufferWithLength:4096 options:storage];
    arena = [g_dev newBufferWithLength:4096 options:MTLResourceStorageModeShared];
    assert(source_buffer && arena);
    memset(arena.contents, 0xa5, arena.length);
    const uint8_t expected[] = {0, 1, 2, 2, 3, 0, 255, 128};
    id<MTLBuffer> upload = [g_dev newBufferWithBytes:expected length:sizeof expected options:MTLResourceStorageModeShared];
    g_cb = [queue commandBuffer];
    // The source is written by an earlier GPU operation in this same command buffer.
    g_blit = [g_cb blitCommandEncoder];
    [g_blit copyFromBuffer:upload sourceOffset:0 toBuffer:source_buffer destinationOffset:37 size:sizeof expected];
    used = 256;
    p_draw_idx draw = {.count = sizeof expected, .ibuf = 0x1000 + 37};
    uint64_t result = zelda_expand_indices(&draw);
    unsigned ended = pass_ends;
    assert(zelda_expand_indices(&draw) == result && pass_ends == ended);
    draw.ibuf += 0x1000;
    assert(zelda_expand_indices(&draw) == result && pass_ends == ended);
    if (g_blit) { [g_blit endEncoding]; g_blit = nil; }
    assert(result >= arena.gpuAddress && result + 2 * sizeof expected <= arena.gpuAddress + arena.length);
    size_t offset = (size_t)(result - arena.gpuAddress);
    MTLTextureDescriptor *descriptor = [MTLTextureDescriptor texture2DDescriptorWithPixelFormat:MTLPixelFormatRGBA8Unorm
        width:32 height:32 mipmapped:NO];
    descriptor.storageMode = MTLStorageModePrivate;
    descriptor.usage = MTLTextureUsageRenderTarget;
    id<MTLTexture> target = [g_dev newTextureWithDescriptor:descriptor];
    MTLRenderPassDescriptor *pass = [MTLRenderPassDescriptor new];
    pass.colorAttachments[0].texture = target;
    pass.colorAttachments[0].loadAction = MTLLoadActionClear;
    pass.colorAttachments[0].storeAction = MTLStoreActionStore;
    pass.colorAttachments[0].clearColor = MTLClearColorMake(1, 0, 1, 1);
    id<MTLRenderCommandEncoder> render = [g_cb renderCommandEncoderWithDescriptor:pass];
    [render setRenderPipelineState:render_pipeline];
    [render drawIndexedPrimitives:MTLPrimitiveTypeTriangle indexCount:6 indexType:MTLIndexTypeUInt16
        indexBuffer:arena indexBufferOffset:offset];
    [render endEncoding];
    id<MTLBuffer> pixels = [g_dev newBufferWithLength:32 * 256 options:MTLResourceStorageModeShared];
    id<MTLBlitCommandEncoder> copy = [g_cb blitCommandEncoder];
    [copy copyFromTexture:target sourceSlice:0 sourceLevel:0 sourceOrigin:MTLOriginMake(0, 0, 0)
        sourceSize:MTLSizeMake(32, 32, 1) toBuffer:pixels destinationOffset:0 destinationBytesPerRow:256 destinationBytesPerImage:32 * 256];
    [copy endEncoding];
    [g_cb commit];
    [g_cb waitUntilCompleted];
    assert(g_cb.status == MTLCommandBufferStatusCompleted && !g_cb.error);
    const uint16_t *indices = (const uint16_t *)((const uint8_t *)arena.contents + offset);
    for (unsigned i = 0; i < sizeof expected; i++) {
        if (indices[i] != expected[i]) {
            fprintf(stderr, "Metal 3 index mismatch: storage=%lu index=%u expected=%u actual=%u\n",
                (unsigned long)storage, i, expected[i], indices[i]);
            exit(1);
        }
    }
    const uint8_t *bytes = arena.contents;
    assert(bytes[offset - 1] == 0xa5 && bytes[offset + 2 * sizeof expected] == 0xa5);
    for (unsigned y = 0; y < 32; y++) for (unsigned x = 0; x < 32; x++) {
        const uint8_t *pixel = (const uint8_t *)pixels.contents + y * 256 + x * 4;
        assert(pixel[0] == 0 && pixel[1] == 255 && pixel[2] == 0 && pixel[3] == 255);
    }
    printf("Metal 3 indexed rendering passed: storage=%lu offsets, preceding GPU writes, index bytes and all 1024 output pixels\n",
        (unsigned long)storage);
}

static void check_invalidation(id<MTLCommandQueue> queue) {
    g_sub_serial++; z_queue_submit++; used = 0;
    source_buffer = [g_dev newBufferWithLength:4096 options:MTLResourceStorageModePrivate];
    arena = [g_dev newBufferWithLength:65536 options:MTLResourceStorageModeShared];
    g_cb = [queue commandBuffer];
    g_blit = [g_cb blitCommandEncoder];
    [g_blit fillBuffer:source_buffer range:NSMakeRange(0, 4096) value:7];
    p_draw_idx draw = {.count=16, .ibuf=0x1000};
    uint64_t original = zelda_expand_indices(&draw);
    uint64_t unrelated[] = {0x1100, 4, 0};
    zelda_buffer_write_op(OP_UPDATE_UB, unrelated);
    assert(zelda_expand_indices(&draw) == original);
    uint64_t mutation[] = {0, 0x2008, 4};
    zelda_buffer_write_op(OP_COPY_B2B, mutation);
    assert(!z_index_count);
    g_blit = [g_cb blitCommandEncoder];
    [g_blit fillBuffer:source_buffer range:NSMakeRange(8, 4) value:23];
    uint64_t changed = zelda_expand_indices(&draw);
    assert(changed != original && zelda_expand_indices(&draw) == changed);
    [g_cb commit]; [g_cb waitUntilCompleted]; assert(!g_cb.error);
    const uint16_t *first = (const void *)((const uint8_t *)arena.contents + original - arena.gpuAddress);
    const uint16_t *second = (const void *)((const uint8_t *)arena.contents + changed - arena.gpuAddress);
    for (unsigned i = 0; i < 16; i++) {
        assert(first[i] == 7);
        assert(second[i] == (i >= 8 && i < 12 ? 23 : 7));
    }
    // Slices may reset the arena; never reuse its old bytes in a new command buffer.
    g_sub_serial++; zelda_buffer_cache_sync(); assert(!z_index_count);
    used = 0; memset(arena.contents, 0xa5, arena.length);
    g_cb = [queue commandBuffer];
    zelda_expand_indices(&draw);
    z_queue_submit++; zelda_buffer_cache_sync(); assert(!z_index_count);
    zelda_expand_indices(&draw);
    z_pool_generation++; zelda_buffer_cache_sync(); assert(!z_index_count);
    zelda_expand_indices(&draw);
    tex_t texture = {.mem=(void *)0x2000, .L={.size=16}};
    p_clear_tex clear = {.tex=&texture}; zelda_buffer_write_op(OP_CLEAR_TEX, &clear); assert(!z_index_count);
    zelda_expand_indices(&draw);
    zelda_buffer_invalidate(UINT64_MAX, 2); assert(!z_index_count);
    [g_cb commit]; [g_cb waitUntilCompleted]; assert(!g_cb.error);
    puts("Metal 3 index reuse passed: aliases, overlapping GPU writes, original reader bytes, slice/submit/pool expiry and texture writes");
}

int main(void) { @autoreleasepool {
    g_dev = MTLCreateSystemDefaultDevice();
    assert(g_dev);
    NSError *error = nil;
    id<MTLLibrary> library = [g_dev newLibraryWithSource:@"#include <metal_stdlib>\nusing namespace metal;\n"
        "vertex float4 v(uint i [[vertex_id]]) { float2 p[4]={float2(-1,-1),float2(1,-1),float2(1,1),float2(-1,1)}; return float4(p[i],0,1); }\n"
        "fragment float4 f() { return float4(0,1,0,1); }" options:nil error:&error];
    assert(library && !error);
    MTLRenderPipelineDescriptor *pipeline_descriptor = [MTLRenderPipelineDescriptor new];
    pipeline_descriptor.vertexFunction = [library newFunctionWithName:@"v"];
    pipeline_descriptor.fragmentFunction = [library newFunctionWithName:@"f"];
    pipeline_descriptor.colorAttachments[0].pixelFormat = MTLPixelFormatRGBA8Unorm;
    render_pipeline = [g_dev newRenderPipelineStateWithDescriptor:pipeline_descriptor error:&error];
    assert(render_pipeline && !error);
    id<MTLCommandQueue> queue = [g_dev newCommandQueue];
    check(queue, MTLResourceStorageModeShared);
    check(queue, MTLResourceStorageModePrivate);
    assert(pass_ends == 2 && z_index_expansions == 2);
    assert(z_index_hits == 4);
    check_invalidation(queue);
    return 0;
} }
