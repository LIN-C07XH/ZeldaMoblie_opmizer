#import <Foundation/Foundation.h>
#import <Metal/Metal.h>
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "nvn_obj.h"

#define DM_NVN_M3 1
enum { PEND_MAX = 64 };
static int g_npend, g_pend_over;
static uint64_t g_pend_lo[PEND_MAX], g_pend_hi[PEND_MAX];
static id<MTLRenderCommandEncoder> g_enc;
static id<MTLBlitCommandEncoder> copy_encoder;
static id<MTLCommandBuffer> command;
static id<MTLBuffer> source, uniform;
static struct { uint64_t address; } z_render;
static int tbdr_on(void) { return 0; }
static int zelda_uniform_overlaps(uint64_t address, uint64_t length) { (void)address; (void)length; return 0; }
static int mtl_addr(uint64_t address, uint64_t length, void **buffer, uint64_t *offset) {
    id<MTLBuffer> found = address == 0x1000 ? source : address == 0x2000 ? uniform : nil;
    if (!found || length > found.length) return 0;
    *buffer = (__bridge void *)found; *offset = 0;
    return 1;
}
static int mtl_gpu_addr(uint64_t address, uint64_t length, void **buffer, uint64_t *offset, uint64_t *gpu) {
    if (!mtl_addr(address, length, buffer, offset)) return 0;
    *gpu = ((__bridge id<MTLBuffer>)*buffer).gpuAddress;
    return 1;
}
static id<MTLBlitCommandEncoder> blit(void) {
    assert(!g_enc);
    if (!copy_encoder) copy_encoder = [command blitCommandEncoder];
    return copy_encoder;
}
static void enc_b_copy_buffer(id<MTLBlitCommandEncoder> encoder, id<MTLBuffer> from, NSUInteger from_offset,
                             id<MTLBuffer> to, NSUInteger to_offset, NSUInteger length) {
    [encoder copyFromBuffer:from sourceOffset:from_offset toBuffer:to destinationOffset:to_offset size:length];
}
static void end_copy(void) { [copy_encoder endEncoding]; copy_encoder = nil; }
static void zelda_write_flush(void);
#define MDIE(...) do { fprintf(stderr, __VA_ARGS__); abort(); } while (0)
#include "../host/nvn_pass_writes.inc"

int main(void) { @autoreleasepool {
    // Even an explicit request must fall back while the Metal 3 in-pass writer is unavailable.
    setenv("ZELDA_PASS_WRITES", "1", 1);
    id<MTLDevice> device = MTLCreateSystemDefaultDevice(); assert(device);
    const float green[] = {0, 1, 0, 1}, blue[] = {0, 0, 1, 1};
    source = [device newBufferWithBytes:green length:sizeof green options:MTLResourceStorageModeShared];
    uniform = [device newBufferWithBytes:blue length:sizeof blue options:MTLResourceStorageModeShared];
    NSError *error = nil;
    id<MTLLibrary> library = [device newLibraryWithSource:@"#include <metal_stdlib>\nusing namespace metal;\n"
        "vertex float4 v(uint i [[vertex_id]]) { float2 p[3]={float2(-1,-1),float2(3,-1),float2(-1,3)}; return float4(p[i],0,1); }\n"
        "fragment float4 f(constant float4 &color [[buffer(0)]]) { return color; }" options:nil error:&error];
    assert(library && !error);
    MTLRenderPipelineDescriptor *pd = [MTLRenderPipelineDescriptor new];
    pd.vertexFunction = [library newFunctionWithName:@"v"]; pd.fragmentFunction = [library newFunctionWithName:@"f"];
    pd.colorAttachments[0].pixelFormat = MTLPixelFormatRGBA8Unorm;
    id<MTLRenderPipelineState> pipeline = [device newRenderPipelineStateWithDescriptor:pd error:&error]; assert(pipeline);
    MTLTextureDescriptor *td = [MTLTextureDescriptor texture2DDescriptorWithPixelFormat:MTLPixelFormatRGBA8Unorm width:16 height:16 mipmapped:NO];
    td.usage = MTLTextureUsageRenderTarget; td.storageMode = MTLStorageModePrivate;
    id<MTLTexture> target = [device newTextureWithDescriptor:td];
    id<MTLCommandQueue> queue = [device newCommandQueue]; command = [queue commandBuffer];
    if (!zelda_write_defer(0x1000, 0x2000, sizeof green))
        enc_b_copy_buffer(blit(), source, 0, uniform, 0, sizeof green);
    end_copy();
    MTLRenderPassDescriptor *pass = [MTLRenderPassDescriptor new];
    pass.colorAttachments[0].texture = target;
    pass.colorAttachments[0].loadAction = MTLLoadActionClear;
    pass.colorAttachments[0].storeAction = MTLStoreActionStore;
    g_enc = [command renderCommandEncoderWithDescriptor:pass];
    zelda_write_encode();
    [g_enc setRenderPipelineState:pipeline]; [g_enc setFragmentBuffer:uniform offset:0 atIndex:0];
    [g_enc drawPrimitives:MTLPrimitiveTypeTriangle vertexStart:0 vertexCount:3];
    [g_enc endEncoding]; g_enc = nil;
    // Submission-end writes must not be the first point at which the new constants become visible.
    zelda_write_flush();
    id<MTLBuffer> pixels = [device newBufferWithLength:16 * 256 options:MTLResourceStorageModeShared];
    [blit() copyFromTexture:target sourceSlice:0 sourceLevel:0 sourceOrigin:MTLOriginMake(0, 0, 0)
        sourceSize:MTLSizeMake(16, 16, 1) toBuffer:pixels destinationOffset:0 destinationBytesPerRow:256 destinationBytesPerImage:16 * 256];
    end_copy(); [command commit]; [command waitUntilCompleted];
    assert(command.status == MTLCommandBufferStatusCompleted && !command.error);
    for (unsigned y = 0; y < 16; y++) for (unsigned x = 0; x < 16; x++) {
        const uint8_t *pixel = (const uint8_t *)pixels.contents + y * 256 + x * 4;
        if (pixel[0] || pixel[1] != 255 || pixel[2] || pixel[3] != 255) {
            fprintf(stderr, "Metal 3 stale uniform: expected green, got [%u,%u,%u,%u]\n", pixel[0],pixel[1],pixel[2],pixel[3]);
            return 1;
        }
    }
    assert(!z_write_n && !z_write_deferred);
    puts("Metal 3 constant update passed: all 256 pixels see the current update before the draw");
    return 0;
} }
