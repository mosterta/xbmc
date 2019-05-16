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
/*  m_interop.glVDPAUGetMappedMemoryCedar = (PFGLVDPAUGETMAPPEDMEMORYCEDAR)GLNVGetProcAddress("glVDPAUGetMappedMemoryCedar");
  m_interop.glVDPAUCreateSurfaceCedar = (PFGLVDPAUDCREATESURFACECEDAR)GLNVGetProcAddress("glVDPAUCreateSurfaceCedar");
  m_interop.glVDPAUDestroySurfaceCedar = (PFGLVDPAUDESTROYSURFACECEDAR)GLNVGetProcAddress("glVDPAUDestroySurfaceCedar");
*/
  m_interop.glVDPAUGetVideoFrameConfig = (PFGLVDPAUGETVIDEOFRAMECCONFIG)GLNVGetProcAddress("glVDPAUGetVideoFrameConfig");

  if (!(m_interop.glVDPAUInitCedar || m_interop.glVDPAUFiniCedar || m_interop.glVDPAURegisterOutputSurfaceCedar ||
      m_interop.glVDPAURegisterVideoSurfaceCedar || m_interop.glVDPAUIsSurfaceCedar || m_interop.glVDPAUUnregisterSurfaceCedar ||
      m_interop.glVDPAUMapSurfacesCedar || m_interop.glVDPAUUnmapSurfacesCedar /* ||  m_interop.glVDPAUGetMappedMemoryCedar || 
        m_interop.glVDPAUCreateSurfaceCedar || m_interop.glVDPAUDestroySurfaceCedar */ || m_interop.glVDPAUGetVideoFrameConfig))
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

bool CVdpauTextureCedar::Map(CVdpauRenderPicture *pic, CHwLayerAdaptorVdpauAllwinner &hwAdaptor)
{
  if (m_vdpauPic)
    return true;

  m_vdpauPic = pic;
  m_vdpauPic->Acquire();

//  m_texWidth = pic->width;
//  m_texHeight = pic->height;

  bool success = false;
  if (m_vdpauPic->procPic.isYuv)
    success = MapNV12();
  else
    success = MapRGB();

  if (success)
  {
    hwAdaptor.setFrame(m_glSurface.glVdpauSurface);
  }
  else
  {
    m_vdpauPic->Release();
    m_vdpauPic = nullptr;
  }

  return success;
}

void CVdpauTextureCedar::Unmap()
{
  if (!m_vdpauPic)
    return;

  if (m_vdpauPic->procPic.isYuv)
    UnmapNV12();
  else
    UnmapRGB();

  m_vdpauPic->Release();
  m_vdpauPic = nullptr;
}

bool CVdpauTextureCedar::MapNV12()
{
  const void *videoSurface = reinterpret_cast<void*>(m_vdpauPic->procPic.videoSurface);
  m_glSurface.glVdpauSurface = m_interop.glVDPAURegisterVideoSurfaceCedar(videoSurface);
  m_interop.glVDPAUMapSurfacesCedar(1, &m_glSurface.glVdpauSurface);

  return true;
}

void CVdpauTextureCedar::UnmapNV12()
{
  m_interop.glVDPAUUnmapSurfacesCedar(1, &m_glSurface.glVdpauSurface);
  m_interop.glVDPAUUnregisterSurfaceCedar(m_glSurface.glVdpauSurface);
}

bool CVdpauTextureCedar::MapRGB()
{
  const void *outSurface = reinterpret_cast<void*>(m_vdpauPic->procPic.outputSurface);
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

