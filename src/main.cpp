#define _CRT_SECURE_NO_WARNINGS
#include <stdio.h>
#include <string.h>
#include <stdint.h>
#include <stdlib.h>
#include <chrono>
#include <string>

// OpenEXR
#include <ImfChannelList.h>
#include <ImfInputFile.h>
#include <ImfOutputFile.h>
#include <ImfFrameBuffer.h>
#include <half.h>

// libjxl
#include <jxl/decode_cxx.h>
#include <jxl/encode_cxx.h>
#include <jxl/color_encoding.h>
#include <jxl/thread_parallel_runner_cxx.h>
#include <jxl/resizable_parallel_runner_cxx.h>

#include "rapidhash/rapidhash.h"

#ifndef _MSC_VER
#include <sys/fcntl.h>
#endif
#include <thread>
#include "systeminfo.h"
#include "fileio.h"

#ifdef _DEBUG
const int kRunCount = 1;
#else
const int kRunCount = 4;
#endif

static JxlThreadParallelRunnerPtr s_jxl_runner;

inline std::chrono::high_resolution_clock::time_point time_now()
{
    return std::chrono::high_resolution_clock::now();
}

inline float time_duration_ms(std::chrono::high_resolution_clock::time_point t0)
{
    std::chrono::duration<float, std::milli> dt = std::chrono::high_resolution_clock::now() - t0;
    return dt.count();
}

struct Image
{
    struct Channel {
        std::string name;
        Imf::PixelType type;
        size_t offset;
    };
    size_t width = 0, height = 0;
    std::vector<Channel> channels;
    std::vector<char> pixels;
};

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

struct CompressorTypeDesc
{
    const char* name;
    CompressorType cmp;
    const char* color;
    int large;
};

static const CompressorTypeDesc kComprTypes[] =
{
    {"Raw",     CompressorType::Raw,        "a64436", 1}, // 0 - just raw bits read/write
    {"None",    CompressorType::ExrNone,    "a64436", 1}, // 1, red
    {"RLE",     CompressorType::ExrRLE,     "dc74ff", 0}, // 2, purple
    {"PIZ",     CompressorType::ExrPIZ,     "ff9a44", 0}, // 3, orange
    {"Zip",     CompressorType::ExrZIP,     "12b520", 0}, // 4, green
    {"HT256",   CompressorType::ExrHT256,   "0094ef", 0}, // 5, blue
	{"JXL",     CompressorType::Jxl,        "e01010", 1}, // 6, red
};
constexpr size_t kComprTypeCount = sizeof(kComprTypes) / sizeof(kComprTypes[0]);

struct CompressorDesc
{
    int type;
    int level;
};

static const CompressorDesc kTestCompr[] =
{
    //{ 0, 0 }, // just raw bits read/write
    
    // EXR
#if 1
    { 1, 0 }, // None
    { 2, 0 }, // RLE
    { 3, 0 }, // PIZ
    //{ 4, 2 },
    { 4, 4 }, // ZIP default
    //{ 4, 6 },
    //{ 4, 9 },
    { 5, 0 }, // HT256
#endif
    
    // JXL
#if 1
    //{ 6, 1 },
    { 6, 3 },
    //{ 6, 5 },
    //{ 6, 0 }, // default level 7
    //{ 6, 9 },
#endif
};
constexpr size_t kTestComprCount = sizeof(kTestCompr) / sizeof(kTestCompr[0]);

struct ComprResult
{
    size_t rawSize = 0;
    size_t cmpSize = 0;
    double tRead = 0;
    double tWrite = 0;
};
static ComprResult s_ResultRuns[kTestComprCount][kRunCount];
static ComprResult s_Result[kTestComprCount];

static void LoadExrFile(const char* file_path, Image& r_image)
{
    MyIStream in_stream(file_path);
    Imf::InputFile file(in_stream);
    const Imf::Header& header = file.header();
    const Imf::ChannelList& channels = header.channels();
    Imath::Box2i dw = header.dataWindow();
    r_image.width  = dw.max.x - dw.min.x + 1;
    r_image.height = dw.max.y - dw.min.y + 1;
    
    size_t offset = 0;
    for (auto it = channels.begin(); it != channels.end(); ++it) {
        const Imf::PixelType type = it.channel().type;
        const size_t size = type == Imf::HALF ? 2 : 4;
        r_image.channels.push_back({it.name(), type, offset});
        offset += size;
    }
    
    r_image.pixels.resize(r_image.width * r_image.height * offset);
    
    Imf::FrameBuffer fb;
    for (const auto& ch : r_image.channels) {
        char *ptr = r_image.pixels.data() + ch.offset - dw.min.x * offset - dw.min.y * offset * r_image.width;
        fb.insert(ch.name, Imf::Slice(ch.type, ptr, offset, offset * r_image.width));
    }
    file.setFrameBuffer(fb);
    file.readPixels(dw.min.y, dw.max.y);
}

