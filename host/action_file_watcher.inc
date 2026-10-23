#include "action_file_watcher.h"
#include <dispatch/dispatch.h>
#include <fcntl.h>
#include <unistd.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

#define MAX_WATCHERS 8

typedef struct {
    char path[1024];
    int fd;
    dispatch_source_t source;
    FileChangedCallback callback;
} WatcherEntry;

static WatcherEntry watchers[MAX_WATCHERS];
static int watcherCount = 0;

bool fileWatcherStart(const char *filePath, FileChangedCallback callback) {
    if (!filePath || !filePath[0] || !callback) return false;
    if (strlen(filePath) >= sizeof(watchers[0].path)) return false;
    fileWatcherStop(filePath);
    if (watcherCount >= MAX_WATCHERS) return false;

    // 打开文件描述符
    int fd = open(filePath, O_EVTONLY);
    if (fd < 0) {
        // 文件可能暂未创建，创建空占位
        FILE *tmp = fopen(filePath, "ab");
        if (tmp) fclose(tmp);
        fd = open(filePath, O_EVTONLY);
        if (fd < 0) return false;
    }

    // UI/config reloads and watcher mutations are serialized on the main queue.
    dispatch_queue_t queue = dispatch_get_main_queue();
    // 监听内核 VNODE 写入、属性变更、重命名及删除事件
    dispatch_source_t source = dispatch_source_create(DISPATCH_SOURCE_TYPE_VNODE,
                                                      (uintptr_t)fd,
                                                      DISPATCH_VNODE_WRITE | DISPATCH_VNODE_EXTEND |
                                                      DISPATCH_VNODE_ATTRIB | DISPATCH_VNODE_LINK |
                                                      DISPATCH_VNODE_RENAME | DISPATCH_VNODE_DELETE | DISPATCH_VNODE_REVOKE,
                                                      queue);
    if (!source) {
        close(fd);
        return false;
    }

    int idx = watcherCount++;
    strncpy(watchers[idx].path, filePath, sizeof(watchers[idx].path) - 1);
    watchers[idx].fd = fd;
    watchers[idx].source = source;
#if defined(__BLOCKS__)
    watchers[idx].callback = [callback copy];
#else
    watchers[idx].callback = callback;
#endif

    char *savedPath = strdup(filePath);

    dispatch_source_set_event_handler(source, ^{
        if (dispatch_source_testcancel(source)) return;
        unsigned long flags = dispatch_source_get_data(source);
        if (flags & (DISPATCH_VNODE_DELETE | DISPATCH_VNODE_RENAME | DISPATCH_VNODE_REVOKE)) {
            fileWatcherStop(savedPath);
            fileWatcherStart(savedPath, callback);
        } else if (flags & (DISPATCH_VNODE_WRITE | DISPATCH_VNODE_EXTEND | DISPATCH_VNODE_ATTRIB)) {
            // 内核通知文件发生修改
            callback(savedPath);
        }
    });

    dispatch_source_set_cancel_handler(source, ^{
        close(fd);
        if (savedPath) free(savedPath);
    });

    dispatch_resume(source);
    callback(filePath); // Load existing settings, including an atomic replacement.
    return true;
}

void fileWatcherStop(const char *filePath) {
    if (!filePath) return;
    for (int i = 0; i < watcherCount; i++) {
        if (strcmp(watchers[i].path, filePath) == 0) {
            if (watchers[i].source) {
                dispatch_source_cancel(watchers[i].source);
            }
            // 移位清除
            for (int j = i; j < watcherCount - 1; j++) {
                watchers[j] = watchers[j + 1];
            }
            watcherCount--;
            watchers[watcherCount].source = nil;
            watchers[watcherCount].callback = nil;
            watchers[watcherCount].path[0] = '\0';
            watchers[watcherCount].fd = -1;
            break;
        }
    }
}
