#pragma once

#include <vector>
#include <cstdint>

// Encode RGBA8 image (row-major) to ETC1 PKM format.
// Inputs: rgba pixels (width*height*4), width, height
// Output: PKM file bytes (including 16-byte PKM header) in outPKM
// Returns true on success.
bool EncodeETC1ToPKM(const uint8_t* rgba, int width, int height, std::vector<uint8_t>& outPKM);
