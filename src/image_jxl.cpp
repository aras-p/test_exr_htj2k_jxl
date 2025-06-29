#include <jxl/decode_cxx.h>
#include <jxl/encode_cxx.h>
#include <jxl/color_encoding.h>
#include <jxl/thread_parallel_runner_cxx.h>

#include "image_jxl.h"

#include <string.h>

static JxlThreadParallelRunnerPtr s_jxl_runner;

void InitJxl(int thread_count)
{
    s_jxl_runner = JxlThreadParallelRunnerMake(nullptr, thread_count);
}

bool LoadJxlFile(MyIStream &mem, Image& r_image)
{    
    JxlDecoderPtr dec = JxlDecoderMake(nullptr);
    if (JxlDecoderSubscribeEvents(dec.get(), JXL_DEC_BASIC_INFO | JXL_DEC_FRAME | JXL_DEC_FULL_IMAGE) != JXL_DEC_SUCCESS)
    {
        printf("Failed to read JXL: JxlDecoderSubscribeEvents failed\n");
        return false;
    }
    if (JxlDecoderSetParallelRunner(dec.get(), JxlThreadParallelRunner, s_jxl_runner.get()) != JXL_DEC_SUCCESS)
    {
        printf("Failed to read JXL: JxlDecoderSetParallelRunner failed\n");
        return false;
    }
    
    JxlDecoderSetInput(dec.get(), (const uint8_t*)mem.data(), mem.size());
    JxlDecoderCloseInput(dec.get());
    
    JxlBasicInfo info = {};
    bool has_alpha = false;
    int extra_non_alpha_channels = 0;
    int rgba_channels = 0;
    // not a vector to avoid zero-initialization of the whole buffer
    size_t total_buffer_size = 0;
    std::unique_ptr<uint8_t[]> planar_buffer;

    while (true)
    {
        JxlDecoderStatus status = JxlDecoderProcessInput(dec.get());

        if (status == JXL_DEC_ERROR)
        {
            printf("Failed to read JXL: JxlDecoderProcessInput error\n");
            return false;
        }
        else if (status == JXL_DEC_NEED_MORE_INPUT)
        {
            printf("Failed to read JXL: got JXL_DEC_NEED_MORE_INPUT, should not happen\n");
            return false;
        }
        else if (status == JXL_DEC_BASIC_INFO)
        {
            if (JxlDecoderGetBasicInfo(dec.get(), &info) != JXL_DEC_SUCCESS)
            {
                printf("Failed to read JXL: JxlDecoderGetBasicInfo failed\n");
                return false;
            }
            r_image.width = info.xsize;
            r_image.height = info.ysize;

            JxlPixelFormat base_fmt = {};
            base_fmt.num_channels = 1;
            if (info.bits_per_sample == 32 && info.exponent_bits_per_sample == 8)
            {
                base_fmt.data_type = JXL_TYPE_FLOAT;
            }
            else if (info.bits_per_sample == 16 && info.exponent_bits_per_sample == 5)
            {
                base_fmt.data_type = JXL_TYPE_FLOAT16;
            }
            else
            {
                printf("Failed to read JXL: unknown base type (bits %i exp %i) failed\n", info.bits_per_sample, info.exponent_bits_per_sample);
                return false;
            }
            
            size_t offset = 0;
            for (int i = 0; i < info.num_color_channels; ++i)
            {
                size_t ch_stride = base_fmt.data_type == JXL_TYPE_FLOAT16 ? 2 : 4;
                Image::Channel ch {std::string("Base") + char('0' + i), ch_stride == 2, offset};
                r_image.channels.push_back(ch);
                offset += ch_stride;
            }
            for (int i = 0; i < info.num_extra_channels; ++i)
            {
                JxlExtraChannelInfo ch_info = {};
                if (JxlDecoderGetExtraChannelInfo(dec.get(), i, &ch_info) != JXL_DEC_SUCCESS) {
                    printf("Failed to read JXL: JxlDecoderGetExtraChannelInfo failed\n");
                    return false;
                }
                has_alpha = info.alpha_bits > 0 && i == 0 && ch_info.type == JXL_CHANNEL_ALPHA;

                std::string name;
                name.resize(ch_info.name_length);
                if (JxlDecoderGetExtraChannelName(dec.get(), i, name.data(), name.size() + 1) != JXL_DEC_SUCCESS) {
                    printf("Failed to read JXL: JxlDecoderGetExtraChannelName failed\n");
                    return false;
                }
                size_t ch_stride = ch_info.bits_per_sample == 16 ? 2 : 4;
                Image::Channel ch {name, ch_stride == 2, offset};
                r_image.channels.push_back(ch);
                offset += ch_stride;
            }
            extra_non_alpha_channels = info.num_extra_channels - (has_alpha ? 1 : 0);
            rgba_channels = info.num_color_channels + (has_alpha ? 1 : 0);

            // Try to avoid the de-swizzle and extra memory overhead if we only have RGB(A) channels:
            // we can decode directly into destination.
            total_buffer_size = r_image.width * r_image.height * offset;
            if (extra_non_alpha_channels == 0)
            {
                r_image.pixels.resize(total_buffer_size);
            }
            else
            {
                planar_buffer = std::make_unique<uint8_t[]>(total_buffer_size);
            }
        }
        else if (status == JXL_DEC_FRAME)
        {
            // Try to reconstruct name of base color channels from JXL "frame name"
            JxlFrameHeader frame_info = {};
            if (JxlDecoderGetFrameHeader(dec.get(), &frame_info) != JXL_DEC_SUCCESS) {
                printf("Failed to read JXL: JxlDecoderGetFrameHeader failed\n");
                return false;
            }
            if (frame_info.name_length > 0 && r_image.channels.size() >= info.num_color_channels)
            {
                std::string name;
                name.resize(frame_info.name_length);
                if (JxlDecoderGetFrameName(dec.get(), name.data(), name.size() + 1) != JXL_DEC_SUCCESS) {
                    printf("Failed to read JXL: JxlDecoderGetFrameName failed\n");
                    return false;
                }
                if (info.num_color_channels == 1)
                {
                    r_image.channels[0].name = name;
                }
                else if (info.num_color_channels == 3)
                {
                    size_t sep_1 = name.find('/');
                    size_t sep_2 = name.rfind('/');
                    if (sep_1 != std::string::npos && sep_2 != std::string::npos && sep_1 != sep_2)
                    {
                        r_image.channels[0].name = name.substr(0, sep_1);
                        r_image.channels[1].name = name.substr(sep_1 + 1, sep_2 - sep_1 - 1);
                        r_image.channels[2].name = name.substr(sep_2 + 1);
                    }
                }
            }
        }
        else if (status == JXL_DEC_NEED_IMAGE_OUT_BUFFER)
        {
            JxlPixelFormat ch_fmt = { uint32_t(rgba_channels), r_image.channels.front().fp16 ? JXL_TYPE_FLOAT16 : JXL_TYPE_FLOAT, JXL_NATIVE_ENDIAN, 0};
            size_t ch_total_size = r_image.width * r_image.height * (ch_fmt.data_type == JXL_TYPE_FLOAT16 ? 2 : 4) * ch_fmt.num_channels;
            if (JxlDecoderSetImageOutBuffer(dec.get(), &ch_fmt, extra_non_alpha_channels == 0 ? (uint8_t*)r_image.pixels.data() : planar_buffer.get(), ch_total_size) != JXL_DEC_SUCCESS)
            {
                printf("Failed to read JXL: JxlDecoderSetImageOutBuffer failed\n");
                return false;
            }
            size_t plane_offset = ch_total_size;
            for (int i = has_alpha ? 1 : 0; i < info.num_extra_channels; i++)
            {
                ch_fmt.num_channels = 1;
                ch_fmt.data_type = r_image.channels[i + info.num_color_channels].fp16 ? JXL_TYPE_FLOAT16 : JXL_TYPE_FLOAT;
                ch_total_size = r_image.width * r_image.height * (ch_fmt.data_type == JXL_TYPE_FLOAT16 ? 2 : 4) * ch_fmt.num_channels;
                if (JxlDecoderSetExtraChannelBuffer(dec.get(), &ch_fmt, planar_buffer.get() + plane_offset, ch_total_size, i) != JXL_DEC_SUCCESS)
                {
                    printf("Failed to read JXL: JxlDecoderSetExtraChannelBuffer failed\n");
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
            printf("Failed to read JXL: unknown status %i\n", status);
            return false;
        }
    }

    if (extra_non_alpha_channels != 0)
    {
        // Swizzle data into interleaved layout. Note that especially for large
        // images, it seems to be much faster to do the loop by linearly writing
        // into destination, with scattered reads (i.e. order is "for all pixels,
        // for all channels") than it is to do linear reads, scattered writes
        // ("for all channels, for all pixels").
        r_image.pixels.resize(total_buffer_size);
        const uint8_t* src_ptr = planar_buffer.get();
        const size_t pixel_stride = r_image.pixels.size() / r_image.width / r_image.height;
        char* dst_ptr = r_image.pixels.data();

        const size_t color_ch_stride = rgba_channels * (r_image.channels[0].fp16 ? 2 : 4);
        const uint8_t* src_col_ptr = src_ptr;
        const size_t pixel_count = r_image.width * r_image.height;
        for (size_t i = 0, n = r_image.width * r_image.height; i != n; ++i)
        {
            // base color channels
            memcpy(dst_ptr, src_col_ptr, color_ch_stride);
            dst_ptr += color_ch_stride;
            src_col_ptr += color_ch_stride;

            // extra channels
            for (size_t ich = rgba_channels, nch = r_image.channels.size(); ich != nch; ++ich)
            {
                const size_t ch_size = r_image.channels[ich].fp16 ? 2 : 4;
                const size_t ch_offset = r_image.channels[ich].offset;
                memcpy(dst_ptr, src_ptr + ch_offset * pixel_count + i * ch_size, ch_size);
                dst_ptr += ch_size;
            }
        }
    }
    
    return true;
}

struct RGBAChannels
{
    int r = -1;
    int g = -1;
    int b = -1;
    int a = -1;
};

static RGBAChannels FindImageRGBAChannels(const Image& image)
{
    RGBAChannels rgba;
    for (int ch = 0; ch < image.channels.size(); ++ch)
    {
        // get channel name, as last component after '.' if that exists
        std::string name = image.channels[ch].name;
        size_t last_dot = name.rfind('.');
        if (last_dot != std::string::npos)
            name = name.substr(last_dot + 1);
        // lowercase it
        for (char& c : name)
        {
            if (c >= 'A' && c <= 'Z')
                c += 'a' - 'A';
        }

        // detect channels
        if (rgba.r < 0 && (name == "r" || name == "red"))
            rgba.r = ch;
        if (rgba.g < 0 && (name == "g" || name == "green"))
            rgba.g = ch;
        if (rgba.b < 0 && (name == "b" || name == "blue"))
            rgba.b = ch;
        if (rgba.a < 0 && (name == "a" || name == "alpha"))
            rgba.a = ch;
    }
    return rgba;
}

bool SaveJxlFile(MyOStream& mem, const Image& image, int cmp_level)
{
    // create encoder
    JxlEncoderPtr enc = JxlEncoderMake(nullptr);
    if (JxlEncoderSetParallelRunner(enc.get(), JxlThreadParallelRunner, s_jxl_runner.get()) != JXL_ENC_SUCCESS)
    {
        printf("Failed to write JXL: JxlEncoderSetParallelRunner failed\n");
        return false;
    }
    
    // set basic info
    JxlBasicInfo basic_info;
    JxlEncoderInitBasicInfo(&basic_info);
    basic_info.xsize = int(image.width);
    basic_info.ysize = int(image.height);

    // JXL has to have 1 or 3 color channels, plus optional alpha (everything else has to be "extra channels").
    // Try to detect which channels in input image might be RGB(A).
    bool use_rgb = false;
    RGBAChannels rgba = FindImageRGBAChannels(image);
    if (rgba.r >= 0 && rgba.g >= 0 && rgba.b >= 0 &&
        image.channels[rgba.r].fp16 == image.channels[rgba.g].fp16 && image.channels[rgba.r].fp16 == image.channels[rgba.b].fp16)
    {
        use_rgb = true;
    }
    
    const bool fp16 = image.channels[use_rgb ? rgba.r : 0].fp16;
    basic_info.bits_per_sample = fp16 ? 16 : 32;
    basic_info.exponent_bits_per_sample = fp16 ? 5 : 8;
    basic_info.num_color_channels = use_rgb ? 3 : 1;
    basic_info.num_extra_channels = int(image.channels.size()) - basic_info.num_color_channels;
    basic_info.uses_original_profile = true;

    // JXL also has a concept of alpha channel; use that if present
    const bool use_alpha = use_rgb && rgba.a >= 0 && image.channels[rgba.r].fp16 == image.channels[rgba.a].fp16;
    if (use_alpha)
    {
        basic_info.alpha_bits = fp16 ? 16 : 32;
        basic_info.alpha_exponent_bits = fp16 ? 5 : 8;
        basic_info.alpha_premultiplied = false;
    }
    if (JxlEncoderSetBasicInfo(enc.get(), &basic_info) != JXL_ENC_SUCCESS)
    {
        printf("Failed to write JXL: JxlEncoderSetBasicInfo failed %i\n", JxlEncoderGetError(enc.get()));
        return false;
    }
    
    // color encoding
    JxlColorEncoding color_encoding;
    JxlColorEncodingSetToLinearSRGB(&color_encoding, use_rgb ? false : true);
    if (JxlEncoderSetColorEncoding(enc.get(), &color_encoding) != JXL_ENC_SUCCESS)
    {
        printf("Failed to write JXL: JxlEncoderSetColorEncoding failed\n");
        return false;
    }

    // add other channels as "extra channels"
    int extra_ch_idx = 0;
    for (size_t idx = 0; idx < image.channels.size(); ++idx)
    {
        if (use_rgb)
        {
            if (idx == rgba.r || idx == rgba.g || idx == rgba.b)
                continue;
        }
        else
        {
            if (idx == 0)
                continue;
        }
        const Image::Channel& ch = image.channels[idx];
        JxlExtraChannelInfo ec;
        JxlEncoderInitExtraChannelInfo(use_alpha && idx == rgba.a ? JXL_CHANNEL_ALPHA : JXL_CHANNEL_OPTIONAL, &ec);
        ec.bits_per_sample = ch.fp16 ? 16 : 32;
        ec.exponent_bits_per_sample = ch.fp16 ? 5 : 8;
        JxlEncoderSetExtraChannelInfo(enc.get(), extra_ch_idx, &ec);
        JxlEncoderSetExtraChannelName(enc.get(), extra_ch_idx, ch.name.c_str(), ch.name.size());
        ++extra_ch_idx;
    }

    // frame settings
    JxlEncoderFrameSettings* frame = JxlEncoderFrameSettingsCreate(enc.get(), nullptr);
    // JXL does not store names of base color channels, so store them as "frame name"
    std::string frame_name;
    if (use_rgb)
    {
        frame_name = image.channels[rgba.r].name + "/" + image.channels[rgba.g].name + "/" + image.channels[rgba.b].name;
    }
    else
    {
        frame_name = image.channels[0].name;
    }
    JxlEncoderSetFrameName(frame, frame_name.c_str());
    JxlEncoderSetFrameLossless(frame, JXL_TRUE);
    if (cmp_level != 0)
        JxlEncoderFrameSettingsSetOption(frame, JXL_ENC_FRAME_SETTING_EFFORT, cmp_level);

    // If we have RGB(A), assemble that into interleaved format and pass to JXL
    const size_t pixel_stride = image.pixels.size() / image.width / image.height;
    std::vector<char> ch_buffer;
    if (use_rgb && use_alpha)
    {
        // RGBA
        const size_t ch_stride = fp16 ? 2 : 4;
        const size_t ch_total_size = image.width * image.height * ch_stride * 4;
        if (ch_buffer.size() < ch_total_size) {
            ch_buffer.resize(ch_total_size);
        }
        const size_t offset_r = image.channels[rgba.r].offset;
        const size_t offset_g = image.channels[rgba.g].offset;
        const size_t offset_b = image.channels[rgba.b].offset;
        const size_t offset_a = image.channels[rgba.a].offset;
        const char* src = image.pixels.data();
        char* dst = ch_buffer.data();
        if (ch_stride == 2)
        {
            for (size_t i = 0, n = image.width * image.height; i != n; ++i)
            {
                ((uint16_t*)dst)[0] = *(const uint16_t*)(src + offset_r);
                ((uint16_t*)dst)[1] = *(const uint16_t*)(src + offset_g);
                ((uint16_t*)dst)[2] = *(const uint16_t*)(src + offset_b);
                ((uint16_t*)dst)[3] = *(const uint16_t*)(src + offset_a);
                src += pixel_stride;
                dst += sizeof(uint16_t) * 4;
            }
        }
        else if (ch_stride == 4)
        {
            for (size_t i = 0, n = image.width * image.height; i != n; ++i)
            {
                ((uint32_t*)dst)[0] = *(const uint32_t*)(src + offset_r);
                ((uint32_t*)dst)[1] = *(const uint32_t*)(src + offset_g);
                ((uint32_t*)dst)[2] = *(const uint32_t*)(src + offset_b);
                ((uint32_t*)dst)[3] = *(const uint32_t*)(src + offset_a);
                src += pixel_stride;
                dst += sizeof(uint32_t) * 4;
            }
        }
        JxlPixelFormat fmt = {4, fp16 ? JXL_TYPE_FLOAT16 : JXL_TYPE_FLOAT, JXL_NATIVE_ENDIAN, 0};
        if (JxlEncoderAddImageFrame(frame, &fmt, ch_buffer.data(), ch_total_size) != JXL_ENC_SUCCESS)
        {
            printf("Failed to write JXL: JxlEncoderAddImageFrame RGBA failed\n");
            return false;
        }
    }
    else if (use_rgb && !use_alpha)
    {
        // RGB
        const size_t ch_stride = fp16 ? 2 : 4;
        const size_t ch_total_size = image.width * image.height * ch_stride * 3;
        if (ch_buffer.size() < ch_total_size) {
            ch_buffer.resize(ch_total_size);
        }
        const size_t offset_r = image.channels[rgba.r].offset;
        const size_t offset_g = image.channels[rgba.g].offset;
        const size_t offset_b = image.channels[rgba.b].offset;
        const char* src = image.pixels.data();
        char* dst = ch_buffer.data();
        if (ch_stride == 2)
        {
            for (size_t i = 0, n = image.width * image.height; i != n; ++i)
            {
                ((uint16_t*)dst)[0] = *(const uint16_t*)(src + offset_r);
                ((uint16_t*)dst)[1] = *(const uint16_t*)(src + offset_g);
                ((uint16_t*)dst)[2] = *(const uint16_t*)(src + offset_b);
                src += pixel_stride;
                dst += sizeof(uint16_t) * 3;
            }
        }
        else if (ch_stride == 4)
        {
            for (size_t i = 0, n = image.width * image.height; i != n; ++i)
            {
                ((uint32_t*)dst)[0] = *(const uint32_t*)(src + offset_r);
                ((uint32_t*)dst)[1] = *(const uint32_t*)(src + offset_g);
                ((uint32_t*)dst)[2] = *(const uint32_t*)(src + offset_b);
                src += pixel_stride;
                dst += sizeof(uint32_t) * 3;
            }
        }
        JxlPixelFormat fmt = { 3, fp16 ? JXL_TYPE_FLOAT16 : JXL_TYPE_FLOAT, JXL_NATIVE_ENDIAN, 0 };
        if (JxlEncoderAddImageFrame(frame, &fmt, ch_buffer.data(), ch_total_size) != JXL_ENC_SUCCESS)
        {
            printf("Failed to write JXL: JxlEncoderAddImageFrame RGB failed\n");
            return false;
        }
    }

    // add other channels as JXL "extra channels"
    extra_ch_idx = 0;
    for (size_t idx = 0; idx < image.channels.size(); ++idx)
    {
        if (use_rgb && (idx == rgba.r || idx == rgba.g || idx == rgba.b))
            continue;
        if (use_alpha && idx == rgba.a)
        {
            ++extra_ch_idx; // skip actual work, but count alpha as extra channel index
            continue;
        }

        const Image::Channel& ch = image.channels[idx];

        // put channel data into a planar format
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
                dst += sizeof(uint16_t);
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
                dst += sizeof(uint32_t);
            }
        }
        JxlPixelFormat fmt = {1, ch.fp16 ? JXL_TYPE_FLOAT16 : JXL_TYPE_FLOAT, JXL_NATIVE_ENDIAN, 0};
        if (!use_rgb && idx == 0)
        {
            if (JxlEncoderAddImageFrame(frame, &fmt, ch_buffer.data(), ch_total_size) != JXL_ENC_SUCCESS)
            {
                printf("Failed to write JXL: JxlEncoderAddImageFrame failed\n");
                return false;
            }
        }
        else
        {
            if (JxlEncoderSetExtraChannelBuffer(frame, &fmt, ch_buffer.data(), ch_total_size, extra_ch_idx) != JXL_ENC_SUCCESS)
            {
                printf("Failed to write JXL: JxlEncoderSetExtraChannelBuffer failed\n");
                return false;
            }
            ++extra_ch_idx;
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
        printf("Failed to write JXL: JxlEncoderProcessOutput failed\n");
        return false;
    }

    // output
    mem.write((const char*)compressed.data(), int(next_out - compressed.data()));
    return true;
}
