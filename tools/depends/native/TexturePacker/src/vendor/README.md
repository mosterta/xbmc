vendor/etc2comp

This directory is reserved for the etc2comp library sources.

How to add etc2comp:

1) Clone or copy the etc2comp source tree into `vendor/etc2comp/`.
   The tree should contain an `include/` directory and `src/` with .c/.cpp files.

2) Enable the vendor encoder in CMake when building TexturePacker:

   cmake -DUSE_VENDOR_ETC2COMP=ON <other args> ..

3) The build system will glob `vendor/etc2comp/src/*.c` and `*.cpp` and add
   them into the TexturePacker target. The encoder wrapper `ETC2Encoder.cpp`
   expects a vendor function `pack_etc1(const uint8_t* rgba, int w, int h,\
   std::vector<uint8_t>& outBlocks)` to be available. If the included library
   exposes a differently-named API, add a thin shim in `vendor/etc2comp/src/`
   matching that signature.

Notes:
- etc2comp is a high-quality ETC1/ETC2 encoder used in many projects. Check
  its license and comply with any requirements before bundling.
- If you prefer, you can instead implement a small shim C++ file in
  `vendor/etc2comp/src/` that adapts the library's API to the `pack_etc1`
  function signature expected by `ETC2Encoder.cpp`.
