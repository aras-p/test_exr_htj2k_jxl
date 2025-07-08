
#include <meshoptimizer.h>
#include <zstd.h>

#include "image_mop.h"
#include "fileio.h"
#define IC_PFOR_IMPLEMENTATION
#include "ic_pfor.h"

#include <string.h>

constexpr size_t kChunkSize = 16 * 1024;

static int s_mop_thread_count;

void InitMop(int thread_count)
{
    s_mop_thread_count = ic::init_pfor(thread_count);
}
void ShutdownMop()
{
    ic::shut_pfor();
}

// File format:
// uchar4   magic MOPF
// int32    width
// int32    height
// int32    flags 1=zstd
// int32    nchannels
// for nchannels:
//      int32   type (0=fp16, 1=fp32)
//      int32   namelen
//      char[namelen] name
// int64[chunkcount] compressed chunk sizes


bool LoadMopFile(MyIStream &mem, Image& r_image)
{
    size_t pixel_stride = 0;
    bool zstd = false;

    // header
    {
        char magic[4];
        mem.read(magic);
        if (memcmp(magic, "MOPF", 4) != 0)
            return false;
        int32_t width = 0, height = 0, flags = 0, chCount = 0;
        mem.read(width);
        mem.read(height);
        mem.read(flags);
        if (flags & 1)
            zstd = true;
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
    r_image.pixels = std::unique_ptr<char[]>(new char[r_image.pixels_size]);

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

    bool ok = true;
    // if we need to add padding, reuse the padding buffers between work item invocations
    std::unique_ptr<char[]> padded_buffer;
    if (coded_stride != pixel_stride)
    {
        padded_buffer.reset(new char[s_mop_thread_count * kChunkSize * coded_stride]);
    }

    ic::pfor(unsigned(chunk_count), 1, [&](int index, int thread_index) {
        const size_t encStart = chunk_start_size[index].first;
        const size_t encSize = chunk_start_size[index].second;
        
        const uint8_t* decode_src = (const uint8_t*)mem.data() + encStart;
        size_t decode_size = encSize;
        std::unique_ptr<uint8_t[]> z_buf;
        if (zstd)
        {
            const size_t z_size = ZSTD_getFrameContentSize(decode_src, decode_size);
            z_buf = std::unique_ptr<uint8_t[]>(new uint8_t[z_size]);
            ZSTD_decompress(z_buf.get(), z_size, decode_src, decode_size);
            decode_src = z_buf.get();
            decode_size = z_size;
        }

        const size_t chunk_pixel_count = index == chunk_count - 1 ? pixel_count - index * kChunkSize : kChunkSize;
        char* dst_data = r_image.pixels.get() + index * kChunkSize * pixel_stride;
        if (coded_stride == pixel_stride)
        {
            if (meshopt_decodeVertexBuffer(dst_data, chunk_pixel_count, coded_stride, decode_src, decode_size) != 0)
            {
                ok = false;
                return;
            }
        }
        else
        {
            char* padded_data = padded_buffer.get() + kChunkSize * coded_stride * thread_index;
            if (meshopt_decodeVertexBuffer(padded_data, chunk_pixel_count, coded_stride, decode_src, decode_size) != 0)
            {
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
    const bool zstd = cmp_level >= 1<<8;
    const int mop_level = cmp_level & 0xFF;
    // header
    {
        const char magic[] = {'M', 'O', 'P', 'F'};
        mem.write(magic);
        int32_t width = int32_t(image.width);
        int32_t height = int32_t(image.height);
        int32_t chCount = int32_t(image.channels.size());
        int32_t flags = 0;
        if (zstd)
            flags |= 1;
        mem.write(width);
        mem.write(height);
        mem.write(flags);
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

    // if we need to add padding, reuse the padding buffers between work item invocations
    std::unique_ptr<char[]> padded_buffer;
    if (coded_stride != pixel_stride)
    {
        padded_buffer.reset(new char[s_mop_thread_count * kChunkSize * coded_stride]);
    }

    ic::pfor(unsigned(chunk_count), 1, [&](int index, int thread_index) {
        const size_t chunk_pixel_count = index == chunk_count - 1 ? pixel_count - index * kChunkSize : kChunkSize;
        size_t bufSize = meshopt_encodeVertexBufferBound(chunk_pixel_count, coded_stride);
        uint8_t* buf = new uint8_t[bufSize];
        const char* src_data = image.pixels.get() + index * kChunkSize * pixel_stride;
        size_t enc_size = 0;
        if (pixel_stride == coded_stride)
        {
            enc_size = meshopt_encodeVertexBufferLevel(
                buf, bufSize,
                src_data, chunk_pixel_count, coded_stride,
                mop_level, 1);
        }
        else
        {
            char* padded_data = padded_buffer.get() + kChunkSize * coded_stride * thread_index;
            const char* src = src_data;
            char* dst = padded_data;
            for (size_t i = 0; i < chunk_pixel_count; ++i)
            {
                memcpy(dst, src, pixel_stride);
                src += pixel_stride;
                memset(dst + pixel_stride, 0, coded_stride - pixel_stride);
                dst += coded_stride;
            }
            enc_size = meshopt_encodeVertexBufferLevel(
                buf, bufSize,
                padded_data, chunk_pixel_count, coded_stride,
                mop_level, 1);
        }
        
        if (zstd)
        {
            const size_t z_bound = ZSTD_compressBound(enc_size);
            uint8_t* z_buf = new uint8_t[z_bound];
            const int z_level = cmp_level >> 8;
            const size_t z_size = ZSTD_compress(z_buf, z_bound, buf, enc_size, z_level);
            delete[] buf;
            buf = z_buf;
            enc_size = z_size;
        }
        encoded_chunks[index] = { buf, enc_size };
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
