#import <Foundation/Foundation.h>
#import <Metal/Metal.h>
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define DM_NVN_M3 1
typedef id<MTLRenderCommandEncoder> RENC;
static id<MTLCommandBuffer> command;
static id<MTLBlitCommandEncoder> g_blit;
static id<MTLRenderCommandEncoder> render;
#include "../host/nvn_encoder_state.inc"
static void rp_end(void) { [render endEncoding]; render = nil; }
static void enc_b_end(id encoder) { [encoder endEncoding]; }
static void args_dirty_all(void) {}
static id<MTLComputeCommandEncoder> enc_c_begin(MTLComputePassDescriptor *descriptor) {
    zelda_dispatch_ensure_ended();
    return descriptor ? [command computeCommandEncoderWithDescriptor:descriptor] : [command computeCommandEncoder];
}
#include "../host/nvn_dispatch.inc"

int main(void) { @autoreleasepool {
    enum { WORDS = 1024, DISPATCHES = 32 };
    id<MTLDevice> device = MTLCreateSystemDefaultDevice(); assert(device);
    NSError *error = nil;
    id<MTLLibrary> library = [device newLibraryWithSource:@"#include <metal_stdlib>\nusing namespace metal;\n"
        "kernel void step(device uint *p [[buffer(0)]], uint i [[thread_position_in_grid]]) { p[i]=p[i]*3+1; }\n"
        "vertex float4 v(uint i [[vertex_id]]) { float2 p[3]={float2(-1,-1),float2(3,-1),float2(-1,3)}; return float4(p[i],0,1); }\n"
        "fragment float4 f(device uint *p [[buffer(0)]], constant uint &expected [[buffer(1)]]) { return p[0]==expected ? float4(0,1,0,1):float4(1,0,1,1); }\n"
        options:nil error:&error]; assert(library && !error);
    id<MTLComputePipelineState> pipeline = [device newComputePipelineStateWithFunction:[library newFunctionWithName:@"step"] error:&error];
    assert(pipeline && !error);
    id<MTLCommandQueue> queue = [device newCommandQueue]; command = [queue commandBuffer];
    id<MTLBuffer> data = [device newBufferWithLength:WORDS*4 options:MTLResourceStorageModePrivate];
    id<MTLBuffer> readback = [device newBufferWithLength:WORDS*4 options:MTLResourceStorageModeShared];
    g_blit = [command blitCommandEncoder]; [g_blit fillBuffer:data range:NSMakeRange(0, WORDS*4) value:0];
    uint32_t expected = 0;
    for (unsigned i = 0; i < DISPATCHES; i++) {
        id<MTLComputeCommandEncoder> encoder = zelda_dispatch_encoder();
        [encoder setComputePipelineState:pipeline]; [encoder setBuffer:data offset:0 atIndex:0];
        [encoder dispatchThreads:MTLSizeMake(WORDS,1,1) threadsPerThreadgroup:MTLSizeMake(64,1,1)];
        zelda_dispatch_end(); expected = expected*3+1;
    }
    assert(z_m3_dispatches == DISPATCHES && z_m3_dispatch_encoders == (z_m3_dispatch_batch ? 1 : DISPATCHES));
    // Opening a standalone compute pass closes the batch, then a copy sees the final dispatch.
    id<MTLComputeCommandEncoder> standalone = enc_c_begin(NULL); assert(!g_m3_dispatch_ce); [standalone endEncoding];
    zelda_dispatch_ensure_ended();
    g_blit = [command blitCommandEncoder];
    [g_blit copyFromBuffer:data sourceOffset:0 toBuffer:readback destinationOffset:0 size:WORDS*4];
    enc_b_end(g_blit); g_blit = nil;
    [command commit]; [command waitUntilCompleted]; assert(!command.error);
    for (unsigned i = 0; i < WORDS; i++) assert(((const uint32_t *)readback.contents)[i] == expected);

    // An ensuing render encoder closes the next compute batch before it reads the buffer.
    command = [queue commandBuffer];
    id<MTLComputeCommandEncoder> encoder = zelda_dispatch_encoder();
    [encoder setComputePipelineState:pipeline]; [encoder setBuffer:data offset:0 atIndex:0];
    [encoder dispatchThreads:MTLSizeMake(WORDS,1,1) threadsPerThreadgroup:MTLSizeMake(64,1,1)]; zelda_dispatch_end();
    zelda_encoder_state_reset(); assert(!g_m3_dispatch_ce);
    MTLTextureDescriptor *td = [MTLTextureDescriptor texture2DDescriptorWithPixelFormat:MTLPixelFormatRGBA8Unorm width:8 height:8 mipmapped:NO];
    td.usage = MTLTextureUsageRenderTarget; td.storageMode = MTLStorageModePrivate;
    MTLRenderPassDescriptor *pass = [MTLRenderPassDescriptor new]; pass.colorAttachments[0].texture = [device newTextureWithDescriptor:td];
    pass.colorAttachments[0].loadAction = MTLLoadActionClear; pass.colorAttachments[0].storeAction = MTLStoreActionStore;
    MTLRenderPipelineDescriptor *pd = [MTLRenderPipelineDescriptor new];
    pd.vertexFunction = [library newFunctionWithName:@"v"]; pd.fragmentFunction = [library newFunctionWithName:@"f"];
    pd.colorAttachments[0].pixelFormat = MTLPixelFormatRGBA8Unorm;
    id<MTLRenderPipelineState> raster = [device newRenderPipelineStateWithDescriptor:pd error:&error]; assert(raster);
    render = [command renderCommandEncoderWithDescriptor:pass]; [render setRenderPipelineState:raster];
    expected = expected*3+1;
    [render setFragmentBuffer:data offset:0 atIndex:0]; [render setFragmentBytes:&expected length:4 atIndex:1];
    [render drawPrimitives:MTLPrimitiveTypeTriangle vertexStart:0 vertexCount:3]; rp_end();
    id<MTLBuffer> pixels = [device newBufferWithLength:8*256 options:MTLResourceStorageModeShared];
    id<MTLBlitCommandEncoder> copy = [command blitCommandEncoder];
    [copy copyFromTexture:pass.colorAttachments[0].texture sourceSlice:0 sourceLevel:0 sourceOrigin:MTLOriginMake(0,0,0)
        sourceSize:MTLSizeMake(8,8,1) toBuffer:pixels destinationOffset:0 destinationBytesPerRow:256 destinationBytesPerImage:8*256];
    [copy endEncoding];
    [command commit]; [command waitUntilCompleted]; assert(!command.error);
    for (unsigned y=0; y<8; y++) for (unsigned x=0; x<8; x++) {
        const uint8_t *pixel = (const uint8_t *)pixels.contents+y*256+x*4;
        assert(pixel[0]==0 && pixel[1]==255 && pixel[2]==0 && pixel[3]==255);
    }
    printf("Metal 3 dispatch batch passed: %u dependent dispatches in %u encoder(s), GPU bytes and compute/render transitions\n",
        DISPATCHES, z_m3_dispatch_batch ? 1 : DISPATCHES);
} }