static void SaveExrFile(const char* file_path, const Image& image, CompressorType cmp_type, int cmp_level)
{
    Imf::Compression compression = Imf::NUM_COMPRESSION_METHODS;
    switch (cmp_type) {
        case CompressorType::ExrNone: compression = Imf::NO_COMPRESSION; break;
        case CompressorType::ExrRLE: compression = Imf::RLE_COMPRESSION; break;
        case CompressorType::ExrPIZ: compression = Imf::PIZ_COMPRESSION; break;
        case CompressorType::ExrZIP: compression = Imf::ZIP_COMPRESSION; break;
        case CompressorType::ExrHT256: compression = Imf::HT256_COMPRESSION; break;
        default: break;
    }
    
    Imf::Header header(int(image.width), int(image.height));
    header.compression() = compression;
    Imf::FrameBuffer fb;
    if (cmp_level != 0)
    {
        if (compression == Imf::ZIP_COMPRESSION)
            header.zipCompressionLevel() = cmp_level;
    }
    
    const size_t stride = image.pixels.size() / image.width / image.height;
    for (const Image::Channel& ch : image.channels)
    {
        header.channels().insert(ch.name, Imf::Channel(ch.type));
        const char *ptr = image.pixels.data() + ch.offset;
        fb.insert(ch.name, Imf::Slice(ch.type, (char*)ptr, stride, stride * image.width));
    }

    MyOStream out_stream(file_path);
    Imf::OutputFile file(out_stream, header);
    file.setFrameBuffer(fb);
    file.writePixels(int(image.height));
}

