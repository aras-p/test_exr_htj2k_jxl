
#include <ImfChannelList.h>
#include <ImfInputFile.h>
#include <ImfOutputFile.h>
#include <ImfFrameBuffer.h>

#include "image_exr.h"
#include "fileio.h"

void InitExr(int thread_count)
{
    Imf::setGlobalThreadCount(thread_count);
}

bool LoadExrFile(MyIStream &mem, Image& r_image)
{
    Imf::InputFile file(mem);
    const Imf::Header& header = file.header();
    const Imf::ChannelList& channels = header.channels();
    Imath::Box2i dw = header.dataWindow();
    r_image.width  = dw.max.x - dw.min.x + 1;
    r_image.height = dw.max.y - dw.min.y + 1;
    
    size_t offset = 0;
    for (auto it = channels.begin(); it != channels.end(); ++it) {
        const Imf::PixelType type = it.channel().type;
        if (type == Imf::UINT)
        {
            printf("EXR files with UINT channels (%s) are not supported\n", it.name());
            return false;
        }
        const size_t size = type == Imf::HALF ? 2 : 4;
        r_image.channels.push_back({it.name(), type == Imf::HALF, offset});
        offset += size;
    }
    
    r_image.pixels_size = r_image.width * r_image.height * offset;
    r_image.pixels = std::unique_ptr<char[]>(new char[r_image.pixels_size]);
    
    Imf::FrameBuffer fb;
    for (const auto& ch : r_image.channels) {
        char *ptr = r_image.pixels.get() + ch.offset - dw.min.x * offset - dw.min.y * offset * r_image.width;
        fb.insert(ch.name, Imf::Slice(ch.fp16 ? Imf::HALF : Imf::FLOAT, ptr, offset, offset * r_image.width));
    }

    file.setFrameBuffer(fb);
    file.readPixels(dw.min.y, dw.max.y);
    return true;
}

bool SaveExrFile(MyOStream &mem, const Image& image, CompressorType cmp_type, int cmp_level)
{
    Imf::Compression compression = Imf::NUM_COMPRESSION_METHODS;
    switch (cmp_type) {
        case CompressorType::ExrNone: compression = Imf::NO_COMPRESSION; break;
        case CompressorType::ExrRLE: compression = Imf::RLE_COMPRESSION; break;
        case CompressorType::ExrPIZ: compression = Imf::PIZ_COMPRESSION; break;
        case CompressorType::ExrZIP: compression = Imf::ZIP_COMPRESSION; break;
        case CompressorType::ExrHTJ2K_32: compression = Imf::HTJ2K32_COMPRESSION; break;
        case CompressorType::ExrHTJ2K_256: compression = Imf::HTJ2K256_COMPRESSION; break;
        default: return false;
    }
    
    Imf::Header header(int(image.width), int(image.height));
    header.compression() = compression;
    Imf::FrameBuffer fb;
    if (cmp_level != 0)
    {
        if (compression == Imf::ZIP_COMPRESSION)
            header.zipCompressionLevel() = cmp_level;
    }
    
    const size_t stride = image.pixels_size / image.width / image.height;
    for (const Image::Channel& ch : image.channels)
    {
        header.channels().insert(ch.name, Imf::Channel(ch.fp16 ? Imf::HALF : Imf::FLOAT));
        const char *ptr = image.pixels.get() + ch.offset;
        fb.insert(ch.name, Imf::Slice(ch.fp16 ? Imf::HALF : Imf::FLOAT, (char*)ptr, stride, stride * image.width));
    }

    Imf::OutputFile file(mem, header);
    file.setFrameBuffer(fb);
    file.writePixels(int(image.height));
    return true;
}
