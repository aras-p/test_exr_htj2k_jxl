### External software

- Vendored:
  - `src/rapidhash` - https://github.com/Nicoshev/rapidhash 1370c3f (2025 Jun)
- Obtained through cmake FetchContent:
  - [OpenEXR](https://github.com/AcademySoftwareFoundation/openexr) - 2025 May, beta branch with HTJ2K support
  - [libjxl](https://github.com/libjxl/libjxl) - 2025 Jun, rev a75b322e

### Files I am testing on
- [Blender281junkshop_half.exr](https://aras-p.info/files/exr_files/Blender281junkshop_half.exr) - 4K res RGBA, FP16, 28MB.
- [Blender41splash_many_mixed_channels.exr](https://aras-p.info/files/exr_files/Blender41splash_many_mixed_channels.exr) - 4K res, many channels (colors, normals, depth etc.),
  mixed FP16 and FP32, 403MB.

Uploaded separately, in order to not blow up Git repo size and/or my Github LFS billing:
