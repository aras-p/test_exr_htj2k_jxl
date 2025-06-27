#include "image.h"

void SanitizePixelValues(Image& image)
{
    const size_t pixel_stride = image.pixels.size() / image.width / image.height;
    const char* ptr = image.pixels.data();
    for (size_t i = 0, n = image.width * image.height; i != n; ++i)
    {
        for (const Image::Channel& ch : image.channels)
        {
            if (ch.fp16)
            {
                uint16_t* pix = (uint16_t*)(ptr + ch.offset);
                uint16_t val = *pix;
                if (val < 1024 || (val >= 32769 && val <= 33791)) // subnormal
                {
                    *pix = 0;
                }
            }
        }
        ptr += pixel_stride;
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
    std::vector<size_t> ch_atob(ia.channels.size());
    for (size_t idxa = 0; idxa < ia.channels.size(); ++idxa)
    {
        const Image::Channel& cha = ia.channels[idxa];
        bool found = false;
        for (size_t idxb = 0; idxb < ib.channels.size(); ++idxb)
        {
            const Image::Channel& chb = ib.channels[idxb];
            if (cha.name == chb.name)
            {
                if (cha.fp16 != chb.fp16)
                {
                    printf("ERROR: image channel '%s' type mismatch: exp fp16=%i, got %i\n", cha.name.c_str(), cha.fp16, chb.fp16);
                    return false;
                }
                found = true;
                ch_atob[idxa] = idxb;
                break;
            }
        }
        if (!found)
        {
            printf("ERROR: image channel '%s' not found in 2nd image\n", cha.name.c_str());
            return false;
        }
    }

    // compare pixel values
    const size_t pixel_stride = ia.pixels.size() / ia.width / ia.height;
    const char* ptra = ia.pixels.data();
    const char* ptrb = ib.pixels.data();
    int error_count = 0;
    for (size_t i = 0, n = ia.width * ia.height; i != n; ++i)
    {
        for (size_t cidxa = 0; cidxa < ia.channels.size(); ++cidxa)
        {
            const Image::Channel& cha = ia.channels[cidxa];
            const Image::Channel& chb = ib.channels[ch_atob[cidxa]];
            if (cha.fp16)
            {
                const uint16_t va = *(const uint16_t*)(ptra + cha.offset);
                const uint16_t vb = *(const uint16_t*)(ptrb + chb.offset);
                if (va != vb)
                {
                    ++error_count;
                    if (error_count < 10)
                        printf("  - ch %s mismatch: pixel %zi,%zi fp16 exp %i got %i\n", cha.name.c_str(), i % ia.width, i % ia.width, va, vb);
                }
            }
            else
            {
                const float va = *(const float*)(ptra + cha.offset);
                const float vb = *(const float*)(ptrb + chb.offset);
                if (va != vb)
                {
                    ++error_count;
                    if (error_count < 10)
                        printf("  - ch %s mismatch: pixel %zi,%zi fp32 exp %f got %f\n", cha.name.c_str(), i % ia.width, i % ia.width, va, vb);
                }
            }
        }
        ptra += pixel_stride;
        ptrb += pixel_stride;
    }

    return error_count == 0;
}
