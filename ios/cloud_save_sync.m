#import "cloud_save_sync.h"
#import <CommonCrypto/CommonDigest.h>
#include <fcntl.h>
#include <sys/file.h>
#include <sys/stat.h>
#include <stdio.h>
#include <unistd.h>
#include <limits.h>
#include <errno.h>
#include <stdlib.h>

@interface CloudSaveOperation : NSObject
@property BOOL cancelled;
@property double deadline;
@property(copy) NSString *localPath;
@property(copy) NSData *bookmark;
@property(copy) BOOL (^snapshot)(NSString *destination);
@property(strong) dispatch_semaphore_t wakeup;
@end
@implementation CloudSaveOperation @end

static NSString *localPath;
static BOOL (^snapshotProvider)(NSString *);
static CloudSaveOperation *activeOperation;
static dispatch_queue_t queue(void) {
    static dispatch_queue_t value;
    static dispatch_once_t once;
    dispatch_once(&once, ^{ value = dispatch_queue_create("local.zelda.cloud-save", DISPATCH_QUEUE_SERIAL); });
    return value;
}
static double now(void) { return NSProcessInfo.processInfo.systemUptime; }
static BOOL valid(CloudSaveOperation *op) {
    @synchronized(op) { return !op.cancelled && now() < op.deadline; }
}
static void cancel(CloudSaveOperation *op) {
    @synchronized(op) {
        op.cancelled = YES;
        if (op.wakeup) dispatch_semaphore_signal(op.wakeup);
    }
}
void cloudSaveSyncCancel(void) {
    @synchronized(CloudSaveOperation.class) {
        cancel(activeOperation);
    }
}
void cloudSaveSyncInit(NSString *path) {
    cloudSaveSyncCancel();
    @synchronized(CloudSaveOperation.class) { localPath = [path copy]; }
}
void cloudSaveSyncSetSnapshotProvider(BOOL (^provider)(NSString *)) {
    @synchronized(CloudSaveOperation.class) { snapshotProvider = [provider copy]; }
}
BOOL cloudSaveSyncIsConfigured(void) {
    return [NSUserDefaults.standardUserDefaults dataForKey:CLOUD_SAVE_BOOKMARK_KEY].length > 0;
}
BOOL cloudSaveSyncSetFolderURL(NSURL *url) {
    if (!url.isFileURL) return NO;
    BOOL scoped = [url startAccessingSecurityScopedResource];
    NSNumber *directory;
    NSError *error;
    BOOL isDirectory = [url getResourceValue:&directory forKey:NSURLIsDirectoryKey error:&error] && directory.boolValue;
    NSData *data = isDirectory ? [url bookmarkDataWithOptions:NSURLBookmarkCreationMinimalBookmark
        includingResourceValuesForKeys:nil relativeToURL:nil error:&error] : nil;
    if (scoped) [url stopAccessingSecurityScopedResource];
    if (!data) return NO;
    cloudSaveSyncCancel();
    [NSUserDefaults.standardUserDefaults setObject:data forKey:CLOUD_SAVE_BOOKMARK_KEY];
    return YES;
}
void cloudSaveSyncClearBookmark(void) {
    cloudSaveSyncCancel();
    [NSUserDefaults.standardUserDefaults removeObjectForKey:CLOUD_SAVE_BOOKMARK_KEY];
}
static NSURL *resolve(NSData *bookmark) {
    if (!bookmark.length) return nil;
    BOOL stale;
    return [NSURL URLByResolvingBookmarkData:bookmark options:NSURLBookmarkResolutionWithoutUI
        relativeToURL:nil bookmarkDataIsStale:&stale error:nil];
}
NSURL *cloudSaveSyncResolveFolderURL(void) {
    return resolve([NSUserDefaults.standardUserDefaults dataForKey:CLOUD_SAVE_BOOKMARK_KEY]);
}
static CloudSaveOperation *operation(double timeout) {
    CloudSaveOperation *op = [CloudSaveOperation new];
    op.deadline = now() + MAX(0, timeout);
    @synchronized(CloudSaveOperation.class) {
        cancel(activeOperation);
        op.localPath = localPath; op.snapshot = snapshotProvider;
        op.bookmark = [NSUserDefaults.standardUserDefaults dataForKey:CLOUD_SAVE_BOOKMARK_KEY];
        activeOperation = op;
    }
    return op;
}
static NSString *digest(NSData *data) {
    unsigned char bytes[CC_SHA256_DIGEST_LENGTH];
    CC_SHA256(data.bytes, (CC_LONG)data.length, bytes);
    NSMutableString *result = [NSMutableString string];
    for (unsigned i = 0; i < sizeof bytes; i++) [result appendFormat:@"%02x", bytes[i]];
    return result;
}
static BOOL directory(NSString *path) {
    struct stat st;
    return !lstat(path.fileSystemRepresentation, &st) && S_ISDIR(st.st_mode);
}
// Exact tree fingerprints detect deletions and reject links, placeholders and
// special files. Never follow a provider-controlled link outside its directory.
static NSDictionary *tree(NSString *path) {
    if (!directory(path)) return nil;
    char canonical[PATH_MAX];
    if (!realpath(path.fileSystemRepresentation, canonical)) return nil;
    path = @(canonical);
    NSFileManager *fm = NSFileManager.defaultManager;
    __block BOOL failed = NO;
    NSDirectoryEnumerator *items = [fm enumeratorAtURL:[NSURL fileURLWithPath:path]
        includingPropertiesForKeys:nil options:0 errorHandler:^BOOL(NSURL *url, NSError *error) {
            (void)url; (void)error; failed = YES; return NO;
        }];
    NSMutableDictionary *files = [NSMutableDictionary dictionary];
    for (NSURL *url in items) {
        struct stat st;
        if (lstat(url.fileSystemRepresentation, &st)) { failed = YES; break; }
        if (S_ISDIR(st.st_mode)) continue;
        if (!S_ISREG(st.st_mode) || [url.lastPathComponent hasSuffix:@".icloud"]) { failed = YES; break; }
        NSData *data = [NSData dataWithContentsOfURL:url options:NSDataReadingMappedIfSafe error:nil];
        if (!data) { failed = YES; break; }
        NSString *relative = [url.path substringFromIndex:path.length + 1];
        files[relative] = digest(data);
    }
    return failed ? nil : files;
}
static NSString *treeDigest(NSDictionary *files) {
    NSData *data = [NSJSONSerialization dataWithJSONObject:files options:NSJSONWritingSortedKeys error:nil];
    return data ? digest(data) : nil;
}
static NSDictionary *metadata(NSString *root, BOOL *missing) {
    NSString *path = [root stringByAppendingPathComponent:CLOUD_SAVE_META_FILE];
    struct stat st;
    int status = lstat(path.fileSystemRepresentation, &st);
    *missing = status && errno == ENOENT;
    if (status || !S_ISREG(st.st_mode)) return nil;
    NSData *data = [NSData dataWithContentsOfFile:path];
    id value = data ? [NSJSONSerialization JSONObjectWithData:data options:0 error:nil] : nil;
    if (![value isKindOfClass:NSDictionary.class]) return nil;
    id slot = value[@"active_slot"], version = value[@"save_version"];
    if ((![slot isEqual:CLOUD_SAVE_SLOT_A] && ![slot isEqual:CLOUD_SAVE_SLOT_B]) ||
        ![version isKindOfClass:NSNumber.class] || [version longLongValue] < 1 ||
        [version longLongValue] == LLONG_MAX) return nil;
    id files = value[@"files"];
    if (files && ![files isKindOfClass:NSDictionary.class]) return nil;
    return value;
}
static NSString *revision(NSDictionary *meta) {
    // Legacy revision identification does not trust wall-clock ordering.
    return [NSString stringWithFormat:@"%@:%@:%@", meta[@"save_version"], meta[@"active_slot"], meta[@"id"] ?: meta[@"timestamp"] ?: @0];
}
static NSString *stateKey(NSURL *url, CloudSaveOperation *op) {
    return digest([[url.absoluteString stringByAppendingString:op.localPath] dataUsingEncoding:NSUTF8StringEncoding]);
}
static NSDictionary *state(NSString *key) {
    return [NSUserDefaults.standardUserDefaults dictionaryForKey:CLOUD_SAVE_STATE_KEY][key];
}
static void remember(NSString *key, NSDictionary *meta, NSDictionary *files) {
    NSMutableDictionary *all = [[NSUserDefaults.standardUserDefaults dictionaryForKey:CLOUD_SAVE_STATE_KEY] mutableCopy] ?: [NSMutableDictionary dictionary];
    all[key] = @{@"revision": revision(meta), @"digest": treeDigest(files)};
    [NSUserDefaults.standardUserDefaults setObject:all forKey:CLOUD_SAVE_STATE_KEY];
}
static NSString *temporary(NSString *parent) {
    return [parent stringByAppendingPathComponent:[@".cloud-stage-" stringByAppendingString:NSUUID.UUID.UUIDString]];
}
static BOOL copyTree(NSString *source, NSString *target) {
    NSDictionary *before = tree(source);
    if (!before) return NO;
    if (![NSFileManager.defaultManager copyItemAtPath:source toPath:target error:nil]) return NO;
    return [before isEqual:tree(target)] && [before isEqual:tree(source)];
}
static BOOL publishLocal(NSString *stage, CloudSaveOperation *op) {
    // Synchronize the timeout/cancel decision with the sole local publication.
    // The game holds this same flock for its entire mounted save lifetime.
    @synchronized(op) {
        if (!valid(op)) return NO;
        NSString *lockPath = [op.localPath stringByAppendingString:@".lock"];
        int lock = open(lockPath.fileSystemRepresentation, O_RDWR | O_CREAT | O_NOFOLLOW, 0600);
        if (lock < 0) return NO;
        BOOL ok = flock(lock, LOCK_EX | LOCK_NB) == 0;
        if (ok && directory(op.localPath)) {
            ok = renamex_np(stage.fileSystemRepresentation, op.localPath.fileSystemRepresentation, RENAME_SWAP) == 0;
            // Keep the pre-sync local save so a cloud conflict remains recoverable.
            if (ok) {
                NSString *backups = [[op.localPath stringByDeletingLastPathComponent] stringByAppendingPathComponent:@"cloud-save-backups"];
                [NSFileManager.defaultManager createDirectoryAtPath:backups withIntermediateDirectories:YES attributes:nil error:nil];
                NSString *saved = [backups stringByAppendingPathComponent:NSUUID.UUID.UUIDString];
                if (![NSFileManager.defaultManager moveItemAtPath:stage toPath:saved error:nil])
                    NSLog(@"CLOUD_SYNC previous local save retained at %@", stage.lastPathComponent);
            }
        } else if (ok) {
            ok = rename(stage.fileSystemRepresentation, op.localPath.fileSystemRepresentation) == 0;
        }
        close(lock);
        return ok;
    }
}
static CloudSaveSyncStatus pull(CloudSaveOperation *op, void (^progress)(NSString *)) {
    if (!op.bookmark.length) return CloudSaveSyncStatusSkipped;
    NSURL *url = resolve(op.bookmark);
    if (!url || !op.localPath.length) return CloudSaveSyncStatusFailed;
    BOOL scoped = [url startAccessingSecurityScopedResource];
    __block CloudSaveSyncStatus result = CloudSaveSyncStatusFailed;
    NSError *error;
    NSFileCoordinator *coordinator = [[NSFileCoordinator alloc] initWithFilePresenter:nil];
    [coordinator coordinateReadingItemAtURL:url options:0 error:&error byAccessor:^(NSURL *folder) {
        if (!valid(op)) { result = CloudSaveSyncStatusCancelled; return; }
        BOOL missing = NO;
        NSDictionary *meta = metadata(folder.path, &missing);
        if (missing) { result = CloudSaveSyncStatusSkipped; return; }
        if (!meta) return;
        NSString *source = [folder.path stringByAppendingPathComponent:meta[@"active_slot"]];
        NSString *stage = temporary([op.localPath stringByDeletingLastPathComponent]);
        NSFileManager *fm = NSFileManager.defaultManager;
        BOOL staged = copyTree(source, stage);
        NSDictionary *files = staged ? tree(stage) : nil;
        if (!files.count || (meta[@"files"] && ![files isEqual:meta[@"files"]])) {
            [fm removeItemAtPath:stage error:nil]; return;
        }
        NSDictionary *local = tree(op.localPath);
        NSString *key = stateKey(url, op);
        NSDictionary *previous = state(key);
        if ([files isEqual:local]) {
            if (valid(op)) remember(key, meta, files);
            result = CloudSaveSyncStatusSuccess;
        } else if (!local || (local.count && ![treeDigest(local) isEqual:previous[@"digest"]])) {
            result = CloudSaveSyncStatusConflict;
        } else if (valid(op)) {
            if (progress) dispatch_async(dispatch_get_main_queue(), ^{ if (valid(op)) progress(@"Applying cloud save snapshot..."); });
            if (publishLocal(stage, op)) { remember(key, meta, files); result = CloudSaveSyncStatusSuccess; }
        }
        // A successful swap consumes the staged save or retains the old save as
        // a backup. Do not delete that old save if moving the backup failed.
        if (result != CloudSaveSyncStatusSuccess || [files isEqual:local]) [fm removeItemAtPath:stage error:nil];
        if (!valid(op) && result != CloudSaveSyncStatusSuccess) result = CloudSaveSyncStatusCancelled;
    }];
    if (scoped) [url stopAccessingSecurityScopedResource];
    return error ? CloudSaveSyncStatusFailed : result;
}
static BOOL push(CloudSaveOperation *op) {
    if (!op.bookmark.length || !op.localPath.length || !op.snapshot || !valid(op)) return NO;
    NSURL *url = resolve(op.bookmark);
    if (!url) return NO;
    NSString *snapshot = temporary([op.localPath stringByDeletingLastPathComponent]);
    if (!op.snapshot(snapshot)) { [NSFileManager.defaultManager removeItemAtPath:snapshot error:nil]; return NO; }
    NSFileManager *fm = NSFileManager.defaultManager;
    NSDictionary *files = tree(snapshot);
    if (!files.count) { [fm removeItemAtPath:snapshot error:nil]; return NO; }
    BOOL scoped = [url startAccessingSecurityScopedResource];
    __block BOOL success = NO;
    NSError *error;
    NSFileCoordinator *coordinator = [[NSFileCoordinator alloc] initWithFilePresenter:nil];
    [coordinator coordinateWritingItemAtURL:url options:0 error:&error byAccessor:^(NSURL *folder) {
        if (!valid(op)) return;
        BOOL missing = NO;
        NSDictionary *meta = metadata(folder.path, &missing);
        if (!missing && !meta) return;
        NSString *key = stateKey(url, op);
        NSDictionary *previous = state(key);
        if (meta && ![previous[@"revision"] isEqual:revision(meta)]) {
            // Unseen remote changes must be pulled before replacing a head.
            NSLog(@"CLOUD_SYNC conflict: cloud changed; local and remote saves preserved"); return;
        }
        if (meta && [files isEqual:meta[@"files"]]) { remember(key, meta, files); success = YES; return; }
        NSString *slot = [meta[@"active_slot"] isEqual:CLOUD_SAVE_SLOT_A] ? CLOUD_SAVE_SLOT_B : CLOUD_SAVE_SLOT_A;
        NSString *stage = temporary(folder.path);
        NSString *target = [folder.path stringByAppendingPathComponent:slot];
        if (!copyTree(snapshot, stage) || !valid(op)) { [fm removeItemAtPath:stage error:nil]; return; }
        if ([fm fileExistsAtPath:target]) {
            if (!directory(target) || renamex_np(stage.fileSystemRepresentation, target.fileSystemRepresentation, RENAME_SWAP)) {
                [fm removeItemAtPath:stage error:nil]; return;
            }
            [fm removeItemAtPath:stage error:nil];
        } else if (rename(stage.fileSystemRepresentation, target.fileSystemRepresentation)) {
            [fm removeItemAtPath:stage error:nil]; return;
        }
        NSDictionary *next = @{@"active_slot": slot, @"save_version": @([meta[@"save_version"] longLongValue] + 1),
            @"id": NSUUID.UUID.UUIDString, @"timestamp": @(NSDate.date.timeIntervalSince1970), @"files": files};
        NSData *data = [NSJSONSerialization dataWithJSONObject:next options:NSJSONWritingSortedKeys error:nil];
        // Only a fully verified inactive slot is published; failed writes leave
        // the previous active slot and metadata intact.
        if (valid(op) && [data writeToFile:[folder.path stringByAppendingPathComponent:CLOUD_SAVE_META_FILE]
            options:NSDataWritingAtomic error:nil]) { remember(key, next, files); success = YES; }
    }];
    if (scoped) [url stopAccessingSecurityScopedResource];
    [fm removeItemAtPath:snapshot error:nil];
    return !error && success;
}
void cloudSaveSyncPullAsync(void (^progress)(NSString *), void (^completion)(CloudSaveSyncStatus)) {
    CloudSaveOperation *op = operation(CLOUD_SAVE_TIMEOUT_SECONDS);
    dispatch_async(queue(), ^{
        CloudSaveSyncStatus result = pull(op, progress);
        if (completion) dispatch_async(dispatch_get_main_queue(), ^{ completion(result); });
    });
}
CloudSaveSyncStatus cloudSaveSyncPullBlocking(NSTimeInterval timeout, void (^progress)(NSString *)) {
    CloudSaveOperation *op = operation(timeout);
    dispatch_semaphore_t done = dispatch_semaphore_create(0);
    @synchronized(op) { op.wakeup = done; if (op.cancelled) dispatch_semaphore_signal(done); }
    __block CloudSaveSyncStatus result;
    dispatch_async(queue(), ^{ result = pull(op, progress); dispatch_semaphore_signal(done); });
    if (dispatch_semaphore_wait(done, dispatch_time(DISPATCH_TIME_NOW, (int64_t)(MAX(0, timeout) * NSEC_PER_SEC)))) {
        cancel(op);
        return CloudSaveSyncStatusSkipped;
    }
    @synchronized(op) { if (op.cancelled) return CloudSaveSyncStatusCancelled; }
    return result;
}
void cloudSaveSyncPushAsync(void (^completion)(BOOL)) {
    CloudSaveOperation *op = operation(25);
    dispatch_async(queue(), ^{
        BOOL result = push(op);
        if (completion) dispatch_async(dispatch_get_main_queue(), ^{ completion(result); });
    });
}
