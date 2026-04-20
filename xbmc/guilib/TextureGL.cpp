/*
 *  Copyright (C) 2005-2018 Team Kodi
 *  This file is part of Kodi - https://kodi.tv
 *
 *  SPDX-License-Identifier: GPL-2.0-or-later
 *  See LICENSES/README.md for more information.
 */

#include "TextureGL.h"

#include "ServiceBroker.h"
#include "guilib/TextureManager.h"
#include "rendering/RenderSystem.h"
#include "settings/AdvancedSettings.h"
#include "utils/GLUtils.h"
#include "utils/MemUtils.h"
#include "utils/log.h"

#include <memory>
#include <algorithm>
#include <cstring>
#include <cmath>
#include <cstdint>
#include <thread>
#include <vector>
#include <atomic>

// Define ETC1 token if headers don't provide it
#ifndef GL_ETC1_RGB8_OES
#define GL_ETC1_RGB8_OES 0x8D64
#endif

#define USE_STB_IMAGE_RESIZE 1
#define STB_IMAGE_RESIZE_IMPLEMENTATION 1
#include "guilib/stb_image_resize.h"

// Optional: use stb_image_resize for higher-quality and faster scaling when
// available. Define `USE_STB_IMAGE_RESIZE` and add stb implementation to build.
#ifdef USE_STB_IMAGE_RESIZE
extern "C" {
int stbir_resize_uint8(const unsigned char *input_pixels, int input_w, int input_h, int input_stride_in_bytes,
                       unsigned char *output_pixels, int output_w, int output_h, int output_stride_in_bytes,
                       int num_channels);
}
#endif

std::unique_ptr<CTexture> CTexture::CreateTexture(unsigned int width,
                                                  unsigned int height,
                                                  XB_FMT format)
{
  return std::make_unique<CGLTexture>(width, height, format);
}

CGLTexture::CGLTexture(unsigned int width, unsigned int height, XB_FMT format)
  : CTexture(width, height, format)
{
  unsigned int major, minor;
  CServiceBroker::GetRenderSystem()->GetRenderVersion(major, minor);
  if (major >= 3)
    m_isOglVersion3orNewer = true;
}

// Simple bilinear scaler for RGB/RGBA images. Returns newly allocated buffer
// (aligned) or nullptr on failure. Caller must free with KODI::MEMORY::AlignedFree.
static uint8_t* ScalePixelsBilinear(const uint8_t* src, unsigned srcW, unsigned srcH,
                                    unsigned dstW, unsigned dstH, unsigned bpp)
{
  if (!src || dstW == 0 || dstH == 0 || srcW == 0 || srcH == 0)
    return nullptr;

  const size_t dstPitch = static_cast<size_t>(dstW) * bpp;
  uint8_t* dst = static_cast<uint8_t*>(KODI::MEMORY::AlignedMalloc(dstPitch * dstH, 16));
  if (!dst)
    return nullptr;

#ifdef USE_STB_IMAGE_RESIZE
  // Try stb_image_resize first (faster/higher quality on many platforms).
  if (stbir_resize_uint8(reinterpret_cast<const unsigned char*>(src), static_cast<int>(srcW), static_cast<int>(srcH), 0,
                         reinterpret_cast<unsigned char*>(dst), static_cast<int>(dstW), static_cast<int>(dstH), 0,
                         static_cast<int>(bpp)) != 0)
  {
    return dst;
  }
  // fall through to CPU fallback if stb fails
#endif

  const float xRatio = static_cast<float>(srcW) / dstW;
  const float yRatio = static_cast<float>(srcH) / dstH;

  unsigned int threads = std::thread::hardware_concurrency();
  if (threads == 0)
    threads = 1;
  threads = std::min<unsigned int>(threads, dstH);

  std::vector<std::thread> workers;
  workers.reserve(threads);
  std::atomic<bool> failed(false);

  auto worker = [&](unsigned yStart, unsigned yEnd)
  {
    try
    {
      for (unsigned y = yStart; y < yEnd && !failed.load(std::memory_order_relaxed); ++y)
      {
        const float sy = y * yRatio;
        const int y0 = static_cast<int>(floorf(sy));
        const int y1 = std::min<int>(y0 + 1, srcH - 1);
        const float yf = sy - y0;

        for (unsigned x = 0; x < dstW; ++x)
        {
          const float sx = x * xRatio;
          const int x0 = static_cast<int>(floorf(sx));
          const int x1 = std::min<int>(x0 + 1, srcW - 1);
          const float xf = sx - x0;

          for (unsigned c = 0; c < bpp; ++c)
          {
            const uint8_t p00 = src[(y0 * srcW + x0) * bpp + c];
            const uint8_t p10 = src[(y0 * srcW + x1) * bpp + c];
            const uint8_t p01 = src[(y1 * srcW + x0) * bpp + c];
            const uint8_t p11 = src[(y1 * srcW + x1) * bpp + c];

            const float a = p00 * (1.0f - xf) + p10 * xf;
            const float b = p01 * (1.0f - xf) + p11 * xf;
            const float v = a * (1.0f - yf) + b * yf;

            dst[(y * dstW + x) * bpp + c] = static_cast<uint8_t>(v + 0.5f);
          }
        }
      }
    }
    catch (...) {
      failed.store(true, std::memory_order_relaxed);
    }
  };

  // split rows among threads
  unsigned base = dstH / threads;
  unsigned rem = dstH % threads;
  unsigned y = 0;
  for (unsigned t = 0; t < threads; ++t)
  {
    unsigned yStart = y;
    unsigned yCount = base + (t < rem ? 1u : 0u);
    y += yCount;
    unsigned yEnd = yStart + yCount;
    if (yCount == 0)
      continue;
    workers.emplace_back(worker, yStart, yEnd);
  }

  for (auto &th : workers)
    th.join();

  if (failed.load(std::memory_order_relaxed))
  {
    KODI::MEMORY::AlignedFree(dst);
    return nullptr;
  }

  return dst;
}

