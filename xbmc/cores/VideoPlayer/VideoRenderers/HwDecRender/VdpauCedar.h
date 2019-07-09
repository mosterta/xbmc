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

#pragma once

//#include <GLES/gl.h>
//#include <GLES/glext.h>

class CHwLayerAdaptorVdpauAllwinner;

#include "windowing/allwinner/hwlayer/HwLayerAdaptorVdpau.h"

struct videoFrameConfig;

namespace VDPAU
{
class CVdpauRenderPicture;

typedef uint32_t vdpauSurfaceCedar;

typedef void ( * PFNGLVDPAUINITCEDAR)(const void *vdpDevice, const void *getProcAddress);
typedef void ( * PFNGLVDPAUFINICEDAR)(void);
typedef vdpauSurfaceCedar ( * PFNGLVDPAUREGISTERVIDEOSURFACECEDAR) (const void *vdpSurface);
typedef vdpauSurfaceCedar ( * PFNGLVDPAUREGISTEROUTPUTSURFACECEDAR) (const void *vdpSurface);
typedef int ( * PFNGLVDPAUISSURFACECEDAR) (vdpauSurfaceCedar surface);
typedef void ( * PFNGLVDPAUUNREGISTERSURFACECEDAR) (vdpauSurfaceCedar surface);
typedef void ( * PFNGLVDPAUMAPSURFACESCEDAR) (int numSurfaces, const vdpauSurfaceCedar *surfaces);
typedef void ( * PFNGLVDPAUUNMAPSURFACESCEDAR) (int numSurfaces, const vdpauSurfaceCedar *surfaces);
typedef int ( * PFNGLVDPAUGETFRAMEIDCEDAR) (int hLayer, int dispFd);
typedef void ( * PFGLVDPAUCLOSEVIDEOLAYERCEDAR) (int hLayer, int dispFd);
typedef int (* PFGLVDPAUGETVIDEOFRAMECCONFIG)(vdpauSurfaceCedar surface,  struct videoFrameConfig *config);
typedef int (* PFGLVDPAUDESTROYSURFACECEDAR) (vdpauSurfaceCedar surface);
typedef int (* PFGLVDPAUDCREATESURFACECEDAR) (int chroma_type, VdpYCbCrFormat format, int width, int height, vdpauSurfaceCedar *surfaces);
typedef int (* PFGLVDPAUGETMAPPEDMEMORYCEDAR) (vdpauSurfaceCedar surface, void** addrY, void** addrU, void** addrV);

struct InteropInfoCedar
{
  PFNGLVDPAUINITCEDAR glVDPAUInitCedar;
  PFNGLVDPAUFINICEDAR glVDPAUFiniCedar;
  PFNGLVDPAUREGISTEROUTPUTSURFACECEDAR glVDPAURegisterOutputSurfaceCedar;
  PFNGLVDPAUREGISTERVIDEOSURFACECEDAR glVDPAURegisterVideoSurfaceCedar;
  PFNGLVDPAUISSURFACECEDAR glVDPAUIsSurfaceCedar;
  PFNGLVDPAUUNREGISTERSURFACECEDAR glVDPAUUnregisterSurfaceCedar;
//  PFNGLVDPAUSURFACEACCESSCEDAR glVDPAUSurfaceAccessCedar;
  PFNGLVDPAUMAPSURFACESCEDAR glVDPAUMapSurfacesCedar;
  PFNGLVDPAUUNMAPSURFACESCEDAR glVDPAUUnmapSurfacesCedar;
  PFGLVDPAUGETVIDEOFRAMECCONFIG glVDPAUGetVideoFrameConfig;
//  PFNGLVDPAUGETSURFACEIVCEDAR glVDPAUGetSurfaceivCedar;

  PFGLVDPAUDESTROYSURFACECEDAR glVDPAUDestroySurfaceCedar;
  PFGLVDPAUDCREATESURFACECEDAR glVDPAUCreateSurfaceCedar;
  PFGLVDPAUGETMAPPEDMEMORYCEDAR glVDPAUGetMappedMemoryCedar;

  void *m_dlVdpauNvHandle;
};

class CInteropStateCedar
{
public:
  CInteropStateCedar(void);
  bool Init(void *device, void *procFunc, int64_t ident);
  void Finish();
  InteropInfoCedar &GetInterop();
  bool NeedInit(void *device, void *procFunc, int64_t ident);

protected:
  void *m_device = nullptr;
  void *m_procFunc = nullptr;
  int64_t m_ident = 0;
  InteropInfoCedar m_interop;
  void* GLNVGetProcAddress(const char * func);
};

class CVdpauTextureCedar
{
public:
  CVdpauTextureCedar(void);
  bool Map(CVideoBuffer *pic, CHwLayerAdaptorVdpauAllwinner &hwAdaptor);
  void Unmap();
  void Init(InteropInfoCedar &interop);

protected:
  bool MapNV12(int surface);
  void UnmapNV12();
  bool MapRGB(int surface);
  void UnmapRGB();
  InteropInfoCedar m_interop;
  CVideoBuffer *m_vdpauPic = nullptr;
  bool m_isYuv = false;

  struct CedarSurface
  {
    vdpauSurfaceCedar glVdpauSurface;
  } m_glSurface;
};
}
