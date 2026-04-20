#include "ETC2Encoder.h"
#include <cstring>
#include <vector>
#include <cstdint>
#include "utils/log.h"

#ifdef ETC2COMP_AVAILABLE
// If vendor etc2comp was included into the build, include its headers and
// implement the wrapper to call its API. The vendor library is expected to
// provide a C++ API callable from here.

//#include "etc2comp.h" // vendor header path (depends on vendor layout)
extern "C" bool pack_etc1(const uint8_t* rgba, int width, int height, std::vector<uint8_t>& outBlocks);

bool EncodeETC1ToPKM(const uint8_t* rgba, int width, int height, std::vector<uint8_t>& outPKM)
{
  // This implementation depends on the vendor API. Adjust as needed to match
  // the included etc2comp version.
  try
  {
    // The vendor API usually provides a function to pack to ETC1/ETC2 blocks.
    // We'll use a hypothetical function "pack_etc1" which should be implemented
    // by the vendor sources. Replace with real API calls.
    std::vector<uint8_t> blocks;
    bool ok = pack_etc1(rgba, width, height, blocks); // vendor function
    if (!ok)
      return false;

    // Build PKM header (PKM 1.0)
    uint8_t header[16];
    memcpy(header, "PKM 10", 6); // magic + version
    // pad to 16 bytes properly
    memset(header + 6, ' ', 10);

    // PKM header fields are big-endian; ETC1 uses extended width/height in header fields 8..11 etc.
    int extWidth = ((width + 3) / 4) * 4;
    int extHeight = ((height + 3) / 4) * 4;

    header[8] = (extWidth >> 8) & 0xFF;
    header[9] = extWidth & 0xFF;
    header[10] = (extHeight >> 8) & 0xFF;
    header[11] = extHeight & 0xFF;
    header[12] = (width >> 8) & 0xFF;
    header[13] = width & 0xFF;
    header[14] = (height >> 8) & 0xFF;
    header[15] = height & 0xFF;

    outPKM.clear();
    outPKM.insert(outPKM.end(), header, header + 16);
    outPKM.insert(outPKM.end(), blocks.begin(), blocks.end());
    return true;
  }
  catch (...) {
    //CLog::Log(LOGERROR, "ETC2Encoder: vendor encoder threw exception");
    return false;
  }
}

#else

bool EncodeETC1ToPKM(const uint8_t* /*rgba*/, int /*width*/, int /*height*/, std::vector<uint8_t>& /*outPKM*/)
{
  // Vendor encoder not available in this build.
  return false;
}

#endif