CGLTexture::~CGLTexture()
{
  DestroyTextureObject();
}

void CGLTexture::CreateTextureObject()
{
  glGenTextures(1, (GLuint*) &m_texture);
}

void CGLTexture::DestroyTextureObject()
{
  if (m_texture)
    CServiceBroker::GetGUI()->GetTextureManager().ReleaseHwTexture(m_texture);
}

void CGLTexture::LoadToGPU()
{
  if (!m_pixels)
  {
    // nothing to load - probably same image (no change)
    return;
  }
  if (m_texture == 0)
  {
    // Have OpenGL generate a texture object handle for us
    // this happens only one time - the first time the texture is loaded
    CreateTextureObject();
  }

  // Bind the texture object
  glBindTexture(GL_TEXTURE_2D, m_texture);

  GLenum filter = (m_scalingMethod == TEXTURE_SCALING::NEAREST ? GL_NEAREST : GL_LINEAR);

  // Set the texture's stretching properties
  if (IsMipmapped())
  {
    GLenum mipmapFilter = (m_scalingMethod == TEXTURE_SCALING::NEAREST ? GL_LINEAR_MIPMAP_NEAREST : GL_LINEAR_MIPMAP_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, mipmapFilter);

#ifndef HAS_GLES
    // Lower LOD bias equals more sharpness, but less smooth animation
    glTexParameterf(GL_TEXTURE_2D, GL_TEXTURE_LOD_BIAS, -0.5f);
    if (!m_isOglVersion3orNewer)
      glTexParameteri(GL_TEXTURE_2D, GL_GENERATE_MIPMAP, GL_TRUE);
#endif
  }
  else
  {
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, filter);
  }

  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, filter);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);

  unsigned int maxSize = CServiceBroker::GetRenderSystem()->GetMaxTextureSize();
  if (m_textureWidth > maxSize || m_textureHeight > maxSize)
  {
    // Prefer scaling down to fit into a single texture rather than truncating
    if ((m_format & XB_FMT_DXT_MASK) != 0)
    {
      // Compressed formats are not handled here — fall back to truncation
      CLog::Log(LOGERROR,
                "GL: Compressed image {}x{} too big, truncating to {}",
                m_textureWidth, m_textureHeight, maxSize);
      if (m_textureWidth > maxSize)
      {
#ifndef HAS_GLES
        glPixelStorei(GL_UNPACK_ROW_LENGTH, m_textureWidth);
#endif
        m_textureWidth = maxSize;
      }
      if (m_textureHeight > maxSize)
        m_textureHeight = maxSize;
    }
    else
    {
      unsigned int largest = std::max(m_textureWidth, m_textureHeight);
      float scale = static_cast<float>(maxSize) / static_cast<float>(largest);
      unsigned int newW = std::max(1u, static_cast<unsigned int>(m_textureWidth * scale));
      unsigned int newH = std::max(1u, static_cast<unsigned int>(m_textureHeight * scale));

      unsigned int bpp = (m_format == XB_FMT_RGB8) ? 3u : 4u;
      uint8_t* scaled = ScalePixelsBilinear(m_pixels, m_textureWidth, m_textureHeight, newW, newH, bpp);
      if (scaled)
      {
        CLog::Log(LOGINFO, "GL: Image {}x{} exceeds max {}; scaled to {}x{}",
                  m_textureWidth, m_textureHeight, maxSize, newW, newH);
        KODI::MEMORY::AlignedFree(m_pixels);
        m_pixels = scaled;
        m_textureWidth = newW;
        m_textureHeight = newH;
      }
      else
      {
        CLog::Log(LOGERROR, "GL: Failed to scale image; falling back to truncation");
#ifndef HAS_GLES
        glPixelStorei(GL_UNPACK_ROW_LENGTH, m_textureWidth);
#endif
        if (m_textureWidth > maxSize) m_textureWidth = maxSize;
        if (m_textureHeight > maxSize) m_textureHeight = maxSize;
      }
    }
  }

