
#include <meshoptimizer.h>

#include "image_mop.h"
#include "fileio.h"
#define IC_PFOR_IMPLEMENTATION
#include "ic_pfor.h"

#include <string.h>

constexpr size_t kChunkSize = 16 * 1024;

void InitMop(int thread_count)
{
    ic::init_pfor(thread_count);
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
            mem.read(ch.name.data(), int(ch.name.size()));
            ch.offset = pixel_stride;
            pixel_stride += ch.fp16 ? 2 : 4;
            r_image.channels.emplace_back(ch);
        }
    }

    const size_t pixel_count = r_image.width * r_image.height;
    r_image.pixels_size = pixel_count * pixel_stride;
    r_image.pixels = std::make_unique<char[]>(r_image.pixels_size);

    size_t chunk_count = (pixel_count + kChunkSize - 1) / kChunkSize;
    const size_t coded_stride = (pixel_stride + 3) / 4 * 4; // mesh optimizer requires stride to be multiple of 4

    std::vector<std::pair<size_t, size_t>> chunk_start_size(chunk_count);
    for (auto& chunk : chunk_start_size)
    {
        mem.read(chunk.second);
    }
    uint64_t pos = mem.tellg();
    for (auto& chunk : chunk_start_size)
    {
        chunk.first = pos;
        pos += chunk.second;
    }

    std::unique_ptr<char[]> padded_buffer;
    if (coded_stride != pixel_stride) {
        padded_buffer.reset(new char[ic::pfor_workers() * kChunkSize * coded_stride]);
    }

    bool ok = true;
    ic::pfor(unsigned(chunk_count), 1, [&](int index, int tid) {
        const size_t encStart = chunk_start_size[index].first;
        const size_t encSize = chunk_start_size[index].second;

        const size_t chunk_pixel_count = index == chunk_count - 1 ? pixel_count - index * kChunkSize : kChunkSize;
        char* dst_data = r_image.pixels.get() + index * kChunkSize * pixel_stride;
        if (coded_stride == pixel_stride)
        {
            if (meshopt_decodeVertexBuffer(dst_data, chunk_pixel_count, coded_stride, (const uint8_t*)mem.data() + encStart, encSize) != 0)
            {
                ok = false;
                return;
            }
        }
        else
        {
            char* padded_data = padded_buffer.get() + kChunkSize * coded_stride * tid;
            if (meshopt_decodeVertexBuffer(padded_data, chunk_pixel_count, coded_stride, (const uint8_t*)mem.data() + encStart, encSize) != 0)
            {
                delete[] padded_data;
                ok = false;
                return;
            }
            const char* src = padded_data;
            char* dst = dst_data;
            for (size_t i = 0; i < chunk_pixel_count; ++i)
            {
                memcpy(dst, src, pixel_stride);
                src += coded_stride;
                dst += pixel_stride;
            }
        }
        });

    return ok;
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
            mem.write(ch.name.c_str(), int(ch.name.size()));
        }
    }

    const size_t pixel_count = image.width * image.height;
    const size_t pixel_stride = image.pixels_size / pixel_count;
    const size_t coded_stride = (pixel_stride + 3) / 4 * 4; // mesh optimizer requires stride to be multiple of 4
    size_t chunk_count = (pixel_count + kChunkSize - 1) / kChunkSize;

    std::vector<std::pair<uint8_t*, size_t>> encoded_chunks(chunk_count);

    ic::pfor(unsigned(chunk_count), 1, [&](int index, int tid) {
        const size_t chunk_pixel_count = index == chunk_count - 1 ? pixel_count - index * kChunkSize : kChunkSize;
        size_t bufSize = meshopt_encodeVertexBufferBound(chunk_pixel_count, coded_stride);
        uint8_t* buf = new uint8_t[bufSize];
        const char* src_data = image.pixels.get() + index * kChunkSize * pixel_stride;
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
        encoded_chunks[index] = { buf, encSize };
        });

    for (std::pair<uint8_t*, size_t>& chunk : encoded_chunks)
    {
        mem.write(chunk.second);
    }
    for (std::pair<uint8_t*, size_t>& chunk : encoded_chunks)
    {
        mem.write((const char*)chunk.first, int(chunk.second));
        delete[] chunk.first;
    }
    return true;
}
