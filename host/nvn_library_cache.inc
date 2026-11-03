// Keep prefetched libraries, rather than asking the driver to compile their
// source a second time. Content identity also rejects a replaced source file.
#include <CommonCrypto/CommonDigest.h>
#include <stdatomic.h>
static _Atomic uint64_t PREFETCH_LIBRARY_HITS;
static NSCache<NSString *, id<MTLLibrary>> *prefetchedLibraryCache(void) {
    static NSCache *CACHE;
    static dispatch_once_t INIT_TOKEN;
    dispatch_once(&INIT_TOKEN, ^{
        CACHE = [NSCache new];
        CACHE.countLimit = 128;
        CACHE.totalCostLimit = 32u << 20;
    });
    return CACHE;
}

static NSString *prefetchedLibraryKey(NSString *path, const void *bytes, size_t length) {
    if (!bytes || length > UINT32_MAX) return nil;
    unsigned char digest[CC_SHA256_DIGEST_LENGTH];
    CC_SHA256(bytes, (CC_LONG)length, digest);
    return [path stringByAppendingFormat:@":%@", [[NSData dataWithBytes:digest length:sizeof digest] base64EncodedStringWithOptions:0]];
}

static id<MTLLibrary> takePrefetchedLibrary(NSString *path, const void *bytes, size_t length) {
    NSString *key = prefetchedLibraryKey(path, bytes, length);
    id<MTLLibrary> library = key ? [prefetchedLibraryCache() objectForKey:key] : nil;
    if (library) atomic_fetch_add(&PREFETCH_LIBRARY_HITS, 1);
    return library;
}

static void rememberPrefetchedLibrary(NSString *path, NSData *source, id<MTLLibrary> library) {
    NSString *key = prefetchedLibraryKey(path, source.bytes, source.length);
    if (key && library) [prefetchedLibraryCache() setObject:library forKey:key cost:source.length];
}
