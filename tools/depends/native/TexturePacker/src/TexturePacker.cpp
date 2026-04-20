/*
 *      Copyright (C) 2005-2014 Team XBMC
 *      http://kodi.tv
 *
 *  This Program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2, or (at your option)
 *  any later version.
 *
 *  This Program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with XBMC; see the file COPYING.  If not, see
 *  <http://www.gnu.org/licenses/>.
 *
 */

#ifdef TARGET_WINDOWS
#include <sys/types.h>
#define __STDC_FORMAT_MACROS
#include <cinttypes>
#define platform_stricmp _stricmp
#else
#include <inttypes.h>
#define platform_stricmp strcasecmp
#endif
#include <cerrno>
#include <dirent.h>
#include <map>

#include "guilib/XBTF.h"
#include "guilib/XBTFReader.h"

#include "DecoderManager.h"

#include "XBTFWriter.h"
#include "md5.h"
#include "cmdlineargs.h"
#include "ETC2Encoder.h"

#ifdef TARGET_WINDOWS
#define strncasecmp _strnicmp
#endif

#include <vector>

#include <lzo/lzo1x.h>
#include <sys/stat.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/wait.h>

#define FLAGS_USE_LZO     1
// pack textures with ETC1 (requires external packer, e.g. PVRTexToolCLI or etcpack)
#define FLAGS_USE_ETC1    2

#define DIR_SEPARATOR '/'

namespace
{

const char *GetFormatString(unsigned int format)
{
  switch (format)
  {
  case XB_FMT_DXT1:
    return "DXT1 ";
  case XB_FMT_DXT3:
    return "DXT3 ";
  case XB_FMT_DXT5:
    return "DXT5 ";
  case XB_FMT_DXT5_YCoCg:
    return "YCoCg";
  case XB_FMT_A8R8G8B8:
    return "ARGB ";
  case XB_FMT_A8:
    return "A8   ";
  case XB_FMT_ETC1:
    return "ETC1 ";
  default:
    return "?????";
  }
}

bool HasAlpha(unsigned char* argb, unsigned int width, unsigned int height)
{
  unsigned char* p = argb + 3; // offset of alpha
  for (unsigned int i = 0; i < 4 * width * height; i += 4)
  {
    if (p[i] != 0xff)
      return true;
  }
  return false;
}

void Usage()
{
  puts("Usage:");
  puts("  -help            Show this screen.");
  puts("  -input <dir>     Input directory. Default: current dir");
  puts("  -output <dir>    Output directory/filename. Default: Textures.xbt");
  puts("  -dupecheck       Enable duplicate file detection. Reduces output file size. Default: off");
  puts("  -etc1            Encode textures to ETC1 (high-quality internal encoder if available)");
}

} // namespace

class TexturePacker
{
public:
  TexturePacker() = default;
  ~TexturePacker() = default;

  void EnableDupeCheck() { m_dupecheck = true; }

  void EnableVerboseOutput() { decoderManager.EnableVerboseOutput(); }

  int createBundle(const std::string& InputDir, const std::string& OutputFile);

  void SetFlags(unsigned int flags) { m_flags = flags; }

private:
  void CreateSkeletonHeader(CXBTFWriter& xbtfWriter,
                            const std::string& fullPath,
                            const std::string& relativePath = "");

  CXBTFFrame CreateXBTFFrame(DecodedFrame& decodedFrame, CXBTFWriter& writer) const;

  bool CheckDupe(MD5Context* ctx, unsigned int pos);

  DecoderManager decoderManager;

  std::map<std::string, unsigned int> m_hashes;
  std::vector<unsigned int> m_dupes;

  bool m_dupecheck{false};
  unsigned int m_flags{0};
};

