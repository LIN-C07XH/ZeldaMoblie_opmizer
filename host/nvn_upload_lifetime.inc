// Queue records use unsafe pointers. Acquire ownership when recording, before
// the caller can replace a texture or drop its last staging-buffer reference.
static void retainUploadResources(NSMutableArray *resources, id source, id destination) {
    if (source) [resources addObject:source];
    if (destination) [resources addObject:destination];
}
