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

//#define GL_NV_vdpau_sim_interop 1

#include "system.h"
#ifdef HAVE_LIBVDPAU
#include "HwBufferAllwinner.h"
#include <dlfcn.h>
#include "windowing/WindowingFactory.h"
#include "guilib/GraphicContext.h"
#include "guilib/TextureManager.h"
#include "cores/VideoPlayer/Process/ProcessInfo.h"
#include "cores/VideoPlayer/VideoRenderers/RenderManager.h"
#include "DVDVideoCodecFFmpeg.h"
#include "DVDClock.h"
#include "settings/Settings.h"
#include "settings/AdvancedSettings.h"
#include "settings/MediaSettings.h"
#include "Application.h"
#include "utils/MathUtils.h"
#include "utils/TimeUtils.h"
#include "DVDCodecs/DVDCodecUtils.h"
#include "cores/VideoPlayer/VideoRenderers/RenderFlags.h"
#include "utils/log.h"
#include "windowing/hwlayer/HwLayerFactory.h"

#if defined(GL_NV_vdpau_sim_interop)
#include <dlfcn.h>
#endif

extern "C" {
  #include <libavutil/imgutils.h>
  #include <libavutil/pixfmt.h>
}

using namespace Actor;
using namespace VDPAUAllwinner;

#define VDPAU_DEBUG 0

#define ARSIZE(X)  (sizeof(X))/(sizeof(X[0]))

#if defined(EGL_KHR_reusable_sync) && !defined(EGL_EGLEXT_PROTOTYPES)
static PFNEGLCREATESYNCKHRPROC eglCreateSyncKHR;
static PFNEGLDESTROYSYNCKHRPROC eglDestroySyncKHR;
static PFNEGLCLIENTWAITSYNCKHRPROC eglClientWaitSyncKHR;
static PFNEGLGETSYNCATTRIBKHRPROC eglGetSyncAttribKHR;
#endif

//-----------------------------------------------------------------------------
//-----------------------------------------------------------------------------

//void *CVDPAUContext::m_dlHandle = 0;

//-----------------------------------------------------------------------------
// CVDPAU
//-----------------------------------------------------------------------------

CCedarRender::CCedarRender()
{
  m_vdpauConfigured = false;
  GLInit();
}

bool CCedarRender::Create(AVCodecContext* avctx, AVCodecContext* mainctx, const enum AVPixelFormat fmt)
{
#if 1
  avctx->get_buffer2 = CCedarRender::FFGetBuffer;
  avctx->slice_flags = SLICE_FLAG_CODED_ORDER|SLICE_FLAG_ALLOW_FIELD;
  mainctx->get_buffer2 = CCedarRender::FFGetBuffer;
  mainctx->slice_flags = SLICE_FLAG_CODED_ORDER|SLICE_FLAG_ALLOW_FIELD;
#endif

  return true;
}

CCedarRender::~CCedarRender()
{
  Destroy();
}

void CCedarRender::Destroy()
{
  CLog::Log(LOGNOTICE, " (VDPAU) %s", __FUNCTION__);

//  g_Windowing.Unregister(this);

  m_vdpauConfig.Destroy();
  
  if(m_vdpProcs.m_dlVdpauNvHandle)
  {
     dlclose(m_vdpProcs.m_dlVdpauNvHandle);
     m_vdpProcs.m_dlVdpauNvHandle = NULL;
  }
}

int CCedarRender::FFGetBuffer(AVCodecContext *avctx, AVFrame *frame, int flags)
{
    int ret;

    if ((ret = update_frame_pool(avctx, frame)) < 0)
        return ret;

    switch (avctx->codec_type) {
    case AVMEDIA_TYPE_VIDEO:
        return video_get_buffer(avctx, frame);
    default:
        return -1;
    }
}