void TexturePacker::CreateSkeletonHeader(CXBTFWriter& xbtfWriter,
                                         const std::string& fullPath,
                                         const std::string& relativePath)
{
  struct dirent* dp;
  struct stat stat_p;
  DIR *dirp = opendir(fullPath.c_str());

  if (dirp)
  {
    for (errno = 0; (dp = readdir(dirp)); errno = 0)
    {
      if (strcmp(dp->d_name, ".") == 0 || strcmp(dp->d_name, "..") == 0)
      {
        continue;
      }

      //stat to check for dir type (reiserfs fix)
      std::string fileN = fullPath + "/" + dp->d_name;
      if (stat(fileN.c_str(), &stat_p) == 0)
      {
        if (dp->d_type == DT_DIR || stat_p.st_mode & S_IFDIR)
        {
          std::string tmpPath = relativePath;
          if (tmpPath.size() > 0)
          {
            tmpPath += "/";
          }

          CreateSkeletonHeader(xbtfWriter, fullPath + DIR_SEPARATOR + dp->d_name,
                               tmpPath + dp->d_name);
        }
        else if (decoderManager.IsSupportedGraphicsFile(dp->d_name))
        {
          std::string fileName = "";
          if (relativePath.size() > 0)
          {
            fileName += relativePath;
            fileName += "/";
          }

          fileName += dp->d_name;

          CXBTFFile file;
          file.SetPath(fileName);
          xbtfWriter.AddFile(file);
        }
      }
    }
    if (errno)
      fprintf(stderr, "Error reading directory %s (%s)\n", fullPath.c_str(), strerror(errno));

    closedir(dirp);
  }
  else
  {
    fprintf(stderr, "Error opening %s (%s)\n", fullPath.c_str(), strerror(errno));
  }
}

CXBTFFrame TexturePacker::CreateXBTFFrame(DecodedFrame& decodedFrame, CXBTFWriter& writer) const
{
  const unsigned int delay = decodedFrame.delay;
  const unsigned int width = decodedFrame.rgbaImage.width;
  const unsigned int height = decodedFrame.rgbaImage.height;
  const unsigned int size = width * height * 4;
  const XB_FMT format = XB_FMT_A8R8G8B8;
  unsigned char* data = (unsigned char*)decodedFrame.rgbaImage.pixels.data();

  const bool hasAlpha = HasAlpha(data, width, height);

  CXBTFFrame frame;
  lzo_uint packedSize = size;

  // If ETC1 packing is requested, try to pack this frame using an external tool.
  if ((m_flags & FLAGS_USE_ETC1) == FLAGS_USE_ETC1)
  {
    // Create temporary PPM (P6) file with RGB data (ETC1 doesn't support alpha)
    char inTemplate[] = "/tmp/texturepack_in_XXXXXX.ppm";
    int inFd = mkstemps(inTemplate, 4); // keeps .ppm
    if (inFd != -1)
    {
      FILE* inF = fdopen(inFd, "wb");
      if (inF)
      {
        fprintf(inF, "P6\n%d %d\n255\n", width, height);
        // write RGB bytes
        for (unsigned y = 0; y < (unsigned)height; ++y)
        {
          for (unsigned x = 0; x < (unsigned)width; ++x)
          {
            unsigned int idx = (y * width + x) * 4;
            unsigned char rgb[3] = { data[idx+2], data[idx+1], data[idx+0] }; // store as RGB
            fwrite(rgb, 1, 3, inF);
          }
        }
        fclose(inF);

        // Build output PKM temp name
        char outTemplate[] = "/tmp/texturepack_out_XXXXXX.pkm";
        int outFd = mkstemps(outTemplate, 4);
        if (outFd != -1)
        {
          close(outFd);

          // Try available packers in order: PVRTexToolCLI, etcpack, etc1tool
          std::string cmd;
          if (system("command -v PVRTexToolCLI >/dev/null 2>&1") == 0)
            cmd = std::string("PVRTexToolCLI -i \"") + inTemplate + "\" -o \"" + outTemplate + "\" -f ETC1";
          else if (system("command -v etcpack >/dev/null 2>&1") == 0)
            cmd = std::string("etcpack -c etc1 \"") + inTemplate + "\" \"" + outTemplate + "\"";
          else if (system("command -v etc1tool >/dev/null 2>&1") == 0)
            cmd = std::string("etc1tool \"") + inTemplate + "\" -o \"" + outTemplate + "\"";
          else
            cmd.clear();

          bool packed = false;
          // First try internal vendor encoder if available
          std::vector<uint8_t> pkm;
          if (EncodeETC1ToPKM((const uint8_t*)data, width, height, pkm))
          {
            // append PKM skipping header
            if (pkm.size() > 16)
            {
              writer.AppendContent(pkm.data() + 16, pkm.size() - 16);
              packedSize = static_cast<lzo_uint>(pkm.size() - 16);
              frame.SetPackedSize(packedSize);
              frame.SetUnpackedSize(size);
              frame.SetWidth(width);
              frame.SetHeight(height);
              frame.SetFormat(static_cast<XB_FMT>(XB_FMT_ETC1 | XB_FMT_OPAQUE));
              frame.SetDuration(delay);
              packed = true;
            }
          }
          // Fall back to external packer if internal encoder not available or failed
          if (!packed && !cmd.empty())
          {
            int ret = system(cmd.c_str());
            if (ret == 0)
            {
              // read PKM and append compressed content (skip PKM header 16 bytes)
              FILE* outF = fopen(outTemplate, "rb");
              if (outF)
              {
                fseek(outF, 0, SEEK_END);
                long outSize = ftell(outF);
                fseek(outF, 16, SEEK_SET);
                if (outSize > 16)
                {
                  long compSize = outSize - 16;
                  std::vector<uint8_t> comp;
                  comp.resize(compSize);
                  if (fread(comp.data(), 1, compSize, outF) == (size_t)compSize)
                  {
                    // append compressed PKM blocks to writer
                    writer.AppendContent(comp.data(), compSize);
                    packedSize = compSize;
                    frame.SetPackedSize(packedSize);
                    frame.SetUnpackedSize(size);
                    frame.SetWidth(width);
                    frame.SetHeight(height);
                    frame.SetFormat(static_cast<XB_FMT>(XB_FMT_ETC1 | XB_FMT_OPAQUE));
                    frame.SetDuration(delay);
                    fclose(outF);
                    packed = true;
                  }
                }
                fclose(outF);
              }
            }
          }

          // remove out file
          unlink(outTemplate);
          if (packed)
          {
            unlink(inTemplate);
            return frame;
          }
        }
        else
        {
          unlink(inTemplate);
        }
      }
      else
      {
        close(inFd);
      }
    }
    // if packing failed, fall through to regular handling
  }

  if ((m_flags & FLAGS_USE_LZO) == FLAGS_USE_LZO)
  {
    // grab a temporary buffer for unpacking into
    packedSize = size + size / 16 + 64 + 3; // see simple.c in lzo

    std::vector<uint8_t> packed;
    packed.resize(packedSize);

    std::vector<uint8_t> working;
    working.resize(LZO1X_999_MEM_COMPRESS);

    if (lzo1x_999_compress(data, size, packed.data(), &packedSize, working.data()) != LZO_E_OK ||
        packedSize > size)
    {
      // compression failed, or compressed size is bigger than uncompressed, so store as uncompressed
      packedSize = size;
      writer.AppendContent(data, size);
    }
    else
    { // success
      lzo_uint optimSize = size;
      if (lzo1x_optimize(packed.data(), packedSize, data, &optimSize, NULL) != LZO_E_OK ||
          optimSize != size)
      { //optimisation failed
        packedSize = size;
        writer.AppendContent(data, size);
      }
      else
      { // success
        writer.AppendContent(packed.data(), packedSize);
      }
    }
  }
  else
  {
    writer.AppendContent(data, size);
  }
  frame.SetPackedSize(packedSize);
  frame.SetUnpackedSize(size);
  frame.SetWidth(width);
  frame.SetHeight(height);
  frame.SetFormat(hasAlpha ? format : static_cast<XB_FMT>(format | XB_FMT_OPAQUE));
  frame.SetDuration(delay);
  return frame;
}

