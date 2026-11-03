#pragma once
// Included once in the adapted backend. Existing pipeline caches still own live PSOs.
// Device/OS/source-contract identity prevents reusing incompatible GPU binaries.
#include <TargetConditionals.h>
#include <CommonCrypto/CommonDigest.h>
#include "dmsh_chain.h"
#include "shader_ir_identity.h"
#include <pthread.h>
#include <stdatomic.h>
#if !TARGET_OS_IPHONE
enum { DESKTOP_PIPELINE_ARCHIVE_MAX_BYTES = 8u << 20, DESKTOP_PIPELINE_CAPTURE_LIMIT = 128 };
static _Atomic unsigned PIPELINE_CAPTURE_REQUESTS;
#endif
static pthread_mutex_t PIPELINE_CACHE_MUTEX = PTHREAD_MUTEX_INITIALIZER;
static id<MTLBinaryArchive> PIPELINE_READ_ARCHIVE, PIPELINE_WRITE_ARCHIVE;
static NSString *PIPELINE_ARCHIVE_PATH;
static BOOL PIPELINE_ARCHIVE_DIRTY, PIPELINE_FLUSH_SCHEDULED;
static dispatch_queue_t PIPELINE_CACHE_QUEUE;
static _Atomic unsigned PIPELINE_CACHE_HITS, PIPELINE_CACHE_MISSES;

static BOOL pipelineArchiveWithinBudget(NSString *path) {
#if !TARGET_OS_IPHONE
    NSNumber *size = [NSFileManager.defaultManager attributesOfItemAtPath:path error:nil][NSFileSize];
    if (!size || size.unsignedLongLongValue > DESKTOP_PIPELINE_ARCHIVE_MAX_BYTES) {
        fprintf(stderr, "PIPELINE_CACHE skip_oversized bytes=%llu limit=%u; using driver cache\n",
            size.unsignedLongLongValue, DESKTOP_PIPELINE_ARCHIVE_MAX_BYTES);
        return NO;
    }
#else
    (void)path;
#endif
    return YES;
}

static void notePipelineLookup(BOOL hit) {
    unsigned count = atomic_fetch_add(hit ? &PIPELINE_CACHE_HITS : &PIPELINE_CACHE_MISSES, 1) + 1;
    if (count == 1 || !(count % 500))
        fprintf(stderr, "PIPELINE_CACHE hits=%u misses=%u\n",
            atomic_load(&PIPELINE_CACHE_HITS), atomic_load(&PIPELINE_CACHE_MISSES));
}

static void flushPipelineCache(void) {
    pthread_mutex_lock(&PIPELINE_CACHE_MUTEX);
    PIPELINE_FLUSH_SCHEDULED = NO;
    if (PIPELINE_ARCHIVE_DIRTY) {
        NSString *temporary = [PIPELINE_ARCHIVE_PATH stringByAppendingString:@".pending"];
        NSError *error = nil;
        if ([PIPELINE_WRITE_ARCHIVE serializeToURL:[NSURL fileURLWithPath:temporary] error:&error] &&
            pipelineArchiveWithinBudget(temporary) &&
            !rename(temporary.fileSystemRepresentation, PIPELINE_ARCHIVE_PATH.fileSystemRepresentation)) {
            PIPELINE_ARCHIVE_DIRTY = NO;
            fprintf(stderr, "PIPELINE_CACHE saved\n");
        } else {
            [NSFileManager.defaultManager removeItemAtPath:temporary error:nil];
            fprintf(stderr, "PIPELINE_CACHE save_failed code=%ld errno=%d\n", (long)error.code, errno);
        }
    }
    pthread_mutex_unlock(&PIPELINE_CACHE_MUTEX);
}

