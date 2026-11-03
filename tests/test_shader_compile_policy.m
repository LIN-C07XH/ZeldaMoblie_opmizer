#import <Foundation/Foundation.h>
#include <assert.h>
#include <stdatomic.h>
#include <unistd.h>
#include "../host/shader_compile_policy.h"

static _Atomic unsigned ACTIVE, PEAK, COMPLETED;
static void compileWork(void) {
    unsigned active = atomic_fetch_add(&ACTIVE, 1) + 1;
    unsigned peak = atomic_load(&PEAK);
    while (active > peak && !atomic_compare_exchange_weak(&PEAK, &peak, active)) {}
    usleep(2000);
    atomic_fetch_sub(&ACTIVE, 1);
    atomic_fetch_add(&COMPLETED, 1);
}

int main(int argc, char **argv) { @autoreleasepool {
    assert(argc == 2);
    unsigned expected = (unsigned)atoi(argv[1]);
    assert(shaderCompileWorkers() == expected);
    for (unsigned index = 0; index < 48; index++)
        [shaderCompileQueue() addOperationWithBlock:^{ compileWork(); }];
    [shaderCompileQueue() waitUntilAllOperationsAreFinished];
    assert(atomic_load(&COMPLETED) == 48 && atomic_load(&PEAK) <= expected && !atomic_load(&ACTIVE));
    atomic_store(&COMPLETED, 0); atomic_store(&PEAK, 0);
    applyShaderWarmup(48, ^(NSUInteger index) { assert(index < 48); compileWork(); });
    assert(atomic_load(&COMPLETED) == 48 && atomic_load(&PEAK) <= expected && !atomic_load(&ACTIVE));
    applyShaderWarmup(0, ^(NSUInteger index) { (void)index; abort(); });
    NSOperationQueue *queue = shaderCompileQueue();
    queue.suspended = YES;
    queue.maxConcurrentOperationCount = 1;
    __block unsigned sequence = 0;
    NSBlockOperation *later = [NSBlockOperation blockOperationWithBlock:^{ assert(sequence++ == 1); }];
    NSBlockOperation *needed = [NSBlockOperation blockOperationWithBlock:^{ assert(sequence++ == 0); }];
    [queue addOperation:later]; [queue addOperation:needed];
    needed.queuePriority = NSOperationQueuePriorityVeryHigh;
    queue.suspended = NO;
    [queue waitUntilAllOperationsAreFinished];
    assert(sequence == 2);
    puts("Shader compilation: bounded prediction and warm-up concurrency passed");
} }
