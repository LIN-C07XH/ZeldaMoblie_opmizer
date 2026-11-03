// AppKit lifecycle adapter; the snapshot protocol is shared with iPhone/iPad.
#import "../ios/cloud_save_sync.h"
#include "filesystem.h"

@interface MacCloudSaveController : NSObject <NSMenuItemValidation>
@property(weak) NSWindow *window;
@property(strong) NSMenuItem *statusItem;
@property BOOL waitingForPull;
@property BOOL uploading;
@property BOOL guestStarted;
@property BOOL quitting;
@property(copy) void (^pullCompletion)(CloudSaveSyncStatus status);
- (void)startAfterPull:(void (^)(void))completion;
- (void)uploadSave:(id)sender;
- (void)linkFolder:(id)sender;
- (void)unlinkFolder:(id)sender;
- (void)cancelPull:(id)sender;
- (void)quitAfterUpload:(void (^)(void))completion;
@end

static MacCloudSaveController *MAC_CLOUD_SAVE;

@implementation MacCloudSaveController
- (instancetype)init {
    if ((self = [super init])) {
        NSString *savePath = [NSFileManager.defaultManager.currentDirectoryPath stringByAppendingPathComponent:@"work/save"];
        cloudSaveSyncInit(savePath);
        cloudSaveSyncSetSnapshotProvider(^BOOL(NSString *destination) {
            return filesystem_snapshot_save(destination.fileSystemRepresentation) == 0;
        });
    }
    return self;
}
- (void)setSyncMessage:(NSString *)message {
    self.statusItem.title = [@"云存档：" stringByAppendingString:message];
    NSLog(@"CLOUD_SYNC mac %@", message);
}
- (void)startAfterPull:(void (^)(void))completion {
    if (!cloudSaveSyncIsConfigured()) { completion(); return; }
    self.waitingForPull = YES;
    NSString *title = self.window.title;
    self.window.title = @"Zelda-Mobile — 正在同步云存档…";
    [self setSyncMessage:@"正在下载（可在菜单中取消）"];
    __block BOOL finished = NO;
    void (^finish)(CloudSaveSyncStatus) = ^(CloudSaveSyncStatus result) {
            if (finished) return;
            finished = YES;
            self.pullCompletion = nil;
            self.waitingForPull = NO;
            self.window.title = title;
            if (self.quitting) return;
            switch (result) {
                case CloudSaveSyncStatusSuccess: [self setSyncMessage:@"已同步"]; break;
                case CloudSaveSyncStatusConflict: [self setSyncMessage:@"存档冲突，已保留本地与云端"]; break;
                case CloudSaveSyncStatusCancelled: [self setSyncMessage:@"已取消，本次使用本地存档"]; break;
                default: [self setSyncMessage:@"未下载，本次使用本地存档"]; break;
            }
            completion();
    };
    self.pullCompletion = finish;
    // Create the operation on the main thread before cancellation can be selected.
    cloudSaveSyncPullAsync(nil, finish);
    dispatch_after(dispatch_time(DISPATCH_TIME_NOW, (int64_t)(CLOUD_SAVE_TIMEOUT_SECONDS * NSEC_PER_SEC)),
        dispatch_get_main_queue(), ^{
            if (!finished) { cloudSaveSyncCancel(); finish(CloudSaveSyncStatusSkipped); }
        });
}
- (void)linkFolder:(id)sender {
    (void)sender;
    NSOpenPanel *panel = [NSOpenPanel openPanel];
    panel.canChooseFiles = NO;
    panel.canChooseDirectories = YES;
    panel.canCreateDirectories = YES;
    panel.allowsMultipleSelection = NO;
    panel.message = @"选择与 iPhone / iPad 相同的云存档目录。下次启动自动下载；已有不同进度会保留两边。";
    [panel beginSheetModalForWindow:self.window completionHandler:^(NSModalResponse response) {
        if (response != NSModalResponseOK) return;
        if (cloudSaveSyncSetFolderURL(panel.URL)) [self setSyncMessage:@"已关联，下次启动下载"];
        else [self setSyncMessage:@"关联失败，请选择独立的云盘目录"];
    }];
}
- (void)unlinkFolder:(id)sender {
    (void)sender;
    cloudSaveSyncClearBookmark();
    [self setSyncMessage:@"未关联（云端存档保留）"];
}
- (void)cancelPull:(id)sender {
    (void)sender;
    cloudSaveSyncCancel();
    if (self.pullCompletion) self.pullCompletion(CloudSaveSyncStatusCancelled);
}
- (void)uploadSave:(id)sender {
    (void)sender;
    if (self.uploading || self.waitingForPull || self.quitting || !self.guestStarted || !cloudSaveSyncIsConfigured()) return;
    self.uploading = YES;
    [self setSyncMessage:@"正在上传最后一次保存"];
    cloudSaveSyncPushAsync(^(BOOL success) {
        self.uploading = NO;
        if (!self.quitting) [self setSyncMessage:success ? @"已同步" : @"上传未完成，请检查云盘或存档冲突"];
    });
}
- (void)quitAfterUpload:(void (^)(void))completion {
    if (self.quitting) return;
    self.quitting = YES;
    [self cancelPull:nil];
    if (!self.guestStarted || !cloudSaveSyncIsConfigured()) { completion(); return; }
    self.window.title = @"Zelda-Mobile — 正在上传已保存进度…";
    [self setSyncMessage:@"退出前上传"];
    __block BOOL finished = NO;
    void (^finish)(void) = ^{
        if (finished) return;
        finished = YES;
        cloudSaveSyncCancel();
        completion();
    };
    cloudSaveSyncPushAsync(^(BOOL success) {
        [self setSyncMessage:success ? @"已同步" : @"上传未完成，本地存档保留"];
        finish();
    });
    dispatch_after(dispatch_time(DISPATCH_TIME_NOW, (int64_t)(CLOUD_SAVE_TIMEOUT_SECONDS * NSEC_PER_SEC)),
        dispatch_get_main_queue(), ^{ if (!finished) { [self setSyncMessage:@"上传超时，本地存档保留"]; finish(); } });
}
- (BOOL)validateMenuItem:(NSMenuItem *)item {
    if (self.quitting) return NO;
    if (item.action == @selector(cancelPull:)) return self.waitingForPull;
    if (self.waitingForPull || self.uploading) return NO;
    if (item.action == @selector(uploadSave:)) return self.guestStarted && cloudSaveSyncIsConfigured();
    if (item.action == @selector(unlinkFolder:)) return cloudSaveSyncIsConfigured();
    return item.action == @selector(linkFolder:);
}
@end
