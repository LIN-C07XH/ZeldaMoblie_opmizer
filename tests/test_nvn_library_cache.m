#import <Foundation/Foundation.h>
#import <Metal/Metal.h>
#include <assert.h>
#include <stdint.h>
#include "../host/nvn_library_cache.inc"

int main(void) { @autoreleasepool {
    id<MTLDevice> device = MTLCreateSystemDefaultDevice();
    if (!device) return 77;
    NSString *source = @"#include <metal_stdlib>\nusing namespace metal;\n"
        "kernel void retained_library(device uint *output [[buffer(0)]], uint i [[thread_position_in_grid]]) { output[i] = i + 9; }\n";
    NSData *data = [source dataUsingEncoding:NSUTF8StringEncoding];
    id<MTLLibrary> library = [device newLibraryWithSource:source options:nil error:nil];
    assert(library);
    rememberPrefetchedLibrary(@"fixture.metallib", data, library);
    id<MTLLibrary> reused = takePrefetchedLibrary(@"fixture.metallib", data.bytes, data.length);
    assert(reused == library);
    library = nil;
    assert(takePrefetchedLibrary(@"other.metallib", data.bytes, data.length) == nil);
    NSData *changed = [[source stringByAppendingString:@"\n// Changed input"] dataUsingEncoding:NSUTF8StringEncoding];
    assert(takePrefetchedLibrary(@"fixture.metallib", changed.bytes, changed.length) == nil);
    id<MTLComputePipelineState> pipeline = [device newComputePipelineStateWithFunction:[reused newFunctionWithName:@"retained_library"] error:nil];
    assert(pipeline);
    id<MTLBuffer> output = [device newBufferWithLength:256 options:MTLResourceStorageModeShared];
    id<MTLCommandBuffer> command = [[device newCommandQueue] commandBuffer];
    id<MTLComputeCommandEncoder> encoder = [command computeCommandEncoder];
    [encoder setComputePipelineState:pipeline];
    [encoder setBuffer:output offset:0 atIndex:0];
    [encoder dispatchThreadgroups:MTLSizeMake(1, 1, 1) threadsPerThreadgroup:MTLSizeMake(64, 1, 1)];
    [encoder endEncoding]; [command commit]; [command waitUntilCompleted];
    assert(!command.error);
    for (unsigned index = 0; index < 64; index++) assert(((uint32_t *)output.contents)[index] == index + 9);
    puts("Prefetched library: reuse, source invalidation and GPU output passed");
} }
