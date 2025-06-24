#include <jxl/decode_cxx.h>
#include <jxl/encode_cxx.h>
#include <jxl/color_encoding.h>
#include <jxl/thread_parallel_runner_cxx.h>
#include <jxl/resizable_parallel_runner_cxx.h>

#include "image_jxl.h"
#include "fileio.h"

static JxlThreadParallelRunnerPtr s_jxl_runner;

void InitJxl(int thread_count)
{
    s_jxl_runner = JxlThreadParallelRunnerMake(nullptr, thread_count);
}

bool LoadJxlFile(const char* file_path, Image& r_image)
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
                base_stride = 4;
            }
            else if (info.bits_per_sample == 16 && info.exponent_bits_per_sample == 5)
            {
                base_fmt.data_type = JXL_TYPE_FLOAT16;
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
                Image::Channel ch {"Base", base_stride == 2, offset};
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
                Image::Channel ch {name, base_stride == 2, offset};
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

bool SaveJxlFile(const char* file_path, const Image& image, int cmp_level)
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
    
    const bool first_ch_fp16 = image.channels.front().fp16;
    basic_info.bits_per_sample = first_ch_fp16 ? 16 : 32;
    basic_info.exponent_bits_per_sample = first_ch_fp16 ? 5 : 8;
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
        const bool fp16 = image.channels[i].fp16;
        JxlEncoderInitExtraChannelInfo(JXL_CHANNEL_OPTIONAL, &ec);
        ec.bits_per_sample = fp16 ? 16 : 32;
        ec.exponent_bits_per_sample = fp16 ? 5 : 8;
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
        const size_t ch_stride = ch.fp16 ? 2 : 4;
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
        fmt.data_type = ch.fp16 ? JXL_TYPE_FLOAT16 : JXL_TYPE_FLOAT;
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
