/*
 * Shim placeholder for pack_etc1
 *
 * Replace or edit this file when embedding etc2comp: implement `pack_etc1`
 * to call the actual encoder API from the bundled etc2comp sources.
 *
 * Expected signature:
 *   bool pack_etc1(const uint8_t* rgba, int width, int height, std::vector<uint8_t>& outBlocks)
 *
 * `outBlocks` should contain the raw ETC1 compressed blocks (no PKM header).
 */

#include <vector>
#include <cstdint>
#include <cstring>
#ifdef ETC2COMP_AVAILABLE
#include "Etc/Etc.h"
#include "Etc/EtcErrorMetric.h"
#include <memory>
#include <thread>
#include <algorithm>
#include <cstdio>
#endif

// If the bundled etc2comp provides a function you can adapt it here and call it.
// For now this shim returns false so the TexturePacker falls back to external
// packers when the internal encoder isn't available.

extern "C" bool pack_etc1(const uint8_t* rgba, int width, int height, std::vector<uint8_t>& outBlocks)
{
#ifndef ETC2COMP_AVAILABLE
  (void)rgba; (void)width; (void)height; (void)outBlocks;
  return false;
#else
  if (!rgba || width <= 0 || height <= 0)
    return false;

  const size_t pixels = static_cast<size_t>(width) * static_cast<size_t>(height);
  std::vector<float> src(pixels * 4);
  for (size_t i = 0; i < pixels; ++i)
  {
    src[i*4 + 0] = rgba[i*4 + 0] / 255.0f;
    src[i*4 + 1] = rgba[i*4 + 1] / 255.0f;
    src[i*4 + 2] = rgba[i*4 + 2] / 255.0f;
    src[i*4 + 3] = rgba[i*4 + 3] / 255.0f;
  }

  unsigned char* pEncoded = nullptr;
  unsigned int encodedBytes = 0;
  unsigned int extW = 0, extH = 0;
  int encodeTimeMs = 0;

  // Use a reasonable default error metric and effort level.
  Etc::ErrorMetric err = Etc::ErrorMetric::NUMERIC;
  float effort = ETCCOMP_DEFAULT_EFFORT_LEVEL;
  unsigned int hw = std::thread::hardware_concurrency();
  unsigned int jobs = hw > 0 ? std::min<unsigned int>(hw, 8u) : 1u;
  unsigned int maxJobs = jobs;

  bool ok = false;
  try
  {
    Etc::Encode(src.data(), static_cast<unsigned int>(width), static_cast<unsigned int>(height),
                Etc::Image::Format::ETC1, err, effort, jobs, maxJobs,
                &pEncoded, &encodedBytes, &extW, &extH, &encodeTimeMs, false);

    if (pEncoded && encodedBytes)
    {
      outBlocks.assign(pEncoded, pEncoded + encodedBytes);
      ok = true;
    }
  }
  catch (const std::exception& e)
  {
    std::fprintf(stderr, "etc2comp encode exception: %s\n", e.what());
    ok = false;
  }
  catch (...)
  {
    std::fprintf(stderr, "etc2comp encode unknown exception\n");
    ok = false;
  }

  if (pEncoded)
  {
    delete [] pEncoded;
  }

  return ok;
#endif
}
