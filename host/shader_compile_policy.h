#pragma once
#include <stdlib.h>

// Leave CPU capacity for input, audio, WindowServer and the rest of the system.
static inline unsigned shaderCompileWorkers(void) {
    const char *SETTING = getenv("ZELDA_SHADER_COMPILE_JOBS");
    int workers = SETTING ? atoi(SETTING) : 2;
    return workers < 1 ? 1 : workers > 4 ? 4 : (unsigned)workers;
}

static inline NSOperationQueue *shaderCompileQueue(void) {
    static NSOperationQueue *QUEUE;
    static dispatch_once_t INIT_TOKEN;
    dispatch_once(&INIT_TOKEN, ^{
        QUEUE = [NSOperationQueue new];
        QUEUE.name = @"local.zelda.shader-compile";
        QUEUE.qualityOfService = NSQualityOfServiceUtility;
        QUEUE.maxConcurrentOperationCount = shaderCompileWorkers();
    });
    return QUEUE;
}

static inline void applyShaderWarmup(NSUInteger count, void (^work)(NSUInteger)) {
    unsigned workers = shaderCompileWorkers();
    dispatch_apply(MIN(count, workers), dispatch_get_global_queue(QOS_CLASS_UTILITY, 0), ^(size_t worker) {
        for (NSUInteger index = worker; index < count; index += workers) work(index);
    });
}