bool TexturePacker::CheckDupe(MD5Context* ctx,
                              unsigned int pos)
{
  unsigned char digest[17];
  MD5Final(digest,ctx);
  digest[16] = 0;
  char hex[33];
  snprintf(hex, sizeof(hex),
           "%02X%02X%02X%02X%02X%02X%02X%02X"
           "%02X%02X%02X%02X%02X%02X%02X%02X",
           digest[0], digest[1], digest[2], digest[3], digest[4], digest[5], digest[6], digest[7],
           digest[8], digest[9], digest[10], digest[11], digest[12], digest[13], digest[14],
           digest[15]);
  hex[32] = 0;
  std::map<std::string, unsigned int>::iterator it = m_hashes.find(hex);
  if (it != m_hashes.end())
  {
    m_dupes[pos] = it->second;
    return true;
  }

  m_hashes[hex] = pos;
  m_dupes[pos] = pos;

  return false;
}

int TexturePacker::createBundle(const std::string& InputDir, const std::string& OutputFile)
{
  CXBTFWriter writer(OutputFile);
  if (!writer.Create())
  {
    fprintf(stderr, "Error creating file\n");
    return 1;
  }

  CreateSkeletonHeader(writer, InputDir);

  std::vector<CXBTFFile> files = writer.GetFiles();
  m_dupes.resize(files.size());

  for (size_t i = 0; i < files.size(); i++)
  {
    struct MD5Context ctx;
    MD5Init(&ctx);
    CXBTFFile& file = files[i];

    std::string fullPath = InputDir;
    fullPath += file.GetPath();

    std::string output = file.GetPath();

    DecodedFrames frames;
    bool loaded = decoderManager.LoadFile(fullPath, frames);

    if (!loaded)
    {
      fprintf(stderr, "...unable to load image %s\n", file.GetPath().c_str());
      continue;
    }

    printf("%s\n", output.c_str());
    bool skip=false;
    if (m_dupecheck)
    {
      for (unsigned int j = 0; j < frames.frameList.size(); j++)
        MD5Update(&ctx, (const uint8_t*)frames.frameList[j].rgbaImage.pixels.data(),
                  frames.frameList[j].rgbaImage.height * frames.frameList[j].rgbaImage.pitch);

      if (CheckDupe(&ctx, i))
      {
        printf("****  duplicate of %s\n", files[m_dupes[i]].GetPath().c_str());
        file.GetFrames().insert(file.GetFrames().end(),
                                files[m_dupes[i]].GetFrames().begin(),
                                files[m_dupes[i]].GetFrames().end());
        skip = true;
      }
    }
    else
    {
      m_dupes[i] = i;
    }

    if (!skip)
    {
      for (unsigned int j = 0; j < frames.frameList.size(); j++)
      {
        CXBTFFrame frame = CreateXBTFFrame(frames.frameList[j], writer);
        file.GetFrames().push_back(frame);
        printf("    frame %4i (delay:%4i)                         %s%c (%d,%d @ %" PRIu64
               " bytes)\n",
               j, frame.GetDuration(), GetFormatString(frame.GetFormat()),
               frame.HasAlpha() ? ' ' : '*', frame.GetWidth(), frame.GetHeight(),
               frame.GetUnpackedSize());
      }
    }
    file.SetLoop(0);

    writer.UpdateFile(file);
  }

  if (!writer.UpdateHeader(m_dupes))
  {
    fprintf(stderr, "Error writing header to file\n");
    return 1;
  }

  if (!writer.Close())
  {
    fprintf(stderr, "Error closing file\n");
    return 1;
  }

  return 0;
}

