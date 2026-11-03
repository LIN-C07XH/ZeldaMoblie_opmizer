#import <Foundation/Foundation.h>
#import <Metal/Metal.h>
#include <stdatomic.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>
#include <assert.h>
#include <TargetConditionals.h>
#undef TARGET_OS_IPHONE
#define TARGET_OS_IPHONE 1
static NSString *TEST_ROOT;
@interface IrTestBundle : NSObject
+ (instancetype)mainBundle;
- (NSString *)resourcePath;
@end
@implementation IrTestBundle
+ (instancetype)mainBundle { return [IrTestBundle new]; }
- (NSString *)resourcePath { return TEST_ROOT; }
@end
@interface IrTestDevice : NSObject
- (id<MTLLibrary>)newLibraryWithData:(dispatch_data_t)data error:(NSError **)error;
@end
@implementation IrTestDevice
- (id<MTLLibrary>)newLibraryWithData:(dispatch_data_t)data error:(NSError **)error {
    (void)error;
    assert(dispatch_data_get_size(data) == 11);
    dispatch_data_apply(data, ^bool(dispatch_data_t region, size_t offset, const void *bytes, size_t size) {
        (void)region;
        assert(!memcmp(bytes, &"MTLBfixture"[offset], size));
        return true;
    });
    return (id<MTLLibrary>)[NSObject new];
}
@end
#define NSBundle IrTestBundle
#include "../host/shader_ir.inc"
int main(int argc, char **argv) { @autoreleasepool {
    assert(argc == 3);
    TEST_ROOT = @(argv[1]);
    NSData *source = [@"fixture source" dataUsingEncoding:NSUTF8StringEncoding];
    id<MTLDevice> device = (id<MTLDevice>)[IrTestDevice new];
    BOOL expected = !strcmp(argv[2], "valid");
    NSString *path = [TEST_ROOT stringByAppendingPathComponent:@"b-test/a.metallib"];
    assert((shader_ir_load(device, path, source.bytes, source.length, false) != nil) == expected);
    assert(!shader_ir_load(device, path, "changed", 7, false));
    assert(!shader_ir_load(device, path, source.bytes, source.length, true));
    setenv("ZELDA_SHADER_IR", "0", 1);
    assert(!shader_ir_load(device, path, source.bytes, source.length, false));
    shader_ir_report("fixture");
} }
