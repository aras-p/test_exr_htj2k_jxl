#include "image.h"

void SanitizePixelValues(Image& image)
{
    const size_t pixel_stride = image.pixels.size() / image.width / image.height;
    for (const Image::Channel& ch : image.channels)
    {
        if (ch.fp16)
        {
            const char* ptr = image.pixels.data() + ch.offset;
            for (size_t i = 0, n = image.width * image.height; i != n; ++i)
            {
                uint16_t val = *(const uint16_t*)ptr;
                if (val < 1024 || (val >= 32769 && val <= 33791)) // subnormal
                {
                    val = 0;
                    *(uint16_t*)ptr = val;
                }
                ptr += pixel_stride;
            }
        }
    }
}

bool CompareImages(const Image& ia, const Image& ib)
{
    // basic info
    if (ia.width != ib.width || ia.height != ib.height || ia.channels.size() != ib.channels.size())
    {
        printf("ERROR: image sizes do not match: exp %zix%zi %zich, got %zix%zi %zich\n", ia.width, ia.height, ia.channels.size(), ib.width, ib.height, ib.channels.size());
        return false;
    }
    if (ia.pixels.size() != ib.pixels.size())
    {
        printf("ERROR: image pixel sizes do not match: exp %zi, got %zi\n", ia.pixels.size(), ib.pixels.size());
        return false;
    }
    
    // see if we have matching channels, note that we support different channel orderings
    const size_t pixel_stride = ia.pixels.size() / ia.width / ia.height;
    bool ok = true;
    for (const Image::Channel& cha : ia.channels)
    {
        int offsetb = -1;
        for (const Image::Channel& chb : ib.channels)
        {
            if (cha.name == chb.name)
            {
                if (cha.fp16 != chb.fp16)
                {
                    printf("ERROR: image channel '%s' type mismatch: exp fp16=%i, got %i\n", cha.name.c_str(), cha.fp16, chb.fp16);
                    return false;
                }
                offsetb = int(chb.offset);
                break;
            }
        }
        if (offsetb < 0)
        {
            printf("ERROR: image channel '%s' not found in 2nd image\n", cha.name.c_str());
            return false;
        }
        
        // compare pixel values
        const char* srca = ia.pixels.data() + cha.offset;
        const char* srcb = ib.pixels.data() + offsetb;
        int error_count = 0;
        if (cha.fp16)
        {
            for (size_t i = 0, n = ia.width * ia.height; i != n; ++i)
            {
                const uint16_t va = *(const uint16_t*)srca;
                const uint16_t vb = *(const uint16_t*)srcb;
                if (va != vb)
                {
                    ok = false;
                    ++error_count;
                    if (error_count < 5)
                        printf("  - ch %s mismatch: pixel %zi,%zi fp16 exp %i got %i\n", cha.name.c_str(), i%ia.width, i%ia.width, va, vb);
                }
                srca += pixel_stride;
                srcb += pixel_stride;
            }
        }
        else
        {
            for (size_t i = 0, n = ia.width * ia.height; i != n; ++i)
            {
                const float va = *(const float*)srca;
                const float vb = *(const float*)srcb;
                if (va != vb)
                {
                    ok = false;
                    ++error_count;
                    if (error_count < 5)
                        printf("  - ch %s mismatch: pixel %zi,%zi fp32 exp %f got %f\n", cha.name.c_str(), i%ia.width, i%ia.width, va, vb);
                }
                srca += pixel_stride;
                srcb += pixel_stride;
            }
        }
    }
    return ok;
}