static bool LoadJxlFile(const char* file_path, Image& r_image)
{
    size_t in_size = GetFileSize(file_path);
    if (in_size == 0)
    {
        printf("Failed to read JXL %s: file not found\n", file_path);
        return false;
    }
    std::vector<uint8_t> jxl_file(in_size);
    {
        MyIStream in_stream(file_path);
        in_stream.read((char*)jxl_file.data(), int(in_size));
    }
    
    JxlDecoderPtr dec = JxlDecoderMake(nullptr);
    if (JxlDecoderSubscribeEvents(dec.get(), JXL_DEC_BASIC_INFO | JXL_DEC_FULL_IMAGE) != JXL_DEC_SUCCESS)
    {
        printf("Failed to read JXL %s: JxlDecoderSubscribeEvents failed\n", file_path);
        return false;
    }
    if (JxlDecoderSetParallelRunner(dec.get(), JxlThreadParallelRunner, s_jxl_runner.get()) != JXL_DEC_SUCCESS)
    {
        printf("Failed to read JXL %s: JxlDecoderSetParallelRunner failed\n", file_path);
        return false;
    }
    
    JxlDecoderSetInput(dec.get(), jxl_file.data(), jxl_file.size());
    JxlDecoderCloseInput(dec.get());
    
    JxlBasicInfo info = {};
    JxlPixelFormat base_fmt = {};
    size_t base_stride = 0;
    base_fmt.num_channels = 1;
    Imf::PixelType base_type = Imf::NUM_PIXELTYPES;
    
    size_t ch_total_size = 0;
    std::vector<uint8_t> planar_buffer;

    while (true)
    {
        JxlDecoderStatus status = JxlDecoderProcessInput(dec.get());

        if (status == JXL_DEC_ERROR)
        {
            printf("Failed to read JXL %s: JxlDecoderProcessInput error\n", file_path);
            return false;
        }
        else if (status == JXL_DEC_NEED_MORE_INPUT)
        {
            printf("Failed to read JXL %s: got JXL_DEC_NEED_MORE_INPUT, should not happen\n", file_path);
            return false;
        }
        else if (status == JXL_DEC_BASIC_INFO)
        {
            if (JxlDecoderGetBasicInfo(dec.get(), &info) != JXL_DEC_SUCCESS)
            {
                printf("Failed to read JXL %s: JxlDecoderGetBasicInfo failed\n", file_path);
                return false;
            }
            r_image.width = info.xsize;
            r_image.height = info.ysize;
            if (info.bits_per_sample == 32 && info.exponent_bits_per_sample == 8)
            {
                base_fmt.data_type = JXL_TYPE_FLOAT;
                base_type = Imf::FLOAT;
                base_stride = 4;
            }
            else if (info.bits_per_sample == 16 && info.exponent_bits_per_sample == 5)
            {
                base_fmt.data_type = JXL_TYPE_FLOAT16;
                base_type = Imf::HALF;
                base_stride = 2;
            }
            else
            {
                printf("Failed to read JXL %s: unknown base type (bits %i exp %i) failed\n", file_path, info.bits_per_sample, info.exponent_bits_per_sample);
                return false;
            }
            
            const int num_channels = info.num_color_channels + info.num_extra_channels;
            
            if (JxlDecoderImageOutBufferSize(dec.get(), &base_fmt, &ch_total_size) != JXL_DEC_SUCCESS) {
                printf("Failed to read JXL %s: JxlDecoderImageOutBufferSize failed\n", file_path);
                return false;
            }
            planar_buffer.resize(num_channels * ch_total_size);
            size_t offset = 0;
            for (int i = 0; i < info.num_color_channels; ++i)
            {
                Image::Channel ch {"Base", base_type, offset};
                r_image.channels.push_back(ch);
                offset += base_stride;
            }
            for (int i = 0; i < info.num_extra_channels; ++i)
            {
                JxlExtraChannelInfo info = {};
                if (JxlDecoderGetExtraChannelInfo(dec.get(), i, &info) != JXL_DEC_SUCCESS) {
                    printf("Failed to read JXL %s: JxlDecoderGetExtraChannelInfo failed\n", file_path);
                    return false;
                }
                std::string name;
                name.resize(info.name_length);
                if (JxlDecoderGetExtraChannelName(dec.get(), i, name.data(), name.size() + 1) != JXL_DEC_SUCCESS) {
                    printf("Failed to read JXL %s: JxlDecoderGetExtraChannelName failed\n", file_path);
                    return false;
                }
                Image::Channel ch {name, base_type, offset};
                r_image.channels.push_back(ch);
                offset += base_stride;
            }
        }
        else if (status == JXL_DEC_NEED_IMAGE_OUT_BUFFER)
        {
            size_t plane_offset = 0;
            if (JxlDecoderSetImageOutBuffer(dec.get(), &base_fmt, planar_buffer.data() + plane_offset, ch_total_size) != JXL_DEC_SUCCESS)
            {
                printf("Failed to read JXL %s: JxlDecoderSetImageOutBuffer failed\n", file_path);
                return false;
            }
            plane_offset += ch_total_size;
            for (int i = 0; i < info.num_extra_channels; i++)
            {
                if (JxlDecoderSetExtraChannelBuffer(dec.get(), &base_fmt, planar_buffer.data() + plane_offset, ch_total_size, i) != JXL_DEC_SUCCESS)
                {
                    printf("Failed to read JXL %s: JxlDecoderSetExtraChannelBuffer failed\n", file_path);
                    return false;
                }
                plane_offset += ch_total_size;
            }
        }
        else if (status == JXL_DEC_FULL_IMAGE)
        {
            // Nothing to do. Do not yet return. If the image is an animation, more
            // full frames may be decoded. This example only keeps the last one.
        }
        else if (status == JXL_DEC_SUCCESS)
        {
            break;
        }
        else
        {
            printf("Failed to read JXL %s: unknown status %i\n", file_path, status);
            return false;
        }
    }
    
    // swizzle data into interleaved layout
    r_image.pixels.resize(planar_buffer.size());
    const uint8_t* src_ptr = planar_buffer.data();
    const size_t pixel_stride = r_image.pixels.size() / r_image.width / r_image.height;
    for (size_t i = 0; i < r_image.channels.size(); ++i) {
        const Image::Channel& ch = r_image.channels[i];
        
        // put channel data out of plana format into
        // an interleaved format
        if (base_stride == 2)
        {
            const uint8_t* src = src_ptr;
            char* dst = r_image.pixels.data() + ch.offset;
            for (size_t i = 0, n = r_image.width * r_image.height; i != n; ++i)
            {
                *(uint16_t*)dst = *(const uint16_t*)src;
                src += 2;
                dst += pixel_stride;
            }
        }
        else if (base_stride == 4)
        {
            const uint8_t* src = src_ptr;
            char* dst = r_image.pixels.data() + ch.offset;
            for (size_t i = 0, n = r_image.width * r_image.height; i != n; ++i)
            {
                *(uint32_t*)dst = *(const uint32_t*)src;
                src += 4;
                dst += pixel_stride;
            }
        }
        src_ptr += ch_total_size;
    }
    
    return true;
}

