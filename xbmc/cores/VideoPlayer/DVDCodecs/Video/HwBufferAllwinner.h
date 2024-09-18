/*
 *      Copyright (C) 2005-2013 Team XBMC
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

/**
 * design goals:
 * - improve performance
 *   max out hw resources: e.g. make 1080p60 play on ION2
 *   allow advanced de-interlacing on ION
 *
 * - add vdpau/opengl interop
 *
 * - remove tight dependency to render thread
 *   prior design needed to hijack render thread in order to do
 *   gl interop functions. In particular this was a problem for
 *   init and clear down. Introduction of GL_NV_vdpau_interop
 *   increased the need to be independent from render thread
 *
 * - move to an actor based design in order to reduce the number
 *   of locks needed.
 */

#pragma once

#include "system_gl.h"
#define GLX_GLXEXT_PROTOTYPES
#include "DVDVideoCodec.h"
#include "DVDVideoCodecFFmpeg.h"
#include "libavcodec/vdpau.h"
#include "DVDVideoCodec.h"
#include "DVDVideoCodecFFmpeg.h"
#include "threads/CriticalSection.h"
#include "threads/SharedSection.h"
#include "settings/VideoSettings.h"
#include "guilib/DispResource.h"
#include "threads/Event.h"
#include "threads/Thread.h"
#include "guilib/Geometry.h"

extern "C" {
#include "libavutil/avutil.h"
#include "libavcodec/vdpau.h"
}

#define FULLHD_WIDTH                       1920
#define MAX_PIC_Q_LENGTH                   20 //for non-interop_yuv this controls the max length of the decoded pic to render completion Q

class CProcessInfo;
//#define GL_NV_vdpau_sim_interop 0
#define Cedar_vdpau_interop     1
#define ALLWINNER_FRAME_sync    1
//#define GL_OES_EGL_sync         0
#undef GL_NV_vdpau_interop 

#if defined(Cedar_vdpau_interop)

typedef uint32_t vdpauSurfaceCedar;
typedef void (GL_APIENTRYP PFNGLVDPAUINITCEDAR)(const void *vdpDevice, const void *getProcAddress);
typedef void (GL_APIENTRYP PFNGLVDPAUFINICEDAR)(void);
typedef vdpauSurfaceCedar (GL_APIENTRYP PFNGLVDPAUREGISTERVIDEOSURFACECEDAR) (const void *vdpSurface);
typedef vdpauSurfaceCedar (GL_APIENTRYP PFNGLVDPAUREGISTEROUTPUTSURFACECEDAR) (const void *vdpSurface);
typedef int (GL_APIENTRYP PFNGLVDPAUISSURFACECEDAR) (vdpauSurfaceCedar surface);
typedef void (GL_APIENTRYP PFNGLVDPAUUNREGISTERSURFACECEDAR) (vdpauSurfaceCedar surface);
typedef void (GL_APIENTRYP PFNGLVDPAUMAPSURFACESCEDAR) (GLsizei numSurfaces, const vdpauSurfaceCedar *surfaces);
typedef void (GL_APIENTRYP PFNGLVDPAUUNMAPSURFACESCEDAR) (GLsizei numSurfaces, const vdpauSurfaceCedar *surfaces);
typedef int (GL_APIENTRYP PFNGLVDPAUGETFRAMEIDCEDAR) (int hLayer, int dispFd);
typedef void (GL_APIENTRYP PFGLVDPAUCLOSEVIDEOLAYERCEDAR) (int hLayer, int dispFd);
typedef VdpStatus (GL_APIENTRYP PFGLVDPAUDESTROYSURFACECEDAR) (vdpauSurfaceCedar surface);
typedef VdpStatus (GL_APIENTRYP PFGLVDPAUDCREATESURFACECEDAR) (int chroma_type, VdpYCbCrFormat format, int width, int height, uint8_t align[3], vdpauSurfaceCedar *surfaces);
typedef VdpStatus (GL_APIENTRYP PFGLVDPAUGETMAPPEDMEMORYCEDAR) (vdpauSurfaceCedar surface, void** addrY, void** addrU, void** addrV);
#endif

