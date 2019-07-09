/*
 *      Copyright (C) 2007-2017 Team XBMC
 *      http://xbmc.org
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

#include "VdpauCedar.h"
#include "cores/VideoPlayer/DVDCodecs/Video/VDPAU.h"
#include "utils/log.h"
#include "windowing/allwinner/hwlayer/HwLayerFactory.h"
#include "cores/VideoPlayer/Process/allwinner/ProcessInfoSunxi.h" 

#include <dlfcn.h>

using namespace VDPAU;

//-----------------------------------------------------------------------------
// interop state
//-----------------------------------------------------------------------------

void* CInteropStateCedar::GLNVGetProcAddress(const char * func)
{
  void *fAddr = NULL;
  
  if(m_interop.m_dlVdpauNvHandle)
    fAddr = dlsym(m_interop.m_dlVdpauNvHandle, func);
  return fAddr;
}

bool CInteropStateCedar::Init(void *device, void *procFunc, int64_t ident)
{
  m_device = device;
  m_procFunc = procFunc;
  m_ident = ident;

  if(!m_interop.m_dlVdpauNvHandle)
  {
    m_interop.m_dlVdpauNvHandle = dlopen("libcedarDisplay.so.1", RTLD_LAZY);
    if(!m_interop.m_dlVdpauNvHandle)
    {
      const char* error = dlerror();
      if (!error)
        error = "dlerror() returned NULL";
    }
  }

  m_interop.glVDPAUInitCedar    = (PFNGLVDPAUINITCEDAR)GLNVGetProcAddress("glVDPAUInitCedar");
  m_interop.glVDPAUFiniCedar = (PFNGLVDPAUFINICEDAR)GLNVGetProcAddress("glVDPAUFiniCedar");
  m_interop.glVDPAURegisterOutputSurfaceCedar    = (PFNGLVDPAUREGISTEROUTPUTSURFACECEDAR)GLNVGetProcAddress("glVDPAURegisterOutputSurfaceCedar");
  m_interop.glVDPAURegisterVideoSurfaceCedar    = (PFNGLVDPAUREGISTERVIDEOSURFACECEDAR)GLNVGetProcAddress("glVDPAURegisterVideoSurfaceCedar");
  m_interop.glVDPAUIsSurfaceCedar    = (PFNGLVDPAUISSURFACECEDAR)GLNVGetProcAddress("glVDPAUIsSurfaceCedar");
  m_interop.glVDPAUUnregisterSurfaceCedar = (PFNGLVDPAUUNREGISTERSURFACECEDAR)GLNVGetProcAddress("glVDPAUUnregisterSurfaceCedar");
  m_interop.glVDPAUMapSurfacesCedar = (PFNGLVDPAUMAPSURFACESCEDAR)GLNVGetProcAddress("glVDPAUMapSurfacesCedar");
  m_interop.glVDPAUUnmapSurfacesCedar = (PFNGLVDPAUUNMAPSURFACESCEDAR)GLNVGetProcAddress("glVDPAUUnmapSurfacesCedar");
  m_interop.glVDPAUGetVideoFrameConfig = (PFGLVDPAUGETVIDEOFRAMECCONFIG)GLNVGetProcAddress("glVDPAUGetVideoFrameConfig");

  m_interop.glVDPAUGetMappedMemoryCedar = (PFGLVDPAUGETMAPPEDMEMORYCEDAR)GLNVGetProcAddress("glVDPAUGetMappedMemoryCedar");
  m_interop.glVDPAUCreateSurfaceCedar = (PFGLVDPAUDCREATESURFACECEDAR)GLNVGetProcAddress("glVDPAUCreateSurfaceCedar");
  m_interop.glVDPAUDestroySurfaceCedar = (PFGLVDPAUDESTROYSURFACECEDAR)GLNVGetProcAddress("glVDPAUDestroySurfaceCedar");

  if (!(m_interop.glVDPAUInitCedar || m_interop.glVDPAUFiniCedar || m_interop.glVDPAURegisterOutputSurfaceCedar ||
      m_interop.glVDPAURegisterVideoSurfaceCedar || m_interop.glVDPAUIsSurfaceCedar || m_interop.glVDPAUUnregisterSurfaceCedar ||
      m_interop.glVDPAUMapSurfacesCedar || m_interop.glVDPAUUnmapSurfacesCedar || m_interop.glVDPAUGetVideoFrameConfig ||
      m_interop.glVDPAUGetMappedMemoryCedar || m_interop.glVDPAUCreateSurfaceCedar || m_interop.glVDPAUDestroySurfaceCedar))
  {
    CLog::Log(LOGERROR, "CInteropStateCedar::Init - loading symbols failed");
    return false;
  }

  m_interop.glVDPAUInitCedar(m_device, m_procFunc);

  CLog::Log(LOGINFO, "CInteropStateCedar::Init: vdpau gl interop initialized");

  return true;
}

void CInteropStateCedar::Finish()
{
  m_interop.glVDPAUFiniCedar();
  m_device = nullptr;
  m_procFunc = nullptr;
}

InteropInfoCedar &CInteropStateCedar::GetInterop()
{
  return m_interop;
}

bool CInteropStateCedar::NeedInit(void *device, void *procFunc, int64_t ident)
{
  if (m_device != device)
    return true;
  if (m_procFunc != procFunc)
    return true;
  if (m_ident != ident)
    return true;

  return false;
}

//-----------------------------------------------------------------------------
// textures
//-----------------------------------------------------------------------------

void CVdpauTextureCedar::Init(InteropInfoCedar &interop)
{
  m_interop = interop;
}

bool CVdpauTextureCedar::Map(CVideoBuffer /*CVdpauRenderPicture*/ *pic, CHwLayerAdaptorVdpauAllwinner &hwAdaptor)
{
  int surface = -1;
  bool success = true;

  if (m_vdpauPic)
    return true;

  m_vdpauPic = pic;
  m_vdpauPic->Acquire();

//  m_texWidth = pic->width;
//  m_texHeight = pic->height;

  CVdpauRenderPicture *buf = dynamic_cast<CVdpauRenderPicture*>(pic);
  if (buf)
  {
    m_isYuv = buf->procPic.isYuv;
    if( m_isYuv )
      surface = buf->procPic.videoSurface;
    else
      surface = buf->procPic.outputSurface;
  }
  else
  {
    CVideoBufferSunxi *bufSunxi = dynamic_cast<CVideoBufferSunxi*>(pic);
    if(bufSunxi)
    {
      switch(bufSunxi->GetFormat())
      {
      case AV_PIX_FMT_YUV420P:
      case AV_PIX_FMT_YUYV422:
      case AV_PIX_FMT_YUV422P:
      case AV_PIX_FMT_YUV444P:
        m_isYuv = true;
        break;
      case AV_PIX_FMT_RGB24:
      case AV_PIX_FMT_BGR24:
        m_isYuv = false;
        break;
      }
      surface = (int)bufSunxi->GetMemPtr();
    }
    else
      success = false;
  }
  if(success)
  {
    if (m_isYuv)
      success = MapNV12(surface);
    else
      success = MapRGB(surface);
  
    if (success)
    {
      hwAdaptor.setFrame(m_glSurface.glVdpauSurface);
    }
    else
    {
      m_vdpauPic->Release();
      m_vdpauPic = nullptr;
    }
  }
  return success;
}

