# OpenEXR, HTJ2K and JPEG-XL comparison for lossless compression of half/float images

A quick test for various *lossless* compression modes of floating point images. Traditionally
OpenEXR has been the go-to format for that; here I compare several existing lossless compression
modes in it (ZIP, PIZ, RLE), as well as others:

- OpenEXR, with upcoming `HT256` compression mode. This is based on "[High-Throughput JPEG 2000](https://jpeg.org/jpeg2000/htj2k.html)"
  format/algorithms, using open source [OpenJPH](https://github.com/aous72/OpenJPH) library.
- [JPEG-XL](https://jpeg.org/jpegxl/index.html) file format, using open source [libjxl](https://github.com/libjxl/libjxl)
  library.

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

Here's compression ratio (horizontal axis) vs performance (MB/s) charts; the more to the upper right corner the result is, the better. A chart
on Mac (M4 Max), using clang compiler from Xcode 16:

![](/img/mac-m4max-20250625.png?raw=true)

Results on Windows (Ryzen 5950X, using Visual Studio 2022 v17.14) overall are similar shape, except everything is 2x-3x slower;
partially due to compiler, partially due to CPU, partially due to M4 Max having *crazy high* memory bandwidth.

### Notes

- At least on *this* data set, OpenEXR with regular ZIP compression seems best.
- Upcoming HT256 compression in OpenEXR on this data is both *worse compression* than ZIP, and *worse performance* :(
	- Note that I am testing OpenEXR with [PR#2061](https://github.com/AcademySoftwareFoundation/openexr/pull/2061) applied;
      without it the performance on Windows is *way* worse.
- JPEG-XL at default compression effort level (7) does produce smaller files (EXR ZIP: 2.19x ratio, JXL 7: 2.42x ratio), but
  compression is *20x slower*, and decompression is *9x slower*.
  Which to me does not feel like a cost worth paying for a fairly small increase in compression ratio.
  - At lowest compression effort level (1) JXL produces compression ratio 2.04x (so worse than EXR ZIP), while still being
    3x slower at compression, and 4x slower at decompression.
  - However, a cumbersome part of JXL is that color channels need to be interleaved, and all the "other channels" need
    to be separate (planar). All my data is fully interleaved, so it costs some performance to arrange it as libjxl wants,
    both for compression and after decompression.
  - `libjxl` currently is not fully lossless on half-precision subnormal values ([#3881](https://github.com/libjxl/libjxl/issues/3881)).


### Files I am testing on

Uploaded separately, in order to not blow up Git repo size and/or my Github LFS billing.

| File | Resolution | Raw size | Notes |
|------|-----------:|---------:|-------|
|[Blender281.exr](https://aras-p.info/files/exr_files/Blender281.exr) 	| 3840x2160 |  63.3MB | RGBA half |
|[Blender35.exr](https://aras-p.info/files/exr_files/Blender35.exr) 	| 3840x2160 | 332.2MB | 18 channels, mixed half/float |
|[Blender40.exr](https://aras-p.info/files/exr_files/Blender40.exr) 	| 3840x2160 | 348.0MB | 15 channels, mixed half/float |
|[Blender41.exr](https://aras-p.info/files/exr_files/Blender41.exr) 	| 3840x2160 | 743.6MB | 37 channels, mixed half/float |
|[Blender43.exr](https://aras-p.info/files/exr_files/Blender43.exr) 	| 3840x2160 |  47.5MB | RGB half |
|[ph_brown_photostudio_02_8k.exr](https://aras-p.info/files/exr_files/ph_brown_photostudio_02_8k.exr) | 8192x4096 | 384.0MB | RGB float, from [polyhaven](https://polyhaven.com/a/brown_photostudio_02) |
|[ph_golden_gate_hills_4k.exr](https://aras-p.info/files/exr_files/ph_golden_gate_hills_4k.exr) | 4096x2048 | 128.0MB | RGBA float, from [polyhaven](https://polyhaven.com/a/golden_gate_hills) |

### External software

Obtained through cmake `FetchContent`:

- [OpenEXR](https://github.com/AcademySoftwareFoundation/openexr) - 2025 Jun, beta branch with HTJ2K support and with a PR that fixes Windows
  performance issue related to `realloc`.
- [libjxl](https://github.com/libjxl/libjxl) - 2025 Jun, rev a75b322e.

