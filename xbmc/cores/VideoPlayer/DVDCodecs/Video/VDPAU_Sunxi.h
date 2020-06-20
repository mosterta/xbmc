/*
 *  Copyright (C) 2005-2018 Team Kodi
 *  This file is part of Kodi - https://kodi.tv
 *
 *  SPDX-License-Identifier: GPL-2.0-or-later
 *  See LICENSES/README.md for more information.
 */

#pragma once

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

#include "cores/VideoPlayer/DVDCodecs/Video/DVDVideoCodec.h"
#include "cores/VideoPlayer/Process/VideoBuffer.h"
#if defined(HAS_GLX)
#include <X11/Xlib.h>
#include <X11/Xutil.h>
#else
#include <EGL/eglplatform_fb.h>
#include <EGL/egl.h>
typedef void Display;
#endif
#include "threads/CriticalSection.h"
#include "threads/SharedSection.h"
#include "cores/VideoSettings.h"
#include "guilib/DispResource.h"
#include "threads/Event.h"
#include "threads/Thread.h"
#include "utils/ActorProtocol.h"
#include "utils/Geometry.h"
#include "cores/VideoPlayer/Process/allwinner/ProcessInfoSunxi.h"
#include <deque>
#include <list>
#include <map>
#include <vector>

extern "C" {
#include <libavutil/avutil.h>
#include <libavcodec/vdpau.h>
}

class CProcessInfo;

namespace VDPAUSunxi
{

/**
 * VDPAU interface to driver
 */

struct VDPAU_procs
{
  VdpGetProcAddress *                   vdp_get_proc_address;
  VdpDeviceDestroy *                    vdp_device_destroy;

  VdpVideoSurfaceCreate *               vdp_video_surface_create;
  VdpVideoSurfaceDestroy *              vdp_video_surface_destroy;

  VdpGetErrorString *                         vdp_get_error_string;

  VdpDecoderCreate *             vdp_decoder_create;
  VdpDecoderDestroy *            vdp_decoder_destroy;
  VdpDecoderRender *             vdp_decoder_render;
  VdpDecoderQueryCapabilities *  vdp_decoder_query_caps;
  VdpDecoderGetParameters *      vdp_decoder_get_parameters;

  VdpPreemptionCallbackRegister * vdp_preemption_callback_register;
};

//-----------------------------------------------------------------------------
// VDPAU data structs
//-----------------------------------------------------------------------------

class CDecoder;

/**
 * Buffer statistics used to control number of frames in queue
 */

class CVdpauBufferStats
{
public:
  uint16_t decodedPics;
  uint16_t processedPics;
  uint16_t renderPics;
  uint64_t latency;         // time decoder has waited for a frame, ideally there is no latency
  int codecFlags;
  bool canSkipDeint;
  bool draining;

