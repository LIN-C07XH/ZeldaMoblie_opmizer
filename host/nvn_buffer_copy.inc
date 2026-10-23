static inline void enc_b_copy_buffer(id encoder, id<MTLBuffer> source, NSUInteger sourceOffset,
                                    id<MTLBuffer> destination, NSUInteger destinationOffset, NSUInteger size) {
#if !DM_NVN_M3
    // Metal 4 copies may overlap; NVN's later copy must observe earlier writes, including aliases.
    if ([encoder conformsToProtocol:@protocol(MTL4CommandEncoder)])
        [encoder barrierAfterEncoderStages:(MTLStageBlit | MTLStageDispatch) beforeEncoderStages:MTLStageBlit
            visibilityOptions:MTL4VisibilityOptionDevice];
#endif
    [encoder copyFromBuffer:source sourceOffset:sourceOffset toBuffer:destination destinationOffset:destinationOffset size:size];
}