static bool SaveJxlFile(const char* file_path, const Image& image, int cmp_level)
{
    // create encoder
    JxlEncoderPtr enc = JxlEncoderMake(nullptr);
    if (JxlEncoderSetParallelRunner(enc.get(), JxlThreadParallelRunner, s_jxl_runner.get()) != JXL_ENC_SUCCESS)
    {
        printf("Failed to write JXL %s: JxlEncoderSetParallelRunner failed\n", file_path);
        return false;
    }
    
    // set basic info
    //JxlPixelFormat pixel_format = {3, JXL_TYPE_FLOAT, JXL_NATIVE_ENDIAN, 0}; //@TODO
    JxlBasicInfo basic_info;
    JxlEncoderInitBasicInfo(&basic_info);
    basic_info.xsize = int(image.width);
    basic_info.ysize = int(image.height);
    
    Imf::PixelType first_ch_type = image.channels.front().type;
    basic_info.bits_per_sample = first_ch_type == Imf::HALF ? 16 : 32;
    basic_info.exponent_bits_per_sample = first_ch_type == Imf::HALF ? 5 : (first_ch_type == Imf::FLOAT ? 8 : 0);
    // JXL has to have 1 or 3 base color channels; we'll assume we use first
    // one and the rest are "extra"
    basic_info.num_color_channels = 1;
    basic_info.num_extra_channels = int(image.channels.size()) - 1;
    basic_info.uses_original_profile = true;
    if (JxlEncoderSetBasicInfo(enc.get(), &basic_info) != JXL_ENC_SUCCESS)
    {
        printf("Failed to write JXL %s: JxlEncoderSetBasicInfo failed %i\n", file_path, JxlEncoderGetError(enc.get()));
        return false;
    }
    
    // color encoding
    JxlColorEncoding color_encoding;
    JxlColorEncodingSetToLinearSRGB(&color_encoding, true);
    if (JxlEncoderSetColorEncoding(enc.get(), &color_encoding) != JXL_ENC_SUCCESS)
    {
        printf("Failed to write JXL %s: JxlEncoderSetColorEncoding failed\n", file_path);
        return false;
    }

    // add other channels as "extra channels"
    for (size_t i = 1; i < image.channels.size(); ++i) {
        JxlExtraChannelInfo ec;
        Imf::PixelType type = image.channels[i].type;
        JxlEncoderInitExtraChannelInfo(JXL_CHANNEL_OPTIONAL, &ec);
        ec.bits_per_sample = type == Imf::HALF ? 16 : 32;
        ec.exponent_bits_per_sample = type == Imf::HALF ? 5 : (type == Imf::FLOAT ? 8 : 0);
        JxlEncoderSetExtraChannelInfo(enc.get(), i - 1, &ec);

        JxlEncoderSetExtraChannelName(enc.get(), i - 1, image.channels[i].name.c_str(), image.channels[i].name.size());
    }

    // frame settings
    JxlEncoderFrameSettings* frame = JxlEncoderFrameSettingsCreate(enc.get(), nullptr);
    JxlEncoderSetFrameLossless(frame, JXL_TRUE);
    if (cmp_level != 0)
        JxlEncoderFrameSettingsSetOption(frame, JXL_ENC_FRAME_SETTING_EFFORT, cmp_level);

    // add image channels as JXL "extra channels"
    std::vector<char> ch_buffer;
    const size_t pixel_stride = image.pixels.size() / image.width / image.height;
    for (size_t i = 0; i < image.channels.size(); ++i) {
        const Image::Channel& ch = image.channels[i];
        
        // put channel data out of interleaved source format into
        // a planar format
        const size_t ch_stride = ch.type == Imf::HALF ? 2 : 4;
        const size_t ch_total_size = image.width * image.height * ch_stride;
        if (ch_buffer.size() < ch_total_size) {
            ch_buffer.resize(ch_total_size);
        }
        if (ch_stride == 2)
        {
            const char* src = image.pixels.data() + ch.offset;
            char* dst = ch_buffer.data();
            for (size_t i = 0, n = image.width * image.height; i != n; ++i)
            {
                *(uint16_t*)dst = *(const uint16_t*)src;
                src += pixel_stride;
                dst += 2;
            }
        }
        else if (ch_stride == 4)
        {
            const char* src = image.pixels.data() + ch.offset;
            char* dst = ch_buffer.data();
            for (size_t i = 0, n = image.width * image.height; i != n; ++i)
            {
                *(uint32_t*)dst = *(const uint32_t*)src;
                src += pixel_stride;
                dst += 4;
            }
        }
        JxlPixelFormat fmt = {};
        fmt.num_channels = 1;
        fmt.data_type = ch.type == Imf::HALF ? JXL_TYPE_FLOAT16 : (ch.type == Imf::FLOAT ? JXL_TYPE_FLOAT : JXL_TYPE_UINT16); //@TODO: UINT32 not supported by JXL
        fmt.endianness = JXL_NATIVE_ENDIAN;
        
        if (i == 0)
        {
            if (JxlEncoderAddImageFrame(frame, &fmt, ch_buffer.data(), ch_total_size) != JXL_ENC_SUCCESS)
            {
                printf("Failed to write JXL %s: JxlEncoderAddImageFrame failed\n", file_path);
                return false;
            }
        }
        else
        {
            if (JxlEncoderSetExtraChannelBuffer(frame, &fmt, ch_buffer.data(), ch_total_size, int(i) - 1) != JXL_ENC_SUCCESS)
            {
                printf("Failed to write JXL %s: JxlEncoderSetExtraChannelBuffer failed\n", file_path);
                return false;
            }
        }
    }

    JxlEncoderCloseInput(enc.get());

    // encode into buffer
    std::vector<uint8_t> compressed;
    compressed.resize(1024 * 1024);
    uint8_t* next_out = compressed.data();
    size_t avail_out = compressed.size();
    JxlEncoderStatus process_result = JXL_ENC_NEED_MORE_OUTPUT;
    while (process_result == JXL_ENC_NEED_MORE_OUTPUT)
    {
        process_result = JxlEncoderProcessOutput(enc.get(), &next_out, &avail_out);
        if (process_result == JXL_ENC_NEED_MORE_OUTPUT)
        {
          size_t offset = next_out - compressed.data();
          compressed.resize(compressed.size() * 2);
          next_out = compressed.data() + offset;
          avail_out = compressed.size() - offset;
        }
    }
    compressed.resize(next_out - compressed.data());
    if (process_result != JXL_ENC_SUCCESS)
    {
        printf("Failed to write JXL %s: JxlEncoderProcessOutput failed\n", file_path);
        return false;
    }

    // write to file
    FILE* f = fopen(file_path, "wb");
    fwrite(compressed.data(), 1, next_out - compressed.data(), f);
    fclose(f);

    return true;
}