#ifndef HAS_GLES
  GLenum format = GL_BGRA;
  GLint numcomponents = GL_RGBA;

  switch (m_format)
  {
  case XB_FMT_DXT1:
    format = GL_COMPRESSED_RGBA_S3TC_DXT1_EXT;
    break;
  case XB_FMT_DXT3:
    format = GL_COMPRESSED_RGBA_S3TC_DXT3_EXT;
    break;
  case XB_FMT_DXT5:
  case XB_FMT_DXT5_YCoCg:
    format = GL_COMPRESSED_RGBA_S3TC_DXT5_EXT;
    break;
  case XB_FMT_RGB8:
    format = GL_RGB;
    numcomponents = GL_RGB;
    break;
  case XB_FMT_A8R8G8B8:
  default:
    break;
  }

  if ((m_format & XB_FMT_DXT_MASK) == 0)
  {
    glTexImage2D(GL_TEXTURE_2D, 0, numcomponents,
                 m_textureWidth, m_textureHeight, 0,
                 format, GL_UNSIGNED_BYTE, m_pixels);
  }
  else
  {
    glCompressedTexImage2D(GL_TEXTURE_2D, 0, format,
                           m_textureWidth, m_textureHeight, 0,
                           GetPitch() * GetRows(), m_pixels);
  }

  if (IsMipmapped() && m_isOglVersion3orNewer)
  {
    glGenerateMipmap(GL_TEXTURE_2D);
  }

  glPixelStorei(GL_UNPACK_ROW_LENGTH, 0);

#else	// GLES version

  // All incoming textures are BGRA, which GLES does not necessarily support.
  // Some (most?) hardware supports BGRA textures via an extension.
  // If not, we convert to RGBA first to avoid having to swizzle in shaders.
  // Explicitly define GL_BGRA_EXT here in the case that it's not defined by
  // system headers, and trust the extension list instead.
#ifndef GL_BGRA_EXT
#define GL_BGRA_EXT 0x80E1
#endif

  GLint internalformat;
  GLenum pixelformat;

  switch (m_format)
  {
    default:
    case XB_FMT_RGBA8:
      internalformat = pixelformat = GL_RGBA;
      break;
    case XB_FMT_RGB8:
      internalformat = pixelformat = GL_RGB;
      break;
    case XB_FMT_ETC1:
      internalformat = GL_ETC1_RGB8_OES; // compressed ETC1 format
      pixelformat = 0;
      break;
    case XB_FMT_A8R8G8B8:
      if (CServiceBroker::GetRenderSystem()->IsExtSupported("GL_EXT_texture_format_BGRA8888") ||
          CServiceBroker::GetRenderSystem()->IsExtSupported("GL_IMG_texture_format_BGRA8888"))
      {
        internalformat = pixelformat = GL_BGRA_EXT;
      }
      else if (CServiceBroker::GetRenderSystem()->IsExtSupported("GL_APPLE_texture_format_BGRA8888"))
      {
        // Apple's implementation does not conform to spec. Instead, they require
        // differing format/internalformat, more like GL.
        internalformat = GL_RGBA;
        pixelformat = GL_BGRA_EXT;
      }
      else
      {
        SwapBlueRed(m_pixels, m_textureHeight, GetPitch());
        internalformat = pixelformat = GL_RGBA;
      }
      break;
  }
  if (m_format == XB_FMT_ETC1)
  {
    // Ensure hardware supports ETC1 compressed textures
    if (!CServiceBroker::GetRenderSystem()->IsExtSupported("GL_OES_compressed_ETC1_RGB8_texture"))
    {
      CLog::Log(LOGERROR, "GL: ETC1 compressed textures not supported on this device");
      // fall back to uploading a blank texture to avoid GL errors
      glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, m_textureWidth, m_textureHeight, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
    }
    else
    {
      // Upload compressed ETC1 data
      GLsizei size = static_cast<GLsizei>(GetPitch() * GetRows());
      glCompressedTexImage2D(GL_TEXTURE_2D, 0, internalformat, m_textureWidth, m_textureHeight, 0, size, m_pixels);
    }
  }
  else
  {
    glTexImage2D(GL_TEXTURE_2D, 0, internalformat, m_textureWidth, m_textureHeight, 0,
      pixelformat, GL_UNSIGNED_BYTE, m_pixels);
  }

  if (IsMipmapped())
  {
    glGenerateMipmap(GL_TEXTURE_2D);
  }

#endif
  VerifyGLState();

  if (!m_bCacheMemory)
  {
    KODI::MEMORY::AlignedFree(m_pixels);
    m_pixels = NULL;
  }

  m_loadedToGPU = true;
}

void CGLTexture::BindToUnit(unsigned int unit)
{
  glActiveTexture(GL_TEXTURE0 + unit);
  glBindTexture(GL_TEXTURE_2D, m_texture);
}