int CCedarRender::video_get_buffer(AVCodecContext *s, AVFrame *pic)
{
    CDVDVideoCodecFFmpeg* ctx        = (CDVDVideoCodecFFmpeg*)s->opaque;
    CCedarRender *vdp = (CCedarRender *)ctx->GetHwRenderManager();
    
    const AVPixFmtDescriptor *desc = av_pix_fmt_desc_get((AVPixelFormat)pic->format);
    int i;

    if (pic->data[0] || pic->data[1] || pic->data[2] || pic->data[3]) {
	CLog::Log(LOGERROR, "pic->data[*]!=NULL in avcodec_default_get_buffer\n");
        return -1;
    }

    if (!desc) {
        CLog::Log(LOGERROR,
            "Unable to get pixel format descriptor for format %s\n",
            av_get_pix_fmt_name((AVPixelFormat)pic->format));
        return -1;
    }

    memset(pic->data, 0, sizeof(pic->data));
    pic->extended_data = pic->data;

    memset(pic->buf, 0, sizeof(pic->buf));
    pic->buf[0] = av_buffer_pool_get(vdp->m_vdpauConfig.videoSurfacePool);
    if (!pic->buf[0])
	goto fail;

    void *buf[3];
    vdp->m_vdpProcs.glVDPAUGetMappedMemoryCedar(
				      (vdpauSurfaceCedar)pic->buf[0]->data, &buf[0],
				      &buf[1], &buf[2]);
#if VDPAU_DEBUG
    CLog::Log(LOGERROR, "CVDPAU::%s - (re-)used video surface=%d", __FUNCTION__, pic->buf[0]->data);
#endif

    for(int i=0; i < 3; ++i)
    {
      pic->data[i] = (uint8_t*)buf[i];

      pic->linesize[i] = vdp->m_vdpauConfig.linesize[i];
    }

    pic->linesize[3] = 0;

    pic->reordered_opaque= s->reordered_opaque;

    return 0;
fail:
    av_frame_unref(pic);
    return AVERROR(ENOMEM);
}

int CCedarRender::update_frame_pool(AVCodecContext *avctx, AVFrame *frame)
{
    CDVDVideoCodecFFmpeg* ctx        = (CDVDVideoCodecFFmpeg*)avctx->opaque;
    CCedarRender *vdp = (CCedarRender *)ctx->GetHwRenderManager();
    int i, ret;
    int stride_align[8];

    switch (avctx->codec_type) {
    case AVMEDIA_TYPE_VIDEO: {
        uint8_t *data[4];
	int linesize[4];
        int w = frame->width;
        int h = frame->height;
        int tmpsize, unaligned;

        if (vdp->m_vdpauConfig.format == frame->format &&
            vdp->m_vdpauConfig.vidWidth == frame->width && 
            vdp->m_vdpauConfig.vidHeight == frame->height)
            return 0;

	switch (frame->format) {
	case AV_PIX_FMT_YUV420P: 
	  vdp->m_vdpauConfig.chromaType = VDP_CHROMA_TYPE_420; 
	  vdp->m_vdpauConfig.ycbcrFormat = VDP_YCBCR_FORMAT_YV12;
	  break;
	case AV_PIX_FMT_YUV422P: 
	  vdp->m_vdpauConfig.chromaType = VDP_CHROMA_TYPE_422; 
	  vdp->m_vdpauConfig.ycbcrFormat = VDP_YCBCR_FORMAT_YV12;
	  break;
	case AV_PIX_FMT_YUV444P: 
	  vdp->m_vdpauConfig.chromaType = VDP_CHROMA_TYPE_444;
	  vdp->m_vdpauConfig.ycbcrFormat = VDP_YCBCR_FORMAT_YV12;
	  break;
	default:
	  CLog::Log(LOGERROR, " (VDPAU) %s: Unsupported data layout: %s", __FUNCTION__,
	    av_get_pix_fmt_name((AVPixelFormat)frame->format)
	  );
	    return -1;
	}

	avcodec_align_dimensions2(avctx, &w, &h, stride_align);

        do {
            // NOTE: do not align linesizes individually, this breaks e.g. assumptions
            // that linesize[0] == 2*linesize[1] in the MPEG-encoder for 4:2:2
            ret = av_image_fill_linesizes(linesize, avctx->pix_fmt, w);
            if (ret < 0)
                return ret;
            // increase alignment of w for next try (rhs gives the lowest bit set in w)
            w += w & ~(w - 1);

            unaligned = 0;
            for (i = 0; i < 4; i++)
                unaligned |= linesize[i] % stride_align[i];
        } while (unaligned);

	memcpy(vdp->m_vdpauConfig.linesize, linesize, sizeof(vdp->m_vdpauConfig.linesize));
	
	av_buffer_pool_uninit(&vdp->m_vdpauConfig.videoSurfacePool);
	vdp->m_vdpauConfig.videoSurfacePool = av_buffer_pool_init2(sizeof(VdpVideoSurface), vdp,
					      CCedarRender::pool_alloc, NULL);

	vdp->m_vdpauConfig.vidWidth = avctx->width;
	vdp->m_vdpauConfig.vidHeight = avctx->height;
	vdp->m_vdpauConfig.surfaceWidth = avctx->coded_width;
	vdp->m_vdpauConfig.surfaceHeight = avctx->coded_height;
	vdp->m_vdpauConfig.format = frame->format;

	break;
        }
    default: assert(0);
    }
    return 0;
fail:
    av_buffer_pool_uninit(&vdp->m_vdpauConfig.videoSurfacePool);
    return ret;
}