static bool TestFile(const char* file_path, int run_index)
{
    const char* fname_part = strrchr(file_path, '/');
    if (fname_part == nullptr)
        fname_part = file_path;
    printf("%s: ", fname_part);
    
    // read the input file
    Image img_in;
    LoadExrFile(file_path, img_in);
    printf("%ix%i, %i channels, %i bytes/pixel\n", int(img_in.width), int(img_in.height), int(img_in.channels.size()), int(img_in.pixels.size()/img_in.width/img_in.height));

    // compute hash of pixel data
    const size_t raw_size = img_in.pixels.size();
    const uint64_t hash_in = rapidhash(img_in.pixels.data(), img_in.pixels.size());
    
    // test various compression schemes
    for (size_t cmp_index = 0; cmp_index < kTestComprCount; ++cmp_index)
    {
        const auto& cmp = kTestCompr[cmp_index];
        const CompressorType cmp_type = kComprTypes[cmp.type].cmp;
        const char* out_file_path = nullptr;
        double t_write = 0;
        double t_read = 0;

        // save the file with given compressor
        auto t_write_0 = time_now();
        if (cmp_type == CompressorType::Raw)
        {
            out_file_path = "_outfile.raw";
            FILE* f = fopen(out_file_path, "wb");
            TurnOffFileCache(f);
            fwrite(img_in.pixels.data(), img_in.pixels.size(), 1, f);
            fclose(f);
        }
        else if (cmp_type == CompressorType::Jxl)
        {
            out_file_path = "_outfile.jxl";
            if (!SaveJxlFile(out_file_path, img_in, cmp.level))
            {
                printf("ERROR: file could not be saved to JXL %s\n", fname_part);
                return false;
            }
        }
        else
        {
            out_file_path = "_outfile.exr";
            SaveExrFile(out_file_path, img_in, cmp_type, cmp.level);
        }
        t_write = time_duration_ms(t_write_0) / 1000.0f;
        size_t out_size = GetFileSize(out_file_path);
        
        // purge filesystem caches
#ifndef _MSC_VER
        int purgeVal = system("purge");
        if (purgeVal != 0)
            printf("WARN: failed to purge\n");
#endif
        
        // read the file back
        Image img_got;
        auto t_read_0 = time_now();
        if (cmp_type == CompressorType::Raw)
        {
            FILE* f = fopen(out_file_path, "rb");
            TurnOffFileCache(f);
            img_got.width = img_in.width;
            img_got.height = img_in.height;
            img_got.pixels.resize(img_in.pixels.size());
            fread(img_got.pixels.data(), img_got.pixels.size(), 1, f);
            fclose(f);
        }
        else if (cmp_type == CompressorType::Jxl)
        {
            if (!LoadJxlFile(out_file_path, img_got))
            {
                printf("ERROR: file could not be loaded from JXL %s\n", fname_part);
                return false;
            }
        }
        else
        {
            LoadExrFile(out_file_path, img_got);
        }
        t_read = time_duration_ms(t_read_0) / 1000.0f;
        const uint64_t hash_got = rapidhash(img_got.pixels.data(), img_got.pixels.size());
        // Note: libjxl currently does not seem to round-trip fp16 subnormals
        // even in full lossless mode, see https://github.com/libjxl/libjxl/issues/3881
        if (hash_got != hash_in && cmp_type != CompressorType::Jxl)
        {
            printf("ERROR: file did not roundtrip exactly with compression %s\n", kComprTypes[cmp.type].name);
            if (img_in.pixels.size() != img_got.pixels.size())
            {
                printf("- result pixel sizes do not even match: exp %zi got %zi\n", img_in.pixels.size(), img_got.pixels.size());
            }
            else
            {
                int counter = 0;
                size_t pixel_stride = img_in.pixels.size() / img_in.width / img_in.height;
                for (size_t i = 0; i < img_in.pixels.size(); i += pixel_stride)
                {
                    if (memcmp(img_in.pixels.data() + i, img_got.pixels.data() + i, pixel_stride) != 0)
                    {
                        size_t pix_idx = i / pixel_stride;
                        printf("- pixel index %zi (%zi,%zi) mismatch:\n", pix_idx, pix_idx % img_got.width, pix_idx / img_got.width);
                        for (const Image::Channel& ch : img_in.channels)
                        {
                            switch(ch.type) {
                                case Imf::HALF:
                                {
                                    const uint16_t vexp = *(const uint16_t*)(img_in.pixels.data() + i + ch.offset);
                                    const uint16_t vgot = *(const uint16_t*)(img_got.pixels.data() + i + ch.offset);
                                    if (vexp != vgot)
                                    {
                                        printf("  - ch %s mismatch: fp16 exp %i (%f) got %i (%f)\n", ch.name.c_str(),
                                               vexp, float(half(half::FromBits, vexp)),
                                               vgot, float(half(half::FromBits, vgot)));
                                    }
                                    break;
                                }
                                case Imf::FLOAT:
                                {
                                    const float vexp = *(const float*)(img_in.pixels.data() + i + ch.offset);
                                    const float vgot = *(const float*)(img_got.pixels.data() + i + ch.offset);
                                    if (vexp != vgot)
                                    {
                                        printf("  - ch %s mismatch: fp32 exp %f got %f\n", ch.name.c_str(), vexp, vgot);
                                    }
                                    break;
                                }
                                case Imf::UINT:
                                {
                                    const uint32_t vexp = *(const uint32_t*)(img_in.pixels.data() + i + ch.offset);
                                    const uint32_t vgot = *(const uint32_t*)(img_got.pixels.data() + i + ch.offset);
                                    if (vexp != vgot)
                                    {
                                        printf("  - ch %s mismatch: uint exp %u got %u\n", ch.name.c_str(),
                                               vexp, vgot);
                                    }
                                    break;
                                }
                                default:
                                    break;
                            }
                        }
                        ++counter;
                        if (counter > 10)
                            break;
                    }
                }
            }
            out_file_path = "_outfile.jxl.exr";
            SaveExrFile(out_file_path, img_got, CompressorType::ExrZIP, 0);
            return false;
        }

        auto& res = s_ResultRuns[cmp_index][run_index];
        res.rawSize += raw_size;
        res.cmpSize += out_size;
        res.tRead += t_read;
        res.tWrite += t_write;
        
        remove(out_file_path);
    }
    
    return true;
}

