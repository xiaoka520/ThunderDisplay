#include "RawPixelSupport.h"
#include <string.h>
#if defined(__aarch64__)
#include <arm_neon.h>
#endif

static uint16_t word(const uint8_t *input) {
    uint16_t value;
    memcpy(&value, input, sizeof(value));
    return value;
}

static void pack_row(const uint8_t *input, size_t width, uint8_t *output) {
    size_t x = 0;
#if defined(__aarch64__)
    // Shift each P010 sample into its position within a five-byte group, then
    // combine adjacent byte lanes. Six unused P010 bits are removed first.
    // Sixteen samples use exactly two 16-byte reads and 16 + 4 byte writes.
    const int16_t shifts[8] = {0, 2, 4, 6, 0, 2, 4, 6};
    const uint8_t lo[16] = {0, 1, 3, 5, 7, 8, 9, 11, 13, 15, 16, 17, 19, 21, 23, 24};
    const uint8_t hi[16] = {255, 2, 4, 6, 255, 255, 10, 12, 14, 255, 255, 18, 20, 22, 255, 255};
    const uint8_t tail_lo[8] = {25, 27, 29, 31, 255, 255, 255, 255};
    const uint8_t tail_hi[8] = {26, 28, 30, 255, 255, 255, 255, 255};
    const int16x8_t shift = vld1q_s16(shifts);
    const uint8x16_t order_lo = vld1q_u8(lo), order_hi = vld1q_u8(hi);
    const uint8x8_t last_lo = vld1_u8(tail_lo), last_hi = vld1_u8(tail_hi);
    for (; x + 16 <= width; x += 16, input += 32, output += 20) {
        uint8x16x2_t bytes;
        bytes.val[0] = vreinterpretq_u8_u16(vshlq_u16(vshrq_n_u16(vreinterpretq_u16_u8(vld1q_u8(input)), 6), shift));
        bytes.val[1] = vreinterpretq_u8_u16(vshlq_u16(vshrq_n_u16(vreinterpretq_u16_u8(vld1q_u8(input + 16)), 6), shift));
        vst1q_u8(output, vorrq_u8(vqtbl2q_u8(bytes, order_lo), vqtbl2q_u8(bytes, order_hi)));
        const uint8x8_t tail = vorr_u8(vqtbl2_u8(bytes, last_lo), vqtbl2_u8(bytes, last_hi));
        const uint32_t last = vget_lane_u32(vreinterpret_u32_u8(tail), 0);
        memcpy(output + 16, &last, sizeof(last));
    }
#endif
    for (; x + 4 <= width; x += 4, input += 8, output += 5) {
        const uint64_t bits = (uint64_t)(word(input) >> 6) | (uint64_t)(word(input + 2) >> 6) << 10 |
            (uint64_t)(word(input + 4) >> 6) << 20 | (uint64_t)(word(input + 6) >> 6) << 30;
        for (unsigned i = 0; i < 5; ++i) output[i] = (uint8_t)(bits >> (8 * i));
    }
    if (x < width) {
        const uint32_t bits = (uint32_t)(word(input) >> 6) | (uint32_t)(word(input + 2) >> 6) << 10;
        for (unsigned i = 0; i < 3; ++i) output[i] = (uint8_t)(bits >> (8 * i));
    }
}

void td_raw_pack_plane(const void *source, size_t stride, size_t width, size_t rows, void *destination, int packed) {
    const uint8_t *input = source;
    uint8_t *output = destination;
    const size_t row_bytes = packed ? (width * 10 + 7) / 8 : width * 2;
    for (size_t row = 0; row < rows; ++row, input += stride, output += row_bytes) {
        if (packed) { pack_row(input, width, output); continue; }
        size_t x = 0;
#if defined(__aarch64__)
        for (; x + 8 <= width; x += 8) {
            const uint16x8_t words = vreinterpretq_u16_u8(vld1q_u8(input + x * 2));
            vst1q_u8(output + x * 2, vreinterpretq_u8_u16(vandq_u16(words, vdupq_n_u16(0xFFC0))));
        }
#endif
        for (; x < width; ++x) {
            const uint16_t value = word(input + x * 2) & 0xFFC0;
            memcpy(output + x * 2, &value, sizeof(value));
        }
    }
}
