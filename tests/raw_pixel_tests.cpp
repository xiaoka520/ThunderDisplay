#include "RawPixelSupport.h"
#include "raw_video.hpp"
#include <cassert>
#include <iostream>

int main() {
    // Unaligned plane starts, tight/padded rows, every vector tail, all 1024
    // ten-bit codes, and nonzero unused bits. The existing Windows unpacker is
    // an independent consumer of the native Mac output.
    for (unsigned width : {320u, 322u, 324u, 326u, 328u, 330u, 332u, 334u, 4094u, 4096u}) {
        for (unsigned padding : {0u, 14u}) {
            const unsigned height = 240, rows = height + height / 2, stride = width * 2 + padding;
            td::Bytes source(size_t(stride) * rows + 1, 0xAB), expected(size_t(width) * rows * 2);
            for (unsigned row = 0; row < rows; ++row) for (unsigned x = 0; x < width; ++x) {
                const uint16_t value = uint16_t(((row * 197 + x * 137) % 1024) << 6);
                const uint16_t noisy = value | uint16_t((row + x) % 64);
                std::memcpy(source.data() + 1 + size_t(row) * stride + x * 2, &noisy, 2);
                std::memcpy(expected.data() + (size_t(row) * width + x) * 2, &value, 2);
            }
            for (bool packed : {false, true}) {
                const size_t count = packed ? td::rawPacked10Bytes(width, height) : expected.size();
                td::Bytes storage(count + 10, 0xCD);
                td_raw_pack_plane(source.data() + 1, stride, width, rows, storage.data() + 3, packed);
                assert(std::all_of(storage.begin(), storage.begin() + 3, [](uint8_t b) { return b == 0xCD; }));
                assert(std::all_of(storage.begin() + 3 + count, storage.end(), [](uint8_t b) { return b == 0xCD; }));
                td::Bytes actual(storage.begin() + 3, storage.begin() + 3 + count);
                if (packed) {
                    td::Bytes decoded(expected.size()); td::unpackRaw10(actual, width, height, decoded, false);
                    assert(decoded == expected);
                } else assert(actual == expected);
            }
        }
    }
    // Tight final rows with no readable lookahead (also run under sanitizers).
    for (unsigned width : {320u, 322u, 334u, 4094u, 4096u}) {
        td::Bytes input(width * 2, 0xFF), output((width * 10 + 7) / 8);
        td_raw_pack_plane(input.data(), width * 2, width, 1, output.data(), true);
        assert(output.back() == (width % 4 ? 15 : 255));
    }
    std::cout << "Native raw pixels: exact ten-bit round trip, row tails, strides and bounds passed\n";
}
