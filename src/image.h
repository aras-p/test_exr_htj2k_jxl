#pragma once

#include <memory>
#include <vector>
#include <stdint.h>
#include <string>

enum class CompressorType
{
    Raw,
    ExrNone,
    ExrRLE,
    ExrPIZ,
    ExrZIP,
    ExrHTJ2K_32,
    ExrHTJ2K_256,
    Jxl,
    Mop,
};

struct Image
{
    struct Channel {
        std::string name;
        bool fp16;
        size_t offset;
    };
    size_t width = 0, height = 0;
    std::vector<Channel> channels;
    std::unique_ptr<char[]> pixels;
    size_t pixels_size = 0;
};

void SanitizePixelValues(Image& image);
bool CompareImages(const Image& ia, const Image& ib);
