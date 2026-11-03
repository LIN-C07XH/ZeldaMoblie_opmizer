#import <Foundation/Foundation.h>
#import "cloud_save_sync.h"
#include <assert.h>
#include <fcntl.h>
#include <sys/file.h>
#include <unistd.h>

static NSFileManager *fm;
static void writeText(NSString *root, NSString *name, NSString *value) {
    assert([value writeToFile:[root stringByAppendingPathComponent:name] atomically:YES encoding:NSUTF8StringEncoding error:nil]);
}
static NSString *readText(NSString *root, NSString *name) {
    return [NSString stringWithContentsOfFile:[root stringByAppendingPathComponent:name] encoding:NSUTF8StringEncoding error:nil];
}
static BOOL push(void) {
    __block BOOL done=NO, success=NO;
    cloudSaveSyncPushAsync(^(BOOL ok){ assert(NSThread.isMainThread); success=ok; done=YES; });
    NSDate *deadline=[NSDate dateWithTimeIntervalSinceNow:5];
    while (!done && deadline.timeIntervalSinceNow>0) [NSRunLoop.currentRunLoop runUntilDate:[NSDate dateWithTimeIntervalSinceNow:.005]];
    assert(done); return success;
}
static void useLocal(NSString *path) {
    cloudSaveSyncInit(path);
    cloudSaveSyncSetSnapshotProvider(^BOOL(NSString *destination) {
        return [fm copyItemAtPath:path toPath:destination error:nil];
    });
}
int main(void) { @autoreleasepool {
    fm=NSFileManager.defaultManager;
    NSString *root=[NSTemporaryDirectory() stringByAppendingPathComponent:NSUUID.UUID.UUIDString];
    assert([fm createDirectoryAtPath:root withIntermediateDirectories:YES attributes:nil error:nil]);
    NSUserDefaults *defaults=NSUserDefaults.standardUserDefaults;
    id oldBookmark=[defaults objectForKey:CLOUD_SAVE_BOOKMARK_KEY], oldState=[defaults objectForKey:CLOUD_SAVE_STATE_KEY];
    @try {
        cloudSaveSyncClearBookmark(); [defaults removeObjectForKey:CLOUD_SAVE_STATE_KEY];
        NSString *local=[root stringByAppendingPathComponent:@"local"], *remote=[root stringByAppendingPathComponent:@"remote"],
            *other=[root stringByAppendingPathComponent:@"other"];
        for(NSString *path in @[local,remote,other]) assert([fm createDirectoryAtPath:path withIntermediateDirectories:YES attributes:nil error:nil]);
        useLocal(local);
        assert(!cloudSaveSyncSetFolderURL([NSURL fileURLWithPath:local]));
        assert(!cloudSaveSyncSetFolderURL([NSURL fileURLWithPath:root]));
        NSString *nested = [local stringByAppendingPathComponent:@"nested"];
        assert([fm createDirectoryAtPath:nested withIntermediateDirectories:NO attributes:nil error:nil]);
        assert(!cloudSaveSyncSetFolderURL([NSURL fileURLWithPath:nested]));
        assert([fm removeItemAtPath:nested error:nil]);
        assert(cloudSaveSyncPullBlocking(1,nil)==CloudSaveSyncStatusSkipped);
        assert(cloudSaveSyncSetFolderURL([NSURL fileURLWithPath:remote]));
        writeText(local,@"progress.sav",@"one"); writeText(local,@"removed.sav",@"old");
        assert(push());
        writeText(local,@"progress.sav",@"two"); assert([fm removeItemAtPath:[local stringByAppendingPathComponent:@"removed.sav"] error:nil]);
        assert(push()); assert(push()); // Unchanged content does not create a new revision.
        assert([readText(remote,CLOUD_SAVE_META_FILE) containsString:@"\"save_version\":2"]);
        useLocal(other);
        assert(cloudSaveSyncPullBlocking(2,nil)==CloudSaveSyncStatusSuccess);
        assert([readText(other,@"progress.sav") isEqual:@"two"] && !readText(other,@"removed.sav"));
        writeText(other,@"progress.sav",@"three"); assert(push());
        useLocal(local);
        assert(cloudSaveSyncPullBlocking(2,nil)==CloudSaveSyncStatusSuccess);
        assert([readText(local,@"progress.sav") isEqual:@"three"]);
        NSString *backups=[root stringByAppendingPathComponent:@"cloud-save-backups"];
        assert([fm contentsOfDirectoryAtPath:backups error:nil].count==2);
        useLocal(other); writeText(other,@"progress.sav",@"four"); assert(push());
        useLocal(local); writeText(local,@"progress.sav",@"local-conflict");
        assert(cloudSaveSyncPullBlocking(2,nil)==CloudSaveSyncStatusConflict);
        assert([readText(local,@"progress.sav") isEqual:@"local-conflict"] && !push());
        NSString *meta=readText(remote,CLOUD_SAVE_META_FILE);
        writeText(remote,CLOUD_SAVE_META_FILE,@"[]");
        assert(cloudSaveSyncPullBlocking(2,nil)==CloudSaveSyncStatusFailed && !push());
        writeText(remote,CLOUD_SAVE_META_FILE,@"{\"active_slot\":\"../outside\",\"save_version\":9}");
        assert(cloudSaveSyncPullBlocking(2,nil)==CloudSaveSyncStatusFailed);
        writeText(remote,CLOUD_SAVE_META_FILE,meta);
        // A mounted game holds save.lock: a pull cannot publish into it.
        NSString *empty=[root stringByAppendingPathComponent:@"empty"];
        assert([fm createDirectoryAtPath:empty withIntermediateDirectories:YES attributes:nil error:nil]);
        useLocal(empty);
        int lock=open([[empty stringByAppendingString:@".lock"] fileSystemRepresentation],O_RDWR|O_CREAT,0600);
        assert(lock>=0 && !flock(lock,LOCK_EX|LOCK_NB));
        assert(cloudSaveSyncPullBlocking(2,nil)==CloudSaveSyncStatusFailed);
        assert(!readText(empty,@"progress.sav")); close(lock);
        // Timeout while provider coordination is pending cannot publish later.
        dispatch_semaphore_t entered=dispatch_semaphore_create(0), release=dispatch_semaphore_create(0), finished=dispatch_semaphore_create(0);
        dispatch_async(dispatch_get_global_queue(QOS_CLASS_DEFAULT,0), ^{
            NSFileCoordinator *coordinator=[[NSFileCoordinator alloc] initWithFilePresenter:nil];
            [coordinator coordinateWritingItemAtURL:[NSURL fileURLWithPath:remote] options:0 error:nil byAccessor:^(NSURL *url) {
                (void)url; dispatch_semaphore_signal(entered); dispatch_semaphore_wait(release,DISPATCH_TIME_FOREVER);
            }];
            dispatch_semaphore_signal(finished);
        });
        assert(!dispatch_semaphore_wait(entered,dispatch_time(DISPATCH_TIME_NOW,2*NSEC_PER_SEC)));
        assert(cloudSaveSyncPullBlocking(.02,nil)==CloudSaveSyncStatusSkipped);
        double began=NSProcessInfo.processInfo.systemUptime;
        dispatch_after(dispatch_time(DISPATCH_TIME_NOW,20*NSEC_PER_MSEC),dispatch_get_global_queue(QOS_CLASS_DEFAULT,0), ^{
            cloudSaveSyncCancel();
        });
        assert(cloudSaveSyncPullBlocking(2,nil)==CloudSaveSyncStatusCancelled);
        assert(NSProcessInfo.processInfo.systemUptime-began<1);
        dispatch_semaphore_signal(release);
        assert(!dispatch_semaphore_wait(finished,dispatch_time(DISPATCH_TIME_NOW,2*NSEC_PER_SEC)));
        cloudSaveSyncClearBookmark();
        assert(cloudSaveSyncPullBlocking(2,nil)==CloudSaveSyncStatusSkipped); // Drain the cancelled worker.
        assert(!readText(empty,@"progress.sav"));
        assert(cloudSaveSyncSetFolderURL([NSURL fileURLWithPath:remote]));
        assert(cloudSaveSyncPullBlocking(2,nil)==CloudSaveSyncStatusSuccess);
        assert([readText(empty,@"progress.sav") isEqual:@"four"]);
        // Hash mismatch and provider-controlled symlinks are rejected before publish.
        NSDictionary *metadata=[NSJSONSerialization JSONObjectWithData:[meta dataUsingEncoding:NSUTF8StringEncoding] options:0 error:nil];
        NSString *slot=[remote stringByAppendingPathComponent:metadata[@"active_slot"]];
        writeText(slot,@"progress.sav",@"corrupted");
        assert(cloudSaveSyncPullBlocking(2,nil)==CloudSaveSyncStatusFailed);
        assert([readText(empty,@"progress.sav") isEqual:@"four"]);
        assert(!symlink(local.fileSystemRepresentation,[[slot stringByAppendingPathComponent:@"escape"] fileSystemRepresentation]));
        assert(cloudSaveSyncPullBlocking(2,nil)==CloudSaveSyncStatusFailed);
        puts("Cloud saves: snapshots, deletions, conflicts, metadata, mounted saves, timeouts, hashes and links passed");
    } @finally {
        cloudSaveSyncCancel();
        if(oldBookmark)[defaults setObject:oldBookmark forKey:CLOUD_SAVE_BOOKMARK_KEY];else[defaults removeObjectForKey:CLOUD_SAVE_BOOKMARK_KEY];
        if(oldState)[defaults setObject:oldState forKey:CLOUD_SAVE_STATE_KEY];else[defaults removeObjectForKey:CLOUD_SAVE_STATE_KEY];
        [fm removeItemAtPath:root error:nil];
    }
} }
