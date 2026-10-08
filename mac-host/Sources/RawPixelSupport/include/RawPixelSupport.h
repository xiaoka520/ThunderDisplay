#ifndef TD_RAW_PIXEL_SUPPORT_H
#define TD_RAW_PIXEL_SUPPORT_H
#include <stddef.h>
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif

// Wire validates dimensions, strides and buffer lengths before these calls.
// Inputs may be unaligned. No read or write extends past a visible row.
void td_raw_pack_plane(const void *source, size_t stride, size_t width, size_t rows,
                       void *destination, int packed);

#ifdef __cplusplus
}
#endif
#endif