static void WriteReportRow(FILE* fout, uint64_t gotTypeMask, size_t cmpIndex, double xval, double yval)
{
    const int cmpLevel = kTestCompr[cmpIndex].level;
    const size_t typeIndex = kTestCompr[cmpIndex].type;
    const char* cmpName = kComprTypes[typeIndex].name;

    for (size_t ii = 0; ii < typeIndex; ++ii)
    {
        if ((gotTypeMask & (1ull<<ii)) == 0)
            continue;
        fprintf(fout, ",null,null");
    }
    fprintf(fout, ",%.2f,'", yval);
    if (cmpLevel != 0)
        fprintf(fout, "%s%i", cmpName, cmpLevel);
    else
        fprintf(fout, "%s", cmpName);
    fprintf(fout, ": %.3f ratio, %.1f MB/s'", xval, yval);
    for (size_t ii = typeIndex+1; ii < kComprTypeCount; ++ii)
    {
        if ((gotTypeMask & (1ull<<ii)) == 0)
            continue;
        fprintf(fout, ",null,null");
    }
}


static void WriteReportFile(int threadCount, int fileCount, size_t fullSize)
{
    std::string curTime = sysinfo_getcurtime();
    std::string outName = curTime + ".html";
    FILE* fout = fopen(outName.c_str(), "wb");
    fprintf(fout,
R"(<script type='text/javascript' src='https://www.gstatic.com/charts/loader.js'></script>
<center style='font-family: Arial;'>
<p><b>EXR compression ratio vs throughput</b>, %i files (%.1fMB) <span style='color: #ccc'>%s, %i runs</span</p>
<div style='border: 1px solid #ccc;'>
<div id='chart_w' style='width: 640px; height: 640px; display:inline-block;'></div>
<div id='chart_r' style='width: 640px; height: 640px; display:inline-block;'></div>
</div>
<p>%s, %s, %i threads</p>
<script type='text/javascript'>
google.charts.load('current', {'packages':['corechart']});
google.charts.setOnLoadCallback(drawChart);
function drawChart() {
var dw = new google.visualization.DataTable();
var dr = new google.visualization.DataTable();
dw.addColumn('number', 'Ratio');
dr.addColumn('number', 'Ratio');
)",
            fileCount, fullSize/1024.0/1024.0, curTime.c_str(), kRunCount,
            sysinfo_getplatform().c_str(), sysinfo_getcpumodel().c_str(), threadCount);

    uint64_t gotCmpTypeMask = 0;
    for (size_t cmpIndex = 0; cmpIndex < kTestComprCount; ++cmpIndex)
    {
        gotCmpTypeMask |= 1ull << kTestCompr[cmpIndex].type;
    }

    for (size_t cmpType = 0; cmpType < kComprTypeCount; ++cmpType)
    {
        if ((gotCmpTypeMask & (1ull << cmpType)) == 0)
            continue;
        const auto& cmp = kComprTypes[cmpType];
        fprintf(fout,
R"(dw.addColumn('number', '%s'); dw.addColumn({type:'string', role:'tooltip'});
dr.addColumn('number', '%s'); dr.addColumn({type:'string', role:'tooltip'});
)", cmp.name, cmp.name);
    }
    fprintf(fout, "dw.addRows([\n");
    for (size_t cmpIndex = 0; cmpIndex < kTestComprCount; ++cmpIndex)
    {
        const auto& res = s_Result[cmpIndex];
        double ratio = (double)res.rawSize/(double)res.cmpSize;
        fprintf(fout, "[%.3f", ratio);
        double perf = res.rawSize / (1024.0*1024.0) / res.tWrite;
        WriteReportRow(fout, gotCmpTypeMask, cmpIndex, ratio, perf);
        fprintf(fout, "]%s\n", cmpIndex == kTestComprCount-1 ? "" : ",");
    }
    fprintf(fout, "]);\n");
    fprintf(fout, "dr.addRows([\n");
    for (size_t cmpIndex = 0; cmpIndex < kTestComprCount; ++cmpIndex)
    {
        const auto& res = s_Result[cmpIndex];
        double ratio = (double)res.rawSize/(double)res.cmpSize;
        fprintf(fout, "[%.3f", ratio);
        double perf = res.rawSize / (1024.0*1024.0) / res.tRead;
        WriteReportRow(fout, gotCmpTypeMask, cmpIndex, ratio, perf);
        fprintf(fout, "]%s\n", cmpIndex == kTestComprCount-1 ? "" : ",");
    }
    fprintf(fout, "]);\n");

    fprintf(fout,
R"(var options = {
    title: 'Writing',
    pointSize: 18,
    series: {
)");
    int seriesIdx = 0;
    for (size_t cmpType = 0; cmpType < kComprTypeCount; ++cmpType)
    {
        if ((gotCmpTypeMask & (1ull<<cmpType)) == 0)
            continue;
        fprintf(fout, "        %i: {pointSize: %i},\n", seriesIdx, kComprTypes[cmpType].large ? 18 : 8);
        ++seriesIdx;
    }
    fprintf(fout,
R"(        100:{}},
    hAxis: {title: 'Compression ratio', viewWindow: {min:1.0,max:4.0}},
    vAxis: {title: 'Writing, MB/s', viewWindow: {min:0, max:10000}},
    chartArea: {left:60, right:10, top:50, bottom:50},
    legend: {position: 'top'},
    colors: [
)");
    bool firstCol = true;
    for (size_t cmpType = 0; cmpType < kComprTypeCount; ++cmpType)
    {
        if ((gotCmpTypeMask & (1ull << cmpType)) == 0)
            continue;
        if (!firstCol)
            fprintf(fout, ",");
        firstCol = false;
        const auto& cmp = kComprTypes[cmpType];
        fprintf(fout, "'#%s'", cmp.color);
    }

    fprintf(fout,
R"(]
};
var chw = new google.visualization.ScatterChart(document.getElementById('chart_w'));
chw.draw(dw, options);

options.title = 'Reading';
options.vAxis.title = 'Reading, MB/s';
options.vAxis.viewWindow.max = 10000;
var chr = new google.visualization.ScatterChart(document.getElementById('chart_r'));
chr.draw(dr, options);
}
</script>
)");
    
    fclose(fout);
}

