#import <Foundation/Foundation.h>
#import <Metal/Metal.h>
#include <assert.h>
#include <math.h>
#include <stdatomic.h>
#include <stdio.h>

#define DM_NVN_M3 1
#define CBUF id<MTLCommandBuffer>
static id<MTLDevice> g_dev;
static void res_add(id resource) { (void)resource; }
static void res_remove(id resource) { (void)resource; }
static void res_commit(void) {}
#define MLOG(...) ((void)0)
#include "../host/nvn_present_scale.inc"

static void check(id<MTLCommandQueue> queue, int mode, NSUInteger width, NSUInteger height,
                  NSUInteger destination_width, NSUInteger destination_height) {
    MTLTextureDescriptor *d = [MTLTextureDescriptor texture2DDescriptorWithPixelFormat:MTLPixelFormatRGBA8Unorm
        width:width height:height mipmapped:NO];
    d.storageMode = MTLStorageModePrivate;
    d.usage = MTLTextureUsageShaderRead | MTLTextureUsageRenderTarget;
    id<MTLTexture> input = [g_dev newTextureWithDescriptor:d]; assert(input);
    id<MTLCommandBuffer> command = [queue commandBuffer];
    MTLRenderPassDescriptor *pass = [MTLRenderPassDescriptor new];
    pass.colorAttachments[0].texture = input;
    pass.colorAttachments[0].loadAction = MTLLoadActionClear;
    pass.colorAttachments[0].storeAction = MTLStoreActionStore;
    pass.colorAttachments[0].clearColor = MTLClearColorMake(0, 1, 0, 1);
    id<MTLRenderCommandEncoder> fill = [command renderCommandEncoderWithDescriptor:pass]; [fill endEncoding];
    zelda_present_metalfx(mode); assert(zelda_present_metalfx_mode() == mode);
    id<MTLTexture> output = zelda_present_upscale(command, input, 0, 0, width, height, destination_width, destination_height);
    assert(output);
    assert(output.width == (mode == 2 ? 3840 : destination_width));
    assert(output.height == (mode == 2 ? 2160 : destination_height));
    id<MTLBuffer> pixel = [g_dev newBufferWithLength:256 options:MTLResourceStorageModeShared];
    id<MTLBlitCommandEncoder> copy = [command blitCommandEncoder];
    [copy copyFromTexture:output sourceSlice:0 sourceLevel:0
        sourceOrigin:MTLOriginMake(output.width / 2, output.height / 2, 0) sourceSize:MTLSizeMake(1, 1, 1)
        toBuffer:pixel destinationOffset:0 destinationBytesPerRow:256 destinationBytesPerImage:256];
    [copy endEncoding]; [command commit]; [command waitUntilCompleted];
    assert(command.status == MTLCommandBufferStatusCompleted && !command.error);
    const uint8_t *p = pixel.contents;
    assert(p[0] <= 1 && p[1] >= 254 && p[2] <= 1);
    zelda_present_metalfx(0);
    assert(!zelda_present_upscale(command, input, 0, 0, width, height, destination_width, destination_height));
    zelda_present_metalfx(1);
    assert(!zelda_present_upscale(command, input, 0, 0, width, height, width, height));
    printf("Metal 3 MetalFX passed: mode=%d %lux%lu -> %lux%lu, GPU output pixel=[%u,%u,%u,%u]\n", mode,
        (unsigned long)width, (unsigned long)height, (unsigned long)output.width, (unsigned long)output.height,p[0],p[1],p[2],p[3]);
}

int main(void) { @autoreleasepool {
    g_dev = MTLCreateSystemDefaultDevice(); assert(g_dev);
    assert([MTLFXSpatialScalerDescriptor supportsDevice:g_dev]);
    id<MTLCommandQueue> queue = [g_dev newCommandQueue];
    check(queue, 1, 64, 36, 128, 72);
    check(queue, 2, 1920, 1080, 2560, 1440);
    return 0;
} }
