#pragma once

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
    ExrHT256,
    Jxl,
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
    std::vector<char> pixels;
};

void SanitizePixelValues(Image& image);
bool CompareImages(const Image& ia, const Image& ib);
