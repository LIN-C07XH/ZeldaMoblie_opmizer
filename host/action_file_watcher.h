#pragma once
#include <stdbool.h>

#if defined(__BLOCKS__)
typedef void (^FileChangedCallback)(const char *filePath);
#else
typedef void (*FileChangedCallback)(const char *filePath);
#endif

/// 启动基于操作系统内核 VNODE 的文件监控（0 轮询开销，文件写入保存时内核异步通知）
/// @param filePath 要监控的文件绝对路径
/// @param callback 文件被修改写入时的回调函数
/// @return 启动成功返回 true
bool fileWatcherStart(const char *filePath, FileChangedCallback callback);

/// 停止指定文件的内核监控
void fileWatcherStop(const char *filePath);