AVBufferRef *CCedarRender::pool_alloc(void *opaque, int size)
{
    CCedarRender             *vdp = (CCedarRender*)opaque;

    AVBufferRef *ret;
    VdpVideoSurface surf;
    VdpStatus vdp_st;
    
    vdp_st = vdp->m_vdpProcs.glVDPAUCreateSurfaceCedar(
                                         vdp->m_vdpauConfig.chromaType,
					 vdp->m_vdpauConfig.ycbcrFormat,
                                         vdp->m_vdpauConfig.surfaceWidth,
                                         vdp->m_vdpauConfig.surfaceHeight,
                                         &surf);
    vdp->CheckStatus(vdp_st, __LINE__);
    if (vdp_st != VDP_STATUS_OK)
    {
      CLog::Log(LOGERROR, "CVDPAU::FFGetBuffer - No Video surface available could be created");
      return NULL;
    }
    else
    {
#if VDPAU_DEBUG
       CLog::Log(LOGERROR, "CVDPAU::FFGetBuffer - Video surface=%d created", surf);
#endif
    }

    ret = av_buffer_create((uint8_t*)surf, 0, FFReleaseBuffer, vdp, 0);
    if (!ret) {
        vdp->m_vdpProcs.glVDPAUDestroySurfaceCedar(surf);
        return NULL;
    }

    return ret;
}

void CCedarRender::FFReleaseBuffer(void *opaque, uint8_t *data)
{
#if VDPAU_DEBUG
   CLog::Log(LOGNOTICE, " (VDPAU) %s", __FUNCTION__);
#endif
  VdpVideoSurface surf;
  CCedarRender             *vdp = (CCedarRender *)opaque;
  
  surf = (VdpVideoSurface)(uintptr_t)data;

  vdp->m_vdpProcs.glVDPAUDestroySurfaceCedar(surf);
  
#if VDPAU_DEBUG
  CLog::Log(LOGERROR, " (VDPAU) %s releasing video surface=%d", __FUNCTION__, surf);
#endif
}

bool CCedarRender::ClearPicture(DVDVideoPicture* pDvdVideoPicture)
{
  bool status = true;
  if(pDvdVideoPicture->cedarRender)
  {
    CHwRenderPicture *pic = (CHwRenderPicture *)pDvdVideoPicture->cedarRender->opaque;
    if(pic && !pic->added)
      ReleaseBuffer(pDvdVideoPicture->cedarRender, false);
  }
  memset(pDvdVideoPicture, 0, sizeof(*pDvdVideoPicture));
  return status;
}

