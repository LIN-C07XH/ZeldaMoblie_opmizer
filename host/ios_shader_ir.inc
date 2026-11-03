// iOS libraries are compiled for iOS 17 on the Mac, independently of the GPU/command API.
// Keep the pack mapped and close its descriptor before loading any individual library.
static id<MTLLibrary> iosShaderIrLoad(id<MTLDevice> device, NSString *sourcePath,
                                    const void *source, size_t length, bool layered) {
    static NSDictionary *MANIFEST;
    static NSData *PACKED;
    static dispatch_once_t INIT_TOKEN;
    dispatch_once(&INIT_TOKEN, ^{
        NSString *directory = [NSBundle.mainBundle.resourcePath stringByAppendingPathComponent:@"shader-ir"];
        NSData *data = [NSData dataWithContentsOfFile:[directory stringByAppendingPathComponent:@"manifest.json"]];
        id record = data ? [NSJSONSerialization JSONObjectWithData:data options:0 error:nil] : nil;
        if ([record isKindOfClass:NSDictionary.class] && [record[@"version"] isEqual:@1] &&
            [record[@"contract"] isEqual:@IOS_SHADER_IR_CONTRACT] &&
            [record[@"target"] isEqual:@"air64-apple-ios17.0"] && [record[@"language"] isEqual:@"metal3.0"] &&
            [record[@"files"] isKindOfClass:NSDictionary.class]) {
            MANIFEST = record;
            PACKED = [NSData dataWithContentsOfFile:[directory stringByAppendingPathComponent:@"libraries.pack"]
                options:NSDataReadingMappedIfSafe error:nil];
        }
        fprintf(stderr, "IOS_SHADER_IR pack=%s libraries=%lu\n", PACKED ? "ready" : "unavailable",
            (unsigned long)[MANIFEST[@"files"] count]);
    });
    NSString *name = [sourcePath.lastPathComponent.stringByDeletingPathExtension
        stringByAppendingString:layered ? @"-cube.metallib" : @".metallib"];
    id entry = MANIFEST[@"files"][name];
    if (!PACKED || !entry) { atomic_fetch_add(&ir_missing, 1); return nil; }
    if (![entry isKindOfClass:NSDictionary.class] ||
        ![MANIFEST[@"source_directory"] isEqual:sourcePath.stringByDeletingLastPathComponent.lastPathComponent] ||
        ![entry[@"offset"] isKindOfClass:NSNumber.class] || ![entry[@"size"] isKindOfClass:NSNumber.class] ||
        ![entry[@"source_sha256"] isEqual:ir_hash(source, length)]) {
        atomic_fetch_add(&ir_rejected, 1); return nil;
    }
    uint64_t offset = [entry[@"offset"] unsignedLongLongValue], size = [entry[@"size"] unsignedLongLongValue];
    if (size < 4 || size > (64u << 20) || offset > PACKED.length || size > PACKED.length - offset) {
        atomic_fetch_add(&ir_rejected, 1); return nil;
    }
    const void *bytes = (const uint8_t *)PACKED.bytes + offset;
    if (memcmp(bytes, "MTLB", 4) || ![entry[@"binary_sha256"] isEqual:ir_hash(bytes, (size_t)size)]) {
        atomic_fetch_add(&ir_rejected, 1); return nil;
    }
    dispatch_data_t data = dispatch_data_create(bytes, (size_t)size, nil, ^{ (void)PACKED; });
    id<MTLLibrary> library = [device newLibraryWithData:data error:nil];
    atomic_fetch_add(library ? &ir_hits : &ir_failed, 1);
    return library;
}
