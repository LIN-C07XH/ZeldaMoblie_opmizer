#pragma once
#include "audio_voice.h"
#include <stddef.h>

typedef struct {
    uint64_t byteOffset;
    size_t byteCount;
    uint32_t sampleBase;
} AudioWaveWindow;

// Capacity is not a readable payload length. Snapshot only samples the decoder
// can consume, preserving ADPCM block phase and the existing loop coordinates.
static inline bool audioWaveWindow(const audio_wave *wave, uint32_t format, uint32_t channels, AudioWaveWindow *window) {
    if (!wave || !window || !channels || channels > 6 || wave->start >= wave->end ||
        (format != 2 && format != 5 && format != 6) || (format == 6 && channels != 1)) return false;
    uint64_t first = wave->loop ? 0 : wave->start, last = wave->end;
    if (wave->loop && wave->loop_start && wave->loop_end && wave->loop_end > last) last = wave->loop_end;
    uint64_t offset, end;
    uint32_t sampleBase;
    if (format == 6) {
        sampleBase = (uint32_t)(first / 14 * 14);
        offset = first / 14 * 8;
        uint64_t finalSample = last - 1;
        end = finalSample / 14 * 8 + 2 + finalSample % 14 / 2;
    } else {
        uint64_t stride = channels * (format == 2 ? 2u : 4u);
        sampleBase = (uint32_t)first;
        offset = first * stride;
        end = last * stride;
    }
    if (offset >= end || end > wave->size || end - offset > SIZE_MAX) return false;
    *window = (AudioWaveWindow){offset, (size_t)(end - offset), sampleBase};
    return true;
}