int main(int argc, const char** argv)
{
    if (argc < 2) {
        printf("USAGE: test_exr_htj2k_jxl <input exr files>\n");
        return 1;
    }
    unsigned nThreads = std::thread::hardware_concurrency();
#ifdef _DEBUG
    nThreads = 0;
#endif
    printf("Setting EXR/JXL to %i threads\n", nThreads);
    Imf::setGlobalThreadCount(nThreads);
    s_jxl_runner = JxlThreadParallelRunnerMake(nullptr, nThreads);

    for (int ri = 0; ri < kRunCount; ++ri)
    {
        printf("Run %i/%i...\n", ri+1, kRunCount);
        for (int fi = 1; fi < argc; ++fi)
        {
            bool ok = TestFile(argv[fi], ri);
            if (!ok)
                return 1;
        }
        
        for (int ci = 0; ci < kTestComprCount; ++ci)
        {
            const ComprResult& res = s_ResultRuns[ci][ri];
            ComprResult& dst = s_Result[ci];
            if (ri == 0)
            {
                dst = res;
            }
            else
            {
                if (res.cmpSize != dst.cmpSize)
                {
                    printf("ERROR: compressor case %i non deterministic compressed size (%zi vs %zi)\n", ci, res.cmpSize, dst.cmpSize);
                    return 1;
                }
                if (res.rawSize != dst.rawSize)
                {
                    printf("ERROR: compressor case %i non deterministic raw size (%zi vs %zi)\n", ci, res.rawSize, dst.rawSize);
                    return 1;
                }
                if (res.tRead < dst.tRead) dst.tRead = res.tRead;
                if (res.tWrite < dst.tWrite) dst.tWrite = res.tWrite;
            }
        }
    }

    WriteReportFile(nThreads, argc-1, s_Result[0].rawSize);
    printf("==== Summary (%i files, %i runs):\n", argc-1, kRunCount);
    for (size_t cmpIndex = 0; cmpIndex < kTestComprCount; ++cmpIndex)
    {
        const auto& cmp = kTestCompr[cmpIndex];
        const auto& res = s_Result[cmpIndex];

        double perfWrite = res.rawSize / (1024.0*1024.0) / res.tWrite;
        double perfRead = res.rawSize / (1024.0*1024.0) / res.tRead;
        printf("  %6s: %7.1f MB (%5.3fx) W: %6.3f s (%5.0f MB/s) R: %6.3f s (%5.0f MB/s)\n",
               kComprTypes[cmp.type].name,
               res.cmpSize/1024.0/1024.0,
               (double)res.rawSize/(double)res.cmpSize,
               res.tWrite,
               perfWrite,
               res.tRead,
               perfRead);
    }
    
    return 0;
}