bool CCedarRender::GetPicture(AVCodecContext* avctx, AVFrame* frame, DVDVideoPicture* picture)
{
  bool status = false;

  if(!picture->cedarRender)
    picture->cedarRender = av_frame_alloc();

  if(picture->cedarRender)
  {
    status = true;

    ((CDVDVideoCodecFFmpeg*)avctx->opaque)->GetPictureCommon(picture);
    av_frame_move_ref(picture->cedarRender, frame);
    picture->cedarRender->format         = frame->format;
    picture->cedarRender->width          = frame->width;
    picture->cedarRender->height         = frame->height;
    picture->cedarRender->channels       = frame->channels;
    picture->cedarRender->channel_layout = frame->channel_layout;
    picture->cedarRender->nb_samples     = frame->nb_samples;

    picture->format = RENDER_FMT_ALLWINNER_HWBUF;
    CHwRenderPicture *additionalData;
    picture->cedarRender->opaque = additionalData = new CHwRenderPicture;
    this->Acquire();
    additionalData->vdp = this;
    additionalData->usefence = true;
    additionalData->surfaceCedar = 0;
    additionalData->valid = true;
    additionalData->added = false;
    additionalData->Sync();
#if VDPAU_DEBUG
      CLog::Log(LOGERROR, " (VDPAU) %s GetPicture video surface=%d", __FUNCTION__, picture->cedarRender-> buf[0]->data);
#endif

  }
  return status;
}
bool CCedarRender::AcquireBuffer(void *frame)
{
    AVFrame *ref = (AVFrame*)frame;
    CHwRenderPicture *pic = (CHwRenderPicture *)ref->opaque;
  
    assert(pic);
    if(pic)
    {
      void* surf = (void*)ref->buf[0]->data;
      vdpauSurfaceCedar glVdpauSurfaceCedar = pic->vdp->m_vdpProcs.glVDPAURegisterVideoSurfaceCedar(surf);
      pic->vdp->m_vdpProcs.glVDPAUMapSurfacesCedar(1, &glVdpauSurfaceCedar);
      pic->surfaceCedar = glVdpauSurfaceCedar;
      pic->added = true;
#if VDPAU_DEBUG
      CLog::Log(LOGERROR, " (VDPAU) %s mapped video surface=%d to surface %d", __FUNCTION__, surf, glVdpauSurfaceCedar);
#endif
    }
    else
    {
      	CLog::Log(LOGERROR, " (VDPAU) %s pic==NULL", __FUNCTION__);
    }
    
    return true;
}

bool CCedarRender::ReleaseBuffer(void *frame, bool delete_frame)
{
    AVFrame *ref = (AVFrame*)frame;
    CHwRenderPicture *pic = (CHwRenderPicture *)ref->opaque;
    CCedarRender *cedar;
    
    assert(pic);
    if(pic)
    {
      if(pic->surfaceCedar)
      {
	//surface is unmapped internally first, if surface is still mapped, no need to call explicitly here
	pic->vdp->m_vdpProcs.glVDPAUUnregisterSurfaceCedar(pic->surfaceCedar);
#if VDPAU_DEBUG
	CLog::Log(LOGERROR, " (VDPAU) %s unmapped surface=%d", __FUNCTION__, pic->surfaceCedar);
#endif	
      }
      cedar = pic->vdp;
      delete pic;
      ref->opaque = NULL;
      if(delete_frame)
	av_frame_free(&ref);
      else
	av_frame_unref(ref);
      cedar->Release();
    }
    else
    {
      	CLog::Log(LOGERROR, " (VDPAU) %s pic==NULL", __FUNCTION__);
    }
    return true;
}

bool CCedarRender::CheckStatus(VdpStatus vdp_st, int line)
{
  if (vdp_st != VDP_STATUS_OK)
  {
    CLog::Log(LOGERROR, " (VDPAU) CCedarRender Error: (%d) at %s:%d\n", vdp_st, __FILE__, line);

    return true;
  }
  return false;
}

//-----------------------------------------------------------------------------
// RenderPicture
//-----------------------------------------------------------------------------

void CHwRenderPicture::Sync()
{
  if (usefence)
  {
    g_HwLayer.initSyncFence(CHwLayerManagerAW::HwLayerType::Video, fence);
  }
}

void* CCedarRender::GLNVGetProcAddress(const char * func)
{
  void *fAddr = NULL;
  
   if(m_vdpProcs.m_dlVdpauNvHandle)
      fAddr = dlsym(m_vdpProcs.m_dlVdpauNvHandle, func);
   return fAddr;
}