  void IncDecoded() { CSingleLock l(m_sec); decodedPics++;}
  void DecDecoded() { CSingleLock l(m_sec); decodedPics--;}
  void IncProcessed() { CSingleLock l(m_sec); processedPics++;}
  void DecProcessed() { CSingleLock l(m_sec); processedPics--;}
  void IncRender() { CSingleLock l(m_sec); renderPics++;}
  void DecRender() { CSingleLock l(m_sec); renderPics--;}
  void Reset() { CSingleLock l(m_sec); decodedPics=0; processedPics=0;renderPics=0;latency=0;}
  void Get(uint16_t &decoded, uint16_t &processed, uint16_t &render) {CSingleLock l(m_sec); decoded = decodedPics, processed=processedPics, render=renderPics;}
  void SetParams(uint64_t time, int flags) { CSingleLock l(m_sec); latency = time; codecFlags = flags; }
  void GetParams(uint64_t &lat, int &flags) { CSingleLock l(m_sec); lat = latency; flags = codecFlags; }
  void SetCanSkipDeint(bool canSkip) { CSingleLock l(m_sec); canSkipDeint = canSkip; }
  bool CanSkipDeint() { CSingleLock l(m_sec); if (canSkipDeint) return true; else return false;}
  void SetDraining(bool drain) { CSingleLock l(m_sec); draining = drain; }
  bool IsDraining() { CSingleLock l(m_sec); if (draining) return true; else return false;}
private:
  CCriticalSection m_sec;
};

/**
 *  CVdpauConfig holds all configuration parameters needed by vdpau
 *  The structure is sent to the internal classes CMixer and COutput
 *  for init.
 */

class CVideoSurfaces;
class CVDPAUContext;

struct CVdpauConfig
{
  int surfaceWidth;
  int surfaceHeight;
  int vidWidth;
  int vidHeight;
  int outWidth;
  int outHeight;
  VdpDecoder vdpDecoder;
  VdpChromaType vdpChromaType;
  CVdpauBufferStats *stats;
  CDecoder *vdpau;
  int upscale;
  int numRenderBuffers;
  uint32_t maxReferences;
  bool useInteropYuv;
  CVDPAUContext *context;
  CProcessInfo *processInfo;
  int resetCounter;
  uint64_t timeOpened;
};

//-----------------------------------------------------------------------------
// VDPAU decoder
//-----------------------------------------------------------------------------

class CVDPAUContext
{
public:
  static bool EnsureContext(CVDPAUContext **ctx);
  void Release();
  VDPAU_procs& GetProcs();
  VdpDevice GetDevice();
  int GetFeatureCount();
private:
  CVDPAUContext();
  void Close();
  bool LoadSymbols();
  bool CreateContext();
  void DestroyContext();
  void QueryProcs();
  void SpewHardwareAvailable();
  static CVDPAUContext *m_context;
  static CCriticalSection m_section;
  static Display *m_display;
  int m_refCount;
  int m_featureCount;
  static void *m_dlHandle;
  VdpDevice m_vdpDevice;
  VDPAU_procs m_vdpProcs;
  VdpStatus (*dl_vdp_device_create_x11)(Display* display, int screen, VdpDevice* device, VdpGetProcAddress **get_proc_address);
};

/**
 *  VDPAU main class
 */
class CDecoder
 : public IHardwareDecoder
 , public IDispResource
{

public:

  struct Desc
  {
    const char *name;
    uint32_t id;
    uint32_t aux; /* optional extra parameter... */
  };

  explicit CDecoder(CProcessInfo& processInfo);
  ~CDecoder() override;

  bool Open (AVCodecContext* avctx, AVCodecContext* mainctx, const enum AVPixelFormat) override;
  CDVDVideoCodec::VCReturn Decode (AVCodecContext* avctx, AVFrame* frame) override;
  bool GetPicture(AVCodecContext* avctx, VideoPicture* picture) override;
  void Reset() override;
  virtual void Close();
  long Release() override;
  bool CanSkipDeint() override;
  unsigned GetAllowedReferences() override { return 5; }

  CDVDVideoCodec::VCReturn Check(AVCodecContext* avctx) override;
  const std::string Name() override { return "vdpausunxi"; }
  void SetCodecControl(int flags) override;

  static bool IsVDPAUFormat(AVPixelFormat fmt);

//  static void FFReleaseBuffer(void *opaque, uint8_t *data);
//  static int FFGetBuffer(AVCodecContext *avctx, AVFrame *pic, int flags);
  static int Render(struct AVCodecContext *s, struct AVFrame *src,
                    const VdpPictureInfo *info, uint32_t buffers_used,
                    const VdpBitstreamBuffer *buffers);

  void OnLostDisplay() override;
  void OnResetDisplay() override;

  static IHardwareDecoder* Create(CDVDStreamInfo &hint, CProcessInfo &processInfo, AVPixelFormat fmt);
  static void Register();

protected:
  void SetWidthHeight(int width, int height);
  bool ConfigVDPAU(AVCodecContext *avctx, int ref_frames);
  bool CheckStatus(VdpStatus vdp_st, int line);
  void FiniVDPAUOutput();
  void SetPictureParams(VideoPicture* pVideoPicture);
  static int FFGetBuffer(AVCodecContext *avctx, AVFrame *pic, int flags);

  static bool ReadFormatOf( AVCodecID codec
                          , VdpDecoderProfile &decoder_profile
                          , VdpChromaType &chroma_type);

  std::shared_ptr<CVideoBufferPoolSunxi> GetBufferPool()
  {
    return m_videoBufferPool;
  }

  // OnLostDevice triggers transition from all states to LOST
  // internal errors trigger transition from OPEN to RESET
  // OnResetDevice triggers transition from LOST to RESET
  enum EDisplayState
  { VDPAU_OPEN
  , VDPAU_RESET
  , VDPAU_LOST
  , VDPAU_ERROR
  } m_DisplayState;
  CCriticalSection m_DecoderSection;
  CEvent m_DisplayEvent;
  int m_ErrorCount;

  std::queue<AVFrame*> m_decodedPics;
  bool m_vdpauConfigured;
  CVdpauConfig m_vdpauConfig;
  AVVDPAUContext m_hwContext;
  AVCodecContext* m_avctx = nullptr;

  CVdpauBufferStats m_bufferStats;
  VDPAU::CInteropStateCedar m_interopState;

  std::shared_ptr<CVideoBufferPoolSunxi> m_videoBufferPool;
  AVFrame *m_pFrame;

  int m_codecControl;
  CProcessInfo& m_processInfo;
};

}
