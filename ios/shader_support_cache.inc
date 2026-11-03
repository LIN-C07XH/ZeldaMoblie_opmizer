#import <Foundation/Foundation.h>
#import <CommonCrypto/CommonDigest.h>
#include <sys/stat.h>
#include <zlib.h>

static NSString *shaderSupportDigest(NSData *data) {
    unsigned char digest[CC_SHA256_DIGEST_LENGTH];
    CC_SHA256(data.bytes, (CC_LONG)data.length, digest);
    NSMutableString *result = [NSMutableString stringWithCapacity:64];
    for (unsigned i = 0; i < sizeof digest; i++) [result appendFormat:@"%02x", digest[i]];
    return result;
}

static NSDictionary *shaderSupportStamp(NSString *directory, NSString *manifestHash) {
    struct stat info;
    if (stat(directory.fileSystemRepresentation, &info) || !S_ISDIR(info.st_mode)) return nil;
    return @{@"version": @1, @"manifest_sha256": manifestHash, @"inode": @((uint64_t)info.st_ino),
        @"modified_seconds": @(info.st_mtimespec.tv_sec), @"modified_ns": @(info.st_mtimespec.tv_nsec),
        @"changed_seconds": @(info.st_ctimespec.tv_sec), @"changed_ns": @(info.st_ctimespec.tv_nsec)};
}

static BOOL shaderSupportFailure(NSError * __strong *error, NSString *message) {
    if (error) *error = [NSError errorWithDomain:@"ShaderSupport" code:1
        userInfo:@{NSLocalizedDescriptionKey: message}];
    return NO;
}

static BOOL prepareIosShaderSupport(NSString *supportRoot, NSString *directory, NSString *chain, NSError * __strong *error) {
    NSError *fileError = nil;
    NSFileManager *fileManager = NSFileManager.defaultManager;
    NSData *manifestData = [NSData dataWithContentsOfFile:[supportRoot stringByAppendingPathComponent:@"manifest.json"]];
    id manifest = manifestData ? [NSJSONSerialization JSONObjectWithData:manifestData options:0 error:nil] : nil;
    if (![manifest isKindOfClass:NSDictionary.class] ||
        (![@[@1, @2] containsObject:manifest[@"version"]]) ||
        ![manifest[@"chain"] isEqual:chain] || ![manifest[@"files"] isKindOfClass:NSArray.class] ||
        ![manifest[@"files"] count]) return shaderSupportFailure(error, @"图形支持资源清单无效，请重新构建应用。");
    NSString *manifestHash = shaderSupportDigest(manifestData);
    NSString *markerPath = [directory stringByAppendingString:@".verified.json"];
    NSDictionary *stamp = shaderSupportStamp(directory, manifestHash);
    NSData *markerData = [NSData dataWithContentsOfFile:markerPath];
    id marker = markerData ? [NSJSONSerialization JSONObjectWithData:markerData options:0 error:nil] : nil;
    // These are app-owned source copies. Directory changes or a new signed manifest
    // invalidate this receipt. Never record completion until every file was verified.
    if (stamp && [marker isKindOfClass:NSDictionary.class] && [marker isEqual:stamp]) {
        fprintf(stderr, "IOS_MOVIE_SHADER cache_hit chain=%s files=%lu\n", chain.UTF8String,
            (unsigned long)[manifest[@"files"] count]);
        return YES;
    }
    if (![fileManager createDirectoryAtPath:directory withIntermediateDirectories:YES attributes:nil error:&fileError])
        return shaderSupportFailure(error, fileError.localizedDescription);
    NSString *support = [supportRoot stringByAppendingPathComponent:[@"b-" stringByAppendingString:chain]];
    NSData *packed = nil;
    if ([manifest[@"version"] isEqual:@2]) {
        packed = [NSData dataWithContentsOfFile:[supportRoot stringByAppendingPathComponent:@"sources.pack"]
            options:NSDataReadingMappedIfSafe error:&fileError];
        if (!packed) return shaderSupportFailure(error, fileError.localizedDescription);
    }
    for (id entry in manifest[@"files"]) { @autoreleasepool {
        if (![entry isKindOfClass:NSDictionary.class]) return shaderSupportFailure(error, @"图形资源条目无效。");
        NSString *file = entry[@"name"];
        if (![file isKindOfClass:NSString.class] || !file.length ||
            ![file.lastPathComponent isEqual:file] || [@[@".", @".."] containsObject:file] ||
            ![entry[@"size"] isKindOfClass:NSNumber.class] || ![entry[@"sha256"] isKindOfClass:NSString.class])
            return shaderSupportFailure(error, @"图形资源条目无效。");
        NSData *source = nil;
        if (packed) {
            if (![entry[@"offset"] isKindOfClass:NSNumber.class] ||
                ![entry[@"compressed_size"] isKindOfClass:NSNumber.class])
                return shaderSupportFailure(error, @"图形资源压缩条目无效。");
            uint64_t offset = [entry[@"offset"] unsignedLongLongValue];
            uint64_t compressedSize = [entry[@"compressed_size"] unsignedLongLongValue];
            uint64_t expectedSize = [entry[@"size"] unsignedLongLongValue];
            if (!expectedSize || expectedSize > (64u << 20) || !compressedSize ||
                offset > packed.length || compressedSize > packed.length - offset)
                return shaderSupportFailure(error, @"图形资源压缩范围无效。");
            NSMutableData *decoded = [NSMutableData dataWithLength:(NSUInteger)expectedSize];
            uLongf decodedSize = (uLongf)expectedSize;
            int result = uncompress(decoded.mutableBytes, &decodedSize,
                (const Bytef *)packed.bytes + offset, (uLong)compressedSize);
            if (result != Z_OK || decodedSize != expectedSize)
                return shaderSupportFailure(error, @"图形资源解压失败。");
            source = decoded;
        } else source = [NSData dataWithContentsOfFile:[support stringByAppendingPathComponent:file]];
        if (!source.length || source.length != [entry[@"size"] unsignedLongLongValue] ||
            ![shaderSupportDigest(source) isEqual:entry[@"sha256"]])
            return shaderSupportFailure(error, [@"包内图形资源损坏：" stringByAppendingString:file]);
        NSString *destination = [directory stringByAppendingPathComponent:file];
        if (![source isEqualToData:[NSData dataWithContentsOfFile:destination]] &&
            ![source writeToFile:destination options:NSDataWritingAtomic error:&fileError])
            return shaderSupportFailure(error, fileError.localizedDescription);
    } }
    stamp = shaderSupportStamp(directory, manifestHash);
    NSData *receipt = stamp ? [NSJSONSerialization dataWithJSONObject:stamp options:0 error:&fileError] : nil;
    if (!receipt || ![receipt writeToFile:markerPath options:NSDataWritingAtomic error:&fileError])
        return shaderSupportFailure(error, fileError.localizedDescription ?: @"无法保存图形资源校验记录。");
    fprintf(stderr, "IOS_MOVIE_SHADER verified chain=%s files=%lu\n", chain.UTF8String,
        (unsigned long)[manifest[@"files"] count]);
    return YES;
}