static void initializePipelineCache(id<MTLDevice> device) {
    static dispatch_once_t INIT_TOKEN;
    dispatch_once(&INIT_TOKEN, ^{
        const char *SETTING = getenv("ZELDA_PIPELINE_CACHE");
        if (SETTING && !atoi(SETTING)) return;
#if TARGET_OS_IPHONE
        const char *CACHE_CONTRACT = IOS_SHADER_IR_CONTRACT;
#else
        const char *CACHE_CONTRACT = ZELDA_SHADER_IR_CONTRACT;
#endif
        NSString *identity = [NSString stringWithFormat:@"%@|%@|%llu|%s|%s", device.name,
            NSProcessInfo.processInfo.operatingSystemVersionString, (unsigned long long)device.registryID,
            DMSH_CHAIN, CACHE_CONTRACT];
        NSData *identityData = [identity dataUsingEncoding:NSUTF8StringEncoding];
        unsigned char digest[CC_SHA256_DIGEST_LENGTH];
        CC_SHA256(identityData.bytes, (CC_LONG)identityData.length, digest);
        NSMutableString *key = [NSMutableString string];
        for (unsigned i = 0; i < sizeof digest; i++) [key appendFormat:@"%02x", digest[i]];
#if TARGET_OS_IPHONE
        NSString *documents = NSSearchPathForDirectoriesInDomains(NSDocumentDirectory, NSUserDomainMask, YES).firstObject;
        NSString *directory = [documents stringByAppendingPathComponent:@"work/shaders/pipeline_cache"];
#else
        const char *SHADER_ROOT = getenv("DM_MSL_DIR");
        NSString *directory = [(SHADER_ROOT && *SHADER_ROOT ? @(SHADER_ROOT) : @"work/shaders") stringByAppendingPathComponent:@"pipeline_cache"];
#endif
        NSError *error = nil;
        if (![NSFileManager.defaultManager createDirectoryAtPath:directory withIntermediateDirectories:YES attributes:nil error:&error]) {
            fprintf(stderr, "PIPELINE_CACHE directory_failed code=%ld\n", (long)error.code); return;
        }
        [[NSURL fileURLWithPath:directory] setResourceValue:@YES forKey:NSURLIsExcludedFromBackupKey error:nil];
        PIPELINE_ARCHIVE_PATH = [directory stringByAppendingPathComponent:[key stringByAppendingString:@".metallib"]];
        MTLBinaryArchiveDescriptor *descriptor = [MTLBinaryArchiveDescriptor new];
        BOOL canLoad = [NSFileManager.defaultManager fileExistsAtPath:PIPELINE_ARCHIVE_PATH];
#if !TARGET_OS_IPHONE
        // Metal 3 on macOS 15 expands a large serialized archive to gigabytes
        // while loading it. Reject it before entering the synchronous driver API.
        if (canLoad && !pipelineArchiveWithinBudget(PIPELINE_ARCHIVE_PATH)) canLoad = NO;
#endif
        if (canLoad)
            descriptor.url = [NSURL fileURLWithPath:PIPELINE_ARCHIVE_PATH];
        PIPELINE_READ_ARCHIVE = descriptor.url ? [device newBinaryArchiveWithDescriptor:descriptor error:&error] : nil;
        // The read archive is immutable while worker threads create pipelines. Capture into a separate archive.
        PIPELINE_WRITE_ARCHIVE = [device newBinaryArchiveWithDescriptor:descriptor error:&error];
        if (!PIPELINE_WRITE_ARCHIVE) {
            descriptor.url = nil;
            PIPELINE_WRITE_ARCHIVE = [device newBinaryArchiveWithDescriptor:descriptor error:&error];
        }
        PIPELINE_CACHE_QUEUE = dispatch_queue_create("local.zelda.pipeline-cache", dispatch_queue_attr_make_with_qos_class(DISPATCH_QUEUE_SERIAL, QOS_CLASS_UTILITY, 0));
        fprintf(stderr, "PIPELINE_CACHE load=%s capture=%s\n", PIPELINE_READ_ARCHIVE ? "hit" : "cold",
            PIPELINE_WRITE_ARCHIVE ? "enabled" : "disabled");
    });
}

