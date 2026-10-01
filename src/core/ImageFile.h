#pragma once

// The image formats decoded here rather than by stb_image -- OpenEXR
// (core/ExrImage.h) and TIFF (core/TiffImage.h) -- behind one probe and one
// sRGB decode, so a caller that hands everything else to stb branches once.

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace imagefile {

struct Info {
    int width = 0;
    int height = 0;
    int channels = 0;      // as stored, alpha included
};

struct Options {
    int channels = 3;      // 1, 3 or 4 wanted, interleaved in that order
    int threads = 0;       // 0 = all cores; 1 = decode on the calling thread
};

// True for an EXR or a TIFF, by the first bytes of the file.
bool handles(const std::string& path);

// Header only. Returns "" on success, else one sentence naming the problem.
std::string probe(const std::string& path, Info& info);

// Interleaved 8-bit sRGB. An unset half of the colour space is the file's own
// for an EXR, and Rec.709 / display-encoded for a TIFF.
std::string decode_srgb8(const std::string& path, const Options& opt, Info& info,
                         std::vector<uint8_t>& out, const std::string& gamut = "",
                         std::optional<bool> is_linear = std::nullopt);

}  // namespace imagefile
