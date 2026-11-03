#import <Foundation/Foundation.h>
#include <assert.h>
#include "../ios/shader_support_cache.inc"
#include "../host/nvn_upload_lifetime.inc"

static void writeManifest(NSString *support, NSData *source) {
    NSDictionary *manifest = @{@"version": @1, @"chain": @"test", @"files": @[
        @{@"name": @"test.metallib", @"size": @(source.length), @"sha256": shaderSupportDigest(source)}]};
    assert([[NSJSONSerialization dataWithJSONObject:manifest options:0 error:nil]
        writeToFile:[support stringByAppendingPathComponent:@"manifest.json"] atomically:YES]);
}

int main(void) { @autoreleasepool {
    NSString *root = [NSTemporaryDirectory() stringByAppendingPathComponent:NSUUID.UUID.UUIDString];
    NSString *support = [root stringByAppendingPathComponent:@"support"];
    NSString *directory = [root stringByAppendingPathComponent:@"b-test"];
    NSString *bundle = [support stringByAppendingPathComponent:@"b-test"];
    NSFileManager *fileManager = NSFileManager.defaultManager;
    assert([fileManager createDirectoryAtPath:bundle withIntermediateDirectories:YES attributes:nil error:nil]);
    NSString *sourcePath = [bundle stringByAppendingPathComponent:@"test.metallib"];
    NSString *targetPath = [directory stringByAppendingPathComponent:@"test.metallib"];
    NSString *markerPath = [directory stringByAppendingString:@".verified.json"];
    NSData *source = [@"shader version one" dataUsingEncoding:NSUTF8StringEncoding];
    assert([source writeToFile:sourcePath atomically:YES]); writeManifest(support, source);
    NSError *error = nil;
    assert(prepareIosShaderSupport(support, directory, @"test", &error));
    assert([[NSData dataWithContentsOfFile:targetPath] isEqual:source]);
    // A hot launch must not read every signed source file again.
    assert([fileManager removeItemAtPath:sourcePath error:nil]);
    assert(prepareIosShaderSupport(support, directory, @"test", &error));
    assert([source writeToFile:sourcePath atomically:YES]);
    // Deleted output invalidates the directory stamp and is repaired.
    assert([fileManager removeItemAtPath:targetPath error:nil]);
    assert(prepareIosShaderSupport(support, directory, @"test", &error));
    assert([[NSData dataWithContentsOfFile:targetPath] isEqual:source]);
    // A new manifest invalidates the receipt even when the destination remains present.
    source = [@"shader version two" dataUsingEncoding:NSUTF8StringEncoding];
    assert([source writeToFile:sourcePath atomically:YES]); writeManifest(support, source);
    assert(prepareIosShaderSupport(support, directory, @"test", &error));
    assert([[NSData dataWithContentsOfFile:targetPath] isEqual:source]);
    // A broken bundle or interrupted verification cannot produce a success receipt.
    assert([fileManager removeItemAtPath:markerPath error:nil]);
    assert([[@"broken" dataUsingEncoding:NSUTF8StringEncoding] writeToFile:sourcePath atomically:YES]);
    assert(!prepareIosShaderSupport(support, directory, @"test", &error) && error);
    assert(![fileManager fileExistsAtPath:markerPath]);
    NSMutableData *compressed = [NSMutableData dataWithLength:compressBound(source.length)];
    uLongf compressedSize = compressed.length;
    assert(compress2(compressed.mutableBytes, &compressedSize, source.bytes, source.length, 1) == Z_OK);
    compressed.length = compressedSize;
    assert([compressed writeToFile:[support stringByAppendingPathComponent:@"sources.pack"] atomically:YES]);
    NSMutableDictionary *packedEntry = [@{@"name": @"test.metallib", @"offset": @0,
        @"compressed_size": @(compressedSize), @"size": @(source.length), @"sha256": shaderSupportDigest(source)} mutableCopy];
    NSDictionary *packedManifest = @{@"version": @2, @"chain": @"test", @"files": @[packedEntry]};
    assert([[NSJSONSerialization dataWithJSONObject:packedManifest options:0 error:nil]
        writeToFile:[support stringByAppendingPathComponent:@"manifest.json"] atomically:YES]);
    assert([fileManager removeItemAtPath:targetPath error:nil]);
    assert(prepareIosShaderSupport(support, directory, @"test", &error));
    assert([[NSData dataWithContentsOfFile:targetPath] isEqual:source]);
    packedEntry[@"offset"] = @(UINT64_MAX);
    assert([[NSJSONSerialization dataWithJSONObject:packedManifest options:0 error:nil]
        writeToFile:[support stringByAppendingPathComponent:@"manifest.json"] atomically:YES]);
    assert(!prepareIosShaderSupport(support, directory, @"test", &error));
    __weak NSObject *weakSource = nil;
    __weak NSObject *weakDestination = nil;
    @autoreleasepool {
    NSMutableArray *resources = [NSMutableArray array];
    @autoreleasepool {
        NSObject *uploadSource = [NSObject new], *uploadDestination = [NSObject new];
        weakSource = uploadSource; weakDestination = uploadDestination;
        retainUploadResources(resources, uploadSource, uploadDestination);
    }
    @autoreleasepool { assert(weakSource && weakDestination); }
    [resources removeAllObjects];
    }
    assert(!weakSource && !weakDestination);
    assert([fileManager removeItemAtPath:root error:nil]);
    puts("iOS shader receipts: hot launch, repair, update and interrupted verification; upload ownership passed");
} }