namespace VDPAUAllwinner
{

//-----------------------------------------------------------------------------
// VDPAU data structs
//-----------------------------------------------------------------------------

class CCedarRender;

/**
 *  CVdpauConfig holds all configuration parameters needed by vdpau
 *  The structure is sent to the internal classes CMixer and COutput
 *  for init.
 */

struct VDPAU_procs
{
  PFGLVDPAUDESTROYSURFACECEDAR glVDPAUDestroySurfaceCedar;
  PFGLVDPAUDCREATESURFACECEDAR glVDPAUCreateSurfaceCedar;
  PFGLVDPAUGETMAPPEDMEMORYCEDAR glVDPAUGetMappedMemoryCedar;

  PFNGLVDPAUINITCEDAR glVDPAUInitCedar;
  PFNGLVDPAUFINICEDAR glVDPAUFiniCedar;
  PFNGLVDPAUREGISTEROUTPUTSURFACECEDAR glVDPAURegisterOutputSurfaceCedar;
  PFNGLVDPAUREGISTERVIDEOSURFACECEDAR glVDPAURegisterVideoSurfaceCedar;
  PFNGLVDPAUISSURFACECEDAR glVDPAUIsSurfaceCedar;
  PFNGLVDPAUUNREGISTERSURFACECEDAR glVDPAUUnregisterSurfaceCedar;
//  PFNGLVDPAUSURFACEACCESSCEDAR glVDPAUSurfaceAccessCedar;
  PFNGLVDPAUMAPSURFACESCEDAR glVDPAUMapSurfacesCedar;
  PFNGLVDPAUUNMAPSURFACESCEDAR glVDPAUUnmapSurfacesCedar;
//  PFNGLVDPAUGETSURFACEIVCEDAR glVDPAUGetSurfaceivCedar;
  void *m_dlVdpauNvHandle;

};

struct CVdpauConfig
{
  CVdpauConfig() : videoSurfacePool(NULL) {};
  ~CVdpauConfig() {
    Destroy();
  }
  void Destroy() {
    if(videoSurfacePool)
      av_buffer_pool_uninit(&videoSurfacePool);
  }
  int surfaceWidth;
  int surfaceHeight;
  int vidWidth;
  int vidHeight;
  int outWidth;
  int outHeight;
  int chromaType;
  int ycbcrFormat;
  int format;
  CCedarRender *vdpau;
  int linesize[4];
  AVBufferPool *videoSurfacePool;
};


/**
 * Ready to render textures
 * Sent from COutput back to CCedarRender
 * Objects are referenced by DVDVideoPicture and are sent
 * to renderer
 */
class CHwRenderPicture
{
  friend class CCedarRender;
public:
  void Sync();
  vdpauSurfaceCedar surfaceCedar;
  bool valid;
  bool added;
  CCedarRender *vdp;
  bool FenceEnabled() { return usefence; }
  int& GetFence() { return fence; }
private:
  int fence;
  bool usefence;
};

/**
 *  VDPAU main class
 */
class CCedarRender :
    public CDVDVideoCodecFFmpeg::IHwRenderManager
{

public:

  struct Desc
  {
    const char *name;
    uint32_t id;
    uint32_t aux; /* optional extra parameter... */
  };

  CCedarRender();
  virtual ~CCedarRender();
  
  virtual bool Create (AVCodecContext* avctx, AVCodecContext* mainctx, const enum AVPixelFormat);
  virtual void Destroy();
  virtual bool ClearPicture(DVDVideoPicture* pDvdVideoPicture);
  virtual bool GetPicture(AVCodecContext* avctx, AVFrame* frame, DVDVideoPicture* picture);
  virtual bool AcquireBuffer(void* frame);
  virtual bool ReleaseBuffer(void* frame, bool delete_frame=true);
  
  static void FFReleaseBuffer(void *opaque, uint8_t *data);
  static int FFGetBuffer(AVCodecContext *avctx, AVFrame *pic, int flags);

protected:
  static int video_get_buffer(AVCodecContext *s, AVFrame *pic);
  static int update_frame_pool(AVCodecContext *avctx, AVFrame *frame);
  static AVBufferRef *pool_alloc(void *opaque, int size);
  bool GLInit();
  void* GLNVGetProcAddress(const char * func);
  
  void SetWidthHeight(int width, int height);
  bool CheckStatus(VdpStatus vdp_st, int line);
  void FiniVDPAUOutput();
  bool ConfigVDPAU(AVCodecContext* avctx, int ref_frames);

  bool          m_vdpauConfigured;
  CVdpauConfig  m_vdpauConfig;
  //AVVDPAUContext m_hwContext;
  VDPAU_procs m_vdpProcs;
};

}
