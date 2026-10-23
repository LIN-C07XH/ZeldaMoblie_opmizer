#pragma once
#import <Foundation/Foundation.h>

#define CLOUD_SAVE_BOOKMARK_KEY @"ZELDA_CLOUD_SAVE_BOOKMARK"
#define CLOUD_SAVE_STATE_KEY @"ZELDA_CLOUD_SAVE_STATE"
#define CLOUD_SAVE_META_FILE @"sync_meta.json"
#define CLOUD_SAVE_SLOT_A @"slot_a"
#define CLOUD_SAVE_SLOT_B @"slot_b"
#define CLOUD_SAVE_TIMEOUT_SECONDS 5.0

typedef NS_ENUM(NSInteger, CloudSaveSyncStatus) {
    CloudSaveSyncStatusIdle = 0, CloudSaveSyncStatusSyncing,
    CloudSaveSyncStatusSuccess, CloudSaveSyncStatusSkipped,
    CloudSaveSyncStatusCancelled, CloudSaveSyncStatusFailed,
    CloudSaveSyncStatusConflict
};

#ifdef __cplusplus
extern "C" {
#endif
void cloudSaveSyncInit(NSString *localSaveDirectory);
// The provider must snapshot committed saves under the filesystem commit lock.
void cloudSaveSyncSetSnapshotProvider(BOOL (^provider)(NSString *destination));
BOOL cloudSaveSyncIsConfigured(void);
// The system picker grants access; no CloudKit container or credentials are used.
BOOL cloudSaveSyncSetFolderURL(NSURL *folderURL);
void cloudSaveSyncClearBookmark(void);
NSURL *cloudSaveSyncResolveFolderURL(void);
// Public asynchronous completions and progress are delivered on the main queue.
void cloudSaveSyncPullAsync(void (^progress)(NSString *text),
                            void (^completion)(CloudSaveSyncStatus status));
// Call from a worker thread. A timeout revokes permission to publish locally.
CloudSaveSyncStatus cloudSaveSyncPullBlocking(NSTimeInterval timeoutSeconds,
                                             void (^progress)(NSString *text));
void cloudSaveSyncCancel(void);
void cloudSaveSyncPushAsync(void (^completion)(BOOL success));
#ifdef __cplusplus
}
#endif