int main(int argc, char* argv[])
{
  if (lzo_init() != LZO_E_OK)
    return 1;
  bool valid = false;

  CmdLineArgs args(argc, (const char**)argv);

  if (args.size() == 1)
  {
    Usage();
    return 1;
  }

  std::string InputDir;
  std::string OutputFilename = "Textures.xbt";

  TexturePacker texturePacker;
  unsigned int flags = FLAGS_USE_LZO;

  for (unsigned int i = 1; i < args.size(); ++i)
  {
    if (!platform_stricmp(args[i], "-help") || !platform_stricmp(args[i], "-h") || !platform_stricmp(args[i], "-?"))
    {
      Usage();
      return 1;
    }
    else if (!platform_stricmp(args[i], "-input") || !platform_stricmp(args[i], "-i"))
    {
      InputDir = args[++i];
      valid = true;
    }
    else if (!strcmp(args[i], "-dupecheck"))
    {
      texturePacker.EnableDupeCheck();
    }
    else if (!strcmp(args[i], "-verbose"))
    {
      texturePacker.EnableVerboseOutput();
    }
    else if (!strcmp(args[i], "-etc1"))
    {
      flags |= FLAGS_USE_ETC1;
    }
    else if (!platform_stricmp(args[i], "-output") || !platform_stricmp(args[i], "-o"))
    {
      OutputFilename = args[++i];
      valid = true;
#ifdef TARGET_POSIX
      char *c = NULL;
      while ((c = (char *)strchr(OutputFilename.c_str(), '\\')) != NULL) *c = '/';
#endif
    }
    else
    {
      fprintf(stderr, "Unrecognized command line flag: %s\n", args[i]);
    }
  }

  if (!valid)
  {
    Usage();
    return 1;
  }

  texturePacker.SetFlags(flags);

  size_t pos = InputDir.find_last_of(DIR_SEPARATOR);
  if (pos != InputDir.length() - 1)
    InputDir += DIR_SEPARATOR;

  texturePacker.createBundle(InputDir, OutputFilename);
}
