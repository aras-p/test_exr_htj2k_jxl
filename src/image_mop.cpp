
#include <meshoptimizer.h>

#include "image_mop.h"
#include "fileio.h"

constexpr size_t kChunkSize = 64 * 1024;

void InitMop(int thread_count)
{
    //@TODO
}

bool LoadMopFile(MyIStream &mem, Image& r_image)
{
    size_t pixel_stride = 0;

    // header
    {
        char magic[4];
        mem.read(magic);
        if (memcmp(magic, "MOPF", 4) != 0)
            return false;
        int32_t width = 0, height = 0, chCount = 0;
        mem.read(width);
        mem.read(height);
        mem.read(chCount);
        if (width < 1 || width > 1024 * 1024 * 1024 || height < 1 || height > 1024 * 1024 * 1024 || chCount < 1 || chCount > 1024 * 1024)
            return false;
        r_image.width = width;
        r_image.height = height;
        r_image.channels.reserve(chCount);
        for (int ich = 0; ich < chCount; ++ich)
        {
            int32_t type = 0, nameLen = 0;
            mem.read(type);
            mem.read(nameLen);
            if (type < 0 || type > 1)
                return false;
            if (nameLen < 0 || nameLen > 1024 * 1024)
                return false;

            Image::Channel ch = {};
            ch.fp16 = type == 0;
            ch.name.resize(nameLen);
            mem.read(ch.name.data(), ch.name.size());
            ch.offset = pixel_stride;
            pixel_stride += ch.fp16 ? 2 : 4;
            r_image.channels.emplace_back(ch);
        }
    }

    const size_t pixel_count = r_image.width * r_image.height;
    r_image.pixels.resize(pixel_count * pixel_stride);

    size_t chunk_count = (pixel_count + kChunkSize - 1) / kChunkSize;
    const size_t coded_stride = (pixel_stride + 3) / 4 * 4; // mesh optimizer requires stride to be multiple of 4

    uint64_t pos = mem.tellg();
    for (size_t ick = 0; ick < chunk_count; ++ick)
    {
        size_t encSize = 0;
        memcpy(&encSize, mem.data() + pos, sizeof(encSize));
        pos += sizeof(encSize);

        const size_t chunk_pixel_count = ick == chunk_count - 1 ? pixel_count - ick * kChunkSize : kChunkSize;
        char* dst_data = r_image.pixels.data() + ick * kChunkSize * pixel_stride;
        if (coded_stride == pixel_stride)
        {
            if (meshopt_decodeVertexBuffer(dst_data, chunk_pixel_count, coded_stride, (const uint8_t*)mem.data() + pos, encSize) != 0)
                return false;
        }
        else
        {
            char* padded_data = new char[chunk_pixel_count * coded_stride];
            if (meshopt_decodeVertexBuffer(padded_data, chunk_pixel_count, coded_stride, (const uint8_t*)mem.data() + pos, encSize) != 0)
            {
                delete[] padded_data;
                return false;
            }
            const char* src = padded_data;
            char* dst = dst_data;
            for (size_t i = 0; i < chunk_pixel_count; ++i)
            {
                memcpy(dst, src, pixel_stride);
                src += coded_stride;
                dst += pixel_stride;
            }
            delete[] padded_data;
        }
        pos += encSize;
    }

    return true;
}

bool SaveMopFile(MyOStream &mem, const Image& image, int cmp_level)
{
    // header
    {
        const char magic[] = {'M', 'O', 'P', 'F'};
        mem.write(magic);
        int32_t width = int32_t(image.width);
        int32_t height = int32_t(image.height);
        int32_t chCount = int32_t(image.channels.size());
        mem.write(width);
        mem.write(height);
        mem.write(chCount);
        for (const Image::Channel& ch : image.channels)
        {
            int32_t type = ch.fp16 ? 0 : 1;
            int32_t nameLen = int32_t(ch.name.size());
            mem.write(type);
            mem.write(nameLen);
            mem.write(ch.name.c_str(), ch.name.size());
        }
    }

    const size_t pixel_count = image.width * image.height;
    const size_t pixel_stride = image.pixels.size() / pixel_count;
    const size_t coded_stride = (pixel_stride + 3) / 4 * 4; // mesh optimizer requires stride to be multiple of 4
    size_t chunk_count = (pixel_count + kChunkSize - 1) / kChunkSize;
    for (size_t ick = 0; ick < chunk_count; ++ick)
    {
        const size_t chunk_pixel_count = ick == chunk_count - 1 ? pixel_count - ick * kChunkSize : kChunkSize;
        size_t bufSize = meshopt_encodeVertexBufferBound(chunk_pixel_count, coded_stride);
        uint8_t* buf = new uint8_t[bufSize];
        const char* src_data = image.pixels.data() + ick * kChunkSize * pixel_stride;
        size_t encSize = 0;
        if (pixel_stride == coded_stride)
        {
            encSize = meshopt_encodeVertexBufferLevel(
                buf, bufSize,
                src_data, chunk_pixel_count, coded_stride,
                cmp_level, 1);
        }
        else
        {
            char* padded_data = new char[chunk_pixel_count * coded_stride];
            const char* src = src_data;
            char* dst = padded_data;
            for (size_t i = 0; i < chunk_pixel_count; ++i)
            {
                memcpy(dst, src, pixel_stride);
                src += pixel_stride;
                memset(dst + pixel_stride, 0, coded_stride - pixel_stride);
                dst += coded_stride;
            }
            encSize = meshopt_encodeVertexBufferLevel(
                buf, bufSize,
                padded_data, chunk_pixel_count, coded_stride,
                cmp_level, 1);
            delete[] padded_data;
        }
        mem.write(encSize);
        mem.write((const char*)buf, encSize);
        delete[] buf;
    }
    return true;
}
