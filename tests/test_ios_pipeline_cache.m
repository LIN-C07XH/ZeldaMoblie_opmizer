#import <Foundation/Foundation.h>
#import <Metal/Metal.h>
#include <assert.h>
#include <stdio.h>
#include <unistd.h>
#include <TargetConditionals.h>
// Exercise the iOS policy with the host Metal driver; no device install is needed.
#ifndef TEST_DESKTOP_PIPELINE
#undef TARGET_OS_IPHONE
#define TARGET_OS_IPHONE 1
#endif
static NSString *TEST_ROOT;
#define NSSearchPathForDirectoriesInDomains(...) @[TEST_ROOT]
#include "dmsh_chain.h"
#include "../host/ios_pipeline_cache.inc"

int main(int argc, char **argv) { @autoreleasepool {
    assert(argc == 3);
    // Archive fixtures explicitly enable caching, independent of the suite's isolation policy.
    setenv("ZELDA_PIPELINE_CACHE", "1", 1);
    TEST_ROOT = @(argv[1]);
    BOOL expectedHit = !strcmp(argv[2], "warm");
#ifdef TEST_DESKTOP_PIPELINE
    setenv("DM_MSL_DIR", [[TEST_ROOT stringByAppendingPathComponent:@"work/shaders"] fileSystemRepresentation], 1);
#endif
    id<MTLDevice> device = MTLCreateSystemDefaultDevice();
    if (!device) { fprintf(stderr, "Metal device unavailable\n"); return 77; }
    NSString *source = @"#include <metal_stdlib>\nusing namespace metal;\n"
        "kernel void cached_fixture(device uint *output [[buffer(0)]], uint i [[thread_position_in_grid]]) { output[i] = i * 7 + 3; }\n"
        "vertex float4 cached_vertex(uint i [[vertex_id]]) { return float4(float(i), 0, 0, 1); }\n"
        "fragment float4 cached_fragment() { return float4(1); }\n";
    NSError *error = nil;
    id<MTLLibrary> library = [device newLibraryWithSource:source options:nil error:&error];
    assert(library && !error);
    id<MTLFunction> function = [library newFunctionWithName:@"cached_fixture"];
    id<MTLComputePipelineState> pipeline = cachedComputePipeline(device, function, &error);
    assert(pipeline && !error);
    dispatch_sync(PIPELINE_CACHE_QUEUE, ^{});
    assert((PIPELINE_READ_ARCHIVE != nil) == expectedHit);
    // A warm hit must not fall back to compilation/capture in this process.
    assert(PIPELINE_ARCHIVE_DIRTY == !expectedHit);
    MTLRenderPipelineDescriptor *descriptor = [MTLRenderPipelineDescriptor new];
    descriptor.vertexFunction = [library newFunctionWithName:@"cached_vertex"];
    descriptor.fragmentFunction = [library newFunctionWithName:@"cached_fragment"];
    descriptor.colorAttachments[0].pixelFormat = MTLPixelFormatRGBA8Unorm;
    assert(cachedRenderPipeline(device, descriptor, &error) && !error);
    dispatch_sync(PIPELINE_CACHE_QUEUE, ^{});
    assert(!descriptor.binaryArchives.count);
    assert(PIPELINE_ARCHIVE_DIRTY == !expectedHit);
    // Changing the descriptor is a legitimate miss; capture it without losing old entries.
    descriptor.colorAttachments[0].pixelFormat = MTLPixelFormatBGRA8Unorm;
    assert(cachedRenderPipeline(device, descriptor, &error) && !error);
#ifdef TEST_DESKTOP_PIPELINE
    if (!strcmp(argv[2], "bounded")) {
        for (unsigned index = 0; index < DESKTOP_PIPELINE_CAPTURE_LIMIT * 2; index++) capturePipeline(descriptor, NO);
        assert(atomic_load(&PIPELINE_CAPTURE_REQUESTS) == DESKTOP_PIPELINE_CAPTURE_LIMIT);
    }
#endif
    id<MTLBuffer> output = [device newBufferWithLength:256 options:MTLResourceStorageModeShared];
    id<MTLCommandQueue> queue = [device newCommandQueue];
    id<MTLCommandBuffer> command = [queue commandBuffer];
    id<MTLComputeCommandEncoder> encoder = [command computeCommandEncoder];
    [encoder setComputePipelineState:pipeline];
    [encoder setBuffer:output offset:0 atIndex:0];
    [encoder dispatchThreadgroups:MTLSizeMake(1, 1, 1) threadsPerThreadgroup:MTLSizeMake(64, 1, 1)];
    [encoder endEncoding]; [command commit]; [command waitUntilCompleted];
    assert(!command.error);
    for (unsigned i = 0; i < 64; i++) assert(((uint32_t *)output.contents)[i] == i * 7 + 3);
    dispatch_sync(PIPELINE_CACHE_QUEUE, ^{ flushPipelineCache(); });
    assert(!PIPELINE_ARCHIVE_DIRTY && [NSFileManager.defaultManager fileExistsAtPath:PIPELINE_ARCHIVE_PATH]);
    puts(expectedHit ? "Metal pipeline archive: warm GPU readback passed" : "Metal pipeline archive: cold capture and GPU readback passed");
} }
