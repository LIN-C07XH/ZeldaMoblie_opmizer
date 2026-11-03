#import <Cocoa/Cocoa.h>
#include <assert.h>
#include <stdatomic.h>
#include <unistd.h>
#include "../ios/cloud_save_sync.h"
#include "../host/filesystem.h"

static BOOL CONFIGURED;
static _Atomic BOOL PULL_FINISHED;
static unsigned UPLOADS, CANCELS;
static void (^PUSH_COMPLETION)(BOOL);
static NSString *LOCAL_PATH;
void cloudSaveSyncInit(NSString *path) { LOCAL_PATH = path; }
void cloudSaveSyncSetSnapshotProvider(BOOL (^provider)(NSString *)) { assert(provider); }
BOOL cloudSaveSyncIsConfigured(void) { return CONFIGURED; }
BOOL cloudSaveSyncSetFolderURL(NSURL *url) { (void)url; return YES; }
void cloudSaveSyncClearBookmark(void) { CONFIGURED = NO; }
void cloudSaveSyncCancel(void) { CANCELS++; }
uint32_t filesystem_snapshot_save(const char *destination) { (void)destination; return 0; }
CloudSaveSyncStatus cloudSaveSyncPullBlocking(NSTimeInterval timeout, void (^progress)(NSString *)) {
    (void)progress;
    assert(!NSThread.isMainThread && timeout == CLOUD_SAVE_TIMEOUT_SECONDS);
    usleep(5000); atomic_store(&PULL_FINISHED, YES);
    return CloudSaveSyncStatusConflict;
}
void cloudSaveSyncPullAsync(void (^progress)(NSString *), void (^completion)(CloudSaveSyncStatus)) {
    dispatch_async(dispatch_get_global_queue(QOS_CLASS_UTILITY, 0), ^{
        CloudSaveSyncStatus result = cloudSaveSyncPullBlocking(CLOUD_SAVE_TIMEOUT_SECONDS, progress);
        dispatch_async(dispatch_get_main_queue(), ^{ completion(result); });
    });
}
void cloudSaveSyncPushAsync(void (^completion)(BOOL)) { UPLOADS++; PUSH_COMPLETION = [completion copy]; }
#include "../host/mac_cloud_save.inc"

int main(void) { @autoreleasepool {
    MacCloudSaveController *controller = [MacCloudSaveController new];
    assert([LOCAL_PATH hasSuffix:@"/work/save"]);
    controller.statusItem = [[NSMenuItem alloc] initWithTitle:@"" action:nil keyEquivalent:@""];
    __block BOOL started = NO;
    [controller startAfterPull:^{ started = YES; }];
    assert(started && !controller.waitingForPull);
    CONFIGURED = YES; started = NO;
    [controller startAfterPull:^{ assert(NSThread.isMainThread && atomic_load(&PULL_FINISHED)); started = YES; }];
    assert(!started && controller.waitingForPull);
    [controller uploadSave:nil]; assert(!UPLOADS);
    NSDate *deadline = [NSDate dateWithTimeIntervalSinceNow:2];
    while (!started && deadline.timeIntervalSinceNow > 0)
        [NSRunLoop.currentRunLoop runUntilDate:[NSDate dateWithTimeIntervalSinceNow:.005]];
    assert(started && !controller.waitingForPull);
    assert([controller.statusItem.title containsString:@"冲突"]);
    started = NO;
    [controller startAfterPull:^{ started = YES; }];
    [controller cancelPull:nil];
    assert(started && !controller.waitingForPull && [controller.statusItem.title containsString:@"已取消"]);
    controller.guestStarted = YES;
    [controller uploadSave:nil]; [controller uploadSave:nil]; assert(UPLOADS == 1);
    assert(controller.uploading);
    PUSH_COMPLETION(YES); PUSH_COMPLETION = nil;
    assert(!controller.uploading && [controller.statusItem.title containsString:@"已同步"]);
    __block unsigned exits = 0;
    [controller quitAfterUpload:^{ exits++; }];
    [controller quitAfterUpload:^{ exits++; }];
    assert(controller.quitting && UPLOADS == 2 && !exits);
    [controller uploadSave:nil]; assert(UPLOADS == 2);
    PUSH_COMPLETION(YES); PUSH_COMPLETION = nil;
    assert(exits == 1 && CANCELS >= 2);
    CONFIGURED = NO;
    controller = [MacCloudSaveController new];
    [controller quitAfterUpload:^{ exits++; }];
    assert(exits == 2 && UPLOADS == 2);
    puts("Mac cloud saves: startup ordering, conflicts, upload coalescing and deferred quit passed");
} }