static void capturePipeline(id descriptor, BOOL compute) {
    if (!PIPELINE_WRITE_ARCHIVE) return;
#if !TARGET_OS_IPHONE
    // Bound queued descriptors as well as archive size. Otherwise a normal
    // 5,000-pipeline startup creates another archive that stalls the next boot.
    unsigned requests = atomic_load(&PIPELINE_CAPTURE_REQUESTS);
    while (requests < DESKTOP_PIPELINE_CAPTURE_LIMIT &&
        !atomic_compare_exchange_weak(&PIPELINE_CAPTURE_REQUESTS, &requests, requests + 1)) {}
    if (requests >= DESKTOP_PIPELINE_CAPTURE_LIMIT) return;
#endif
    // Archive capture can compile again; keep it off the draw thread.
    descriptor = [descriptor copy];
    dispatch_async(PIPELINE_CACHE_QUEUE, ^{ @autoreleasepool {
    pthread_mutex_lock(&PIPELINE_CACHE_MUTEX);
    NSError *error = nil;
    BOOL captured = compute ? [PIPELINE_WRITE_ARCHIVE addComputePipelineFunctionsWithDescriptor:descriptor error:&error] :
        [PIPELINE_WRITE_ARCHIVE addRenderPipelineFunctionsWithDescriptor:descriptor error:&error];
    if (captured) {
        PIPELINE_ARCHIVE_DIRTY = YES;
        if (!PIPELINE_FLUSH_SCHEDULED) {
            PIPELINE_FLUSH_SCHEDULED = YES;
            dispatch_after(dispatch_time(DISPATCH_TIME_NOW, 2 * NSEC_PER_SEC), PIPELINE_CACHE_QUEUE, ^{ flushPipelineCache(); });
        }
    } else fprintf(stderr, "PIPELINE_CACHE capture_failed code=%ld\n", (long)error.code);
    pthread_mutex_unlock(&PIPELINE_CACHE_MUTEX);
    } });
}

static inline id<MTLRenderPipelineState> cachedRenderPipeline(id<MTLDevice> device, MTLRenderPipelineDescriptor *descriptor, NSError **error) {
    initializePipelineCache(device);
    MTLRenderPipelineDescriptor *cached = [descriptor copy];
    if (PIPELINE_READ_ARCHIVE) {
        cached.binaryArchives = [cached.binaryArchives ?: @[] arrayByAddingObject:PIPELINE_READ_ARCHIVE];
        id<MTLRenderPipelineState> hit = [device newRenderPipelineStateWithDescriptor:cached
            options:MTLPipelineOptionFailOnBinaryArchiveMiss reflection:nil error:nil];
        if (hit) { notePipelineLookup(YES); return hit; }
    }
    notePipelineLookup(NO);
    id<MTLRenderPipelineState> pipeline = [device newRenderPipelineStateWithDescriptor:descriptor error:error];
    if (pipeline) capturePipeline(descriptor, NO);
    return pipeline;
}

static inline id<MTLComputePipelineState> cachedComputePipeline(id<MTLDevice> device, id<MTLFunction> function, NSError **error) {
    initializePipelineCache(device);
    MTLComputePipelineDescriptor *descriptor = [MTLComputePipelineDescriptor new];
    descriptor.computeFunction = function;
    if (PIPELINE_READ_ARCHIVE) {
        descriptor.binaryArchives = @[PIPELINE_READ_ARCHIVE];
        id<MTLComputePipelineState> hit = [device newComputePipelineStateWithDescriptor:descriptor
            options:MTLPipelineOptionFailOnBinaryArchiveMiss reflection:nil error:nil];
        if (hit) { notePipelineLookup(YES); return hit; }
        descriptor.binaryArchives = @[];
    }
    notePipelineLookup(NO);
    id<MTLComputePipelineState> pipeline = [device newComputePipelineStateWithFunction:function error:error];
    if (pipeline) capturePipeline(descriptor, YES);
    return pipeline;
}