void CVdpauTextureCedar::Unmap()
{
  if (!m_vdpauPic)
    return;

  if (m_isYuv)
    UnmapNV12();
  else
    UnmapRGB();

  m_vdpauPic->Release();
  m_vdpauPic = nullptr;
}

bool CVdpauTextureCedar::MapNV12(int surface)
{
  const void *videoSurface = reinterpret_cast<void*>(surface);
  m_glSurface.glVdpauSurface = m_interop.glVDPAURegisterVideoSurfaceCedar(videoSurface);
  m_interop.glVDPAUMapSurfacesCedar(1, &m_glSurface.glVdpauSurface);

  return true;
}

void CVdpauTextureCedar::UnmapNV12()
{
  m_interop.glVDPAUUnmapSurfacesCedar(1, &m_glSurface.glVdpauSurface);
  m_interop.glVDPAUUnregisterSurfaceCedar(m_glSurface.glVdpauSurface);
}

bool CVdpauTextureCedar::MapRGB(int surface)
{
  const void *outSurface = reinterpret_cast<void*>(surface);
  m_glSurface.glVdpauSurface = m_interop.glVDPAURegisterOutputSurfaceCedar(outSurface);
  m_interop.glVDPAUMapSurfacesCedar(1, &m_glSurface.glVdpauSurface);
  return true;
}

void CVdpauTextureCedar::UnmapRGB()
{
  m_interop.glVDPAUUnmapSurfacesCedar(1, &m_glSurface.glVdpauSurface);
  m_interop.glVDPAUUnregisterSurfaceCedar(m_glSurface.glVdpauSurface);
}

CInteropStateCedar::CInteropStateCedar(void) 
{ 
  memset(&m_interop, 0, sizeof(m_interop)); 
};

CVdpauTextureCedar::CVdpauTextureCedar(void) 
{ 
  memset(&m_interop, 0, sizeof(m_interop)); 
};
