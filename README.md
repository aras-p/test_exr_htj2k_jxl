# OpenEXR, HTJ2K, JPEG-XL et al. comparison for lossless compression of half/float images

A quick test for various *lossless* compression modes of floating point images. Traditionally
OpenEXR has been the go-to format for that; here I compare several existing lossless compression
modes in it (ZIP, PIZ, RLE), as well as others:

- OpenEXR, with upcoming `HTJ2K` compression mode. This is based on "[High-Throughput JPEG 2000](https://jpeg.org/jpeg2000/htj2k.html)"
  format/algorithms, using open source [OpenJPH](https://github.com/aous72/OpenJPH) library.
- [JPEG-XL](https://jpeg.org/jpegxl/index.html) file format, using open source [libjxl](https://github.com/libjxl/libjxl)
  library.
- "MOP": not a real image format, just mis-using [meshoptimizer](https://github.com/zeux/meshoptimizer) to compress image pixels.

Floating point images usually come in various forms:
- High dynamic range "pictures", where the content is colors, just with higher dynamic range than regular images. Both increased range
  and increased precision is often needed. Environment lighting panoramas, baked lightmaps, or rendered/photographed HDR images are common.
  For testing I got several images from [Poly Haven](https://polyhaven.com/).
- "More than just colors" images, often used for compositing. For example, each pixel would store final color (as a regular image would do),
  but also depth, normal, colors of separate lighting passes (e.g. diffuse/glossy), ambient occlusion, object ID, and so on.
  I wanted to include some of those into testing, which I produced by rendering multi-layer EXR files with Blender, and including
  several different render passes.

Note that here I am only comparing the *lossless* compression modes. Lossy compression is a whole different topic.

### Results

Here's compression ratio (horizontal axis) vs performance (GB/s) charts; the more to the upper right corner the result is, the better.

| Windows (Ryzen 5950X, Visual Studio 2022 v17.14) | Mac (MacBookPro M4 Max, Xcode clang 16) |
|-----|----|
| ![](/img/win-ryzen5950x-20250630.png?raw=true) | ![](/img/mac-m4max-20250630.png?raw=true) |

M4 Max has *crazy high* memory bandwidth, I think that affects result difference way more than CPU & compiler differences.

### Code notes

- I am building `Release` cmake config on both `OpenEXR` and `libjxl` libraries, as well as any dependencies they pull in.
- I am setting up multi-threading via `Imf::setGlobalThreadCount()` for OpenEXR, and `JxlThreadParallelRunner` for libjxl.
- For the "mesh optimizer" ("Mop") test case, I am writing an "image" by:
  - A small header with image size and channel information,
  - Then image is split into chunks, each being 16K pixels in size. Each chunk is compressed independently and in parallel.
  - A small table with compressed sizes for each chunk is written after the header, followed by the compressed data itself
    for each chunk.
  - Mesh optimizer needs "vertex size" (pixel size in this case) to be multiple of four; if that is not the case the chunk data
    is padded with zeroes inside the compression/decompression code.

### My conclusions

- **mesh optimizer is very impressive!**
  - Yes that is not an actual image format, but it is funny that it *very* far ahead of others towards top right of the chart.
  - If you have some floating point pixel data to compress for internal usage, and don't care about interop with other applications,
    then trying out mesh optimizer sounds like a good idea.
  - [Arseny](https://zeux.io/about/) is a witch.
- Out of actual image formats, **just use OpenEXR** would be my go today.
  - Use the usual ZIP compression, at the default (4) level, and move on with your life.
  - Upcoming OpenEXR "HTJ2K" compression produces slightly better compression ratio, however at a bit slower compression,
    and 2x slower decompression speeds. Not sure if good tradeoff.
- **JPEG-XL is not great for lossless floating point compression right now**.
  - Compression levels 1/2 are _barely_ better ratio than EXR ZIP, but are 3-5x slower to compress/decompress.
  - Compression level 4 is indeed better ratio (2.09x, compared to EXR ZIP 1.87x, EXR HTJ2K 1.95x), but are 5-10x slower to compress.
  - The default compression level (7) is even slower, at 2.18x ratio. Feels like overkill though.
  - My impression is that floating point paths within `libjxl` did not (yet?) get the same attention as "regular images" did; it is very
    possible that they will improve the performance and/or ratio in the future (I was testing end-of-June 2025 code).
  - A cumbersome part of `libjxl` is that color channels need to be interleaved, and all the "other channels" need
    to be separate (planar). All my data is fully interleaved, so it costs some performance to arrange it as libjxl wants,
    both for compression and after decompression. As a user, it would be much more convenient to use if their API
    was similar to OpenEXR `Slice` that takes a pointer and two strides (stride between pixels, and stride between rows). Then
    any combination of interleaved/planar or mixed formats for different channels could be passed with the same API.
    In this very repository, `image_exr.cpp` is 80 lines of code, while `image_jxl.cpp` is 550 lines of code :scream:
  - `libjxl` currently is not fully lossless on half-precision subnormal values ([#3881](https://github.com/libjxl/libjxl/issues/3881)).

Most of the images I am testing with are "rendered result", with a bunch of path-tracing noise (in multi-layered images the main result
might be denoised, but some other channels would not be). It could very well be that the "big serious" codecs (HTJ2K, JPEG-XL) are way
better at compressing "natural" images or ones with camera-style noise. To be investigated by future research!


### Code size impact

Size increase of Win x64 statically linked executable, for each of the compression codecs:

- EXR HTJ2K: +308 KB. _Okay._
- JPEG-XL: +6017 KB. _Big!_
- mesh optimizer: +26 KB. _Tiny!_

### Files I am testing on

Total uncompressed size: 3122MB. Uploaded separately, in order to not blow up Git repo size and/or my Github LFS billing.

| File | Resolution | Raw size | Notes |
|------|-----------:|---------:|-------|
|[Blender281rgb16.exr](https://aras-p.info/files/exr_files/Blender281rgb16.exr) 	| 3840x2160 |  47.5MB | RGB half |
|[Blender281rgb32.exr](https://aras-p.info/files/exr_files/Blender281rgb32.exr) 	| 3840x2160 |  94.9MB | RGB float |
|[Blender281layered16.exr](https://aras-p.info/files/exr_files/Blender281layered16.exr) 	| 3840x2160 |  332.2MB | 21 channels, half |
|[Blender281layered32.exr](https://aras-p.info/files/exr_files/Blender281layered32.exr) 	| 3840x2160 |  664.5MB | 21 channels, float |
|[Blender35.exr](https://aras-p.info/files/exr_files/Blender35.exr) 	| 3840x2160 | 332.2MB | 18 channels, mixed half/float |
|[Blender40.exr](https://aras-p.info/files/exr_files/Blender40.exr) 	| 3840x2160 | 348.0MB | 15 channels, mixed half/float |
|[Blender41.exr](https://aras-p.info/files/exr_files/Blender41.exr) 	| 3840x2160 | 743.6MB | 37 channels, mixed half/float |
|[Blender43.exr](https://aras-p.info/files/exr_files/Blender43.exr) 	| 3840x2160 |  47.5MB | RGB half |
|[ph_brown_photostudio_02_8k.exr](https://aras-p.info/files/exr_files/ph_brown_photostudio_02_8k.exr) | 8192x4096 | 384.0MB | RGB float, from [polyhaven](https://polyhaven.com/a/brown_photostudio_02) |
|[ph_golden_gate_hills_4k.exr](https://aras-p.info/files/exr_files/ph_golden_gate_hills_4k.exr) | 4096x2048 | 128.0MB | RGBA float, from [polyhaven](https://polyhaven.com/a/golden_gate_hills) |

### External software

Obtained through cmake `FetchContent`:

- [OpenEXR](https://github.com/AcademySoftwareFoundation/openexr) - 2025 Jun, 3.4.0-dev with HTJ2K support (rev 45ee12752).
- [libjxl](https://github.com/libjxl/libjxl) - 2025 Jun, rev a75b322e.
- [meshoptimizer](https://github.com/zeux/meshoptimizer) - 2025 Jun, v0.24.

Directly in source tree:

- `src/ic_pfor.h` - parallel for utility from Ignacio Castano https://github.com/castano/icbc