bool CCedarRender::GLInit()
{
#if defined(Cedar_vdpau_interop)
  m_vdpProcs.glVDPAUInitCedar = NULL;
  m_vdpProcs.glVDPAUFiniCedar = NULL;
  m_vdpProcs.glVDPAURegisterOutputSurfaceCedar = NULL;
  m_vdpProcs.glVDPAURegisterVideoSurfaceCedar = NULL;
  m_vdpProcs.glVDPAUIsSurfaceCedar = NULL;
  m_vdpProcs.glVDPAUUnregisterSurfaceCedar = NULL;
//  glVDPAUSurfaceAccessCedar = NULL;
  m_vdpProcs.glVDPAUMapSurfacesCedar = NULL;
  m_vdpProcs.glVDPAUUnmapSurfacesCedar = NULL;
//  glVDPAUGetSurfaceivCedar = NULL;
  m_vdpProcs.glVDPAUCreateSurfaceCedar = NULL;
  m_vdpProcs.glVDPAUDestroySurfaceCedar = NULL;
  m_vdpProcs.glVDPAUGetMappedMemoryCedar = NULL;
  m_vdpProcs.m_dlVdpauNvHandle = NULL;
#endif

#if defined(Cedar_vdpau_interop)
  const char VDPAU_MODULE_DIR[] = "/usr/lib";
  if(!m_vdpProcs.m_dlVdpauNvHandle)
  {
    char driver_path[255];
    snprintf(driver_path, 255, "%s/libcedarDisplay.so.1", VDPAU_MODULE_DIR);
    m_vdpProcs.m_dlVdpauNvHandle = dlopen(driver_path, RTLD_LAZY);
    if(!m_vdpProcs.m_dlVdpauNvHandle)
    {
      const char* error = dlerror();
      if (!error)
        error = "dlerror() returned NULL";
    }
  }
#endif
  
#if defined(Cedar_vdpau_interop)
  if (!m_vdpProcs.glVDPAUInitCedar)
    m_vdpProcs.glVDPAUInitCedar    = (PFNGLVDPAUINITCEDAR)GLNVGetProcAddress("glVDPAUInitCedar");
  if (!m_vdpProcs.glVDPAUFiniCedar)
    m_vdpProcs.glVDPAUFiniCedar = (PFNGLVDPAUFINICEDAR)GLNVGetProcAddress("glVDPAUFiniCedar");
  if (!m_vdpProcs.glVDPAURegisterOutputSurfaceCedar)
    m_vdpProcs.glVDPAURegisterOutputSurfaceCedar    = (PFNGLVDPAUREGISTEROUTPUTSURFACECEDAR)GLNVGetProcAddress("glVDPAURegisterOutputSurfaceCedar");
  if (!m_vdpProcs.glVDPAURegisterVideoSurfaceCedar)
    m_vdpProcs.glVDPAURegisterVideoSurfaceCedar    = (PFNGLVDPAUREGISTERVIDEOSURFACECEDAR)GLNVGetProcAddress("glVDPAURegisterVideoSurfaceCedar");
  if (!m_vdpProcs.glVDPAUIsSurfaceCedar)
    m_vdpProcs.glVDPAUIsSurfaceCedar    = (PFNGLVDPAUISSURFACECEDAR)GLNVGetProcAddress("glVDPAUIsSurfaceCedar");
  if (!m_vdpProcs.glVDPAUUnregisterSurfaceCedar)
    m_vdpProcs.glVDPAUUnregisterSurfaceCedar = (PFNGLVDPAUUNREGISTERSURFACECEDAR)GLNVGetProcAddress("glVDPAUUnregisterSurfaceCedar");
  if (!m_vdpProcs.glVDPAUMapSurfacesCedar)
    m_vdpProcs.glVDPAUMapSurfacesCedar = (PFNGLVDPAUMAPSURFACESCEDAR)GLNVGetProcAddress("glVDPAUMapSurfacesCedar");
  if (!m_vdpProcs.glVDPAUUnmapSurfacesCedar)
    m_vdpProcs.glVDPAUUnmapSurfacesCedar = (PFNGLVDPAUUNMAPSURFACESCEDAR)GLNVGetProcAddress("glVDPAUUnmapSurfacesCedar");
  if(!m_vdpProcs.glVDPAUGetMappedMemoryCedar)
    m_vdpProcs.glVDPAUGetMappedMemoryCedar = (PFGLVDPAUGETMAPPEDMEMORYCEDAR)GLNVGetProcAddress("glVDPAUGetMappedMemoryCedar");
  if(!m_vdpProcs.glVDPAUCreateSurfaceCedar)
    m_vdpProcs.glVDPAUCreateSurfaceCedar = (PFGLVDPAUDCREATESURFACECEDAR)GLNVGetProcAddress("glVDPAUCreateSurfaceCedar");
  if(!m_vdpProcs.glVDPAUDestroySurfaceCedar)
    m_vdpProcs.glVDPAUDestroySurfaceCedar = (PFGLVDPAUDESTROYSURFACECEDAR)GLNVGetProcAddress("glVDPAUDestroySurfaceCedar");
#endif

  m_vdpProcs.glVDPAUInitCedar(NULL, NULL);

  return true;
}

#endif
