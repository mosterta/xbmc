/*
 *  Copyright (C) 2005-2018 Team Kodi
 *  This file is part of Kodi - https://kodi.tv
 *
 *  SPDX-License-Identifier: GPL-2.0-or-later
 *  See LICENSES/README.md for more information.
 */

#include "VDPAU_Sunxi.h"
#include "ServiceBroker.h"
#include <dlfcn.h>
#include "windowing/WinSystem.h"
#include "windowing/GraphicContext.h"
#include "guilib/TextureManager.h"
#include "cores/VideoPlayer/DVDCodecs/DVDFactoryCodec.h"
#include "cores/VideoPlayer/Process/ProcessInfo.h"
#include "cores/VideoPlayer/VideoRenderers/RenderManager.h"
#include "cores/VideoPlayer/Interface/Addon/TimingConstants.h"
#include "rendering/RenderSystem.h"
#include "cores/VideoPlayer/Process/allwinner/ProcessInfoSunxi.h"
#include "settings/lib/Setting.h"
#include "settings/AdvancedSettings.h"
#include "settings/Settings.h"
#include "settings/SettingsComponent.h"
#include "Application.h"
#include "utils/MathUtils.h"
#include "utils/TimeUtils.h"
#include "DVDCodecs/DVDCodecUtils.h"
#include "cores/VideoPlayer/VideoRenderers/RenderFlags.h"
#include "utils/log.h"

//#define VDPAU_DEBUG 1

using namespace VDPAUSunxi;

#define ARSIZE(x) (sizeof(x) / sizeof((x)[0]))

CDecoder::Desc decoder_profiles[] = {
{"MPEG1",        VDP_DECODER_PROFILE_MPEG1},
{"MPEG2_SIMPLE", VDP_DECODER_PROFILE_MPEG2_SIMPLE},
{"MPEG2_MAIN",   VDP_DECODER_PROFILE_MPEG2_MAIN},
{"H264_BASELINE",VDP_DECODER_PROFILE_H264_BASELINE},
{"H264_MAIN",    VDP_DECODER_PROFILE_H264_MAIN},
{"H264_HIGH",    VDP_DECODER_PROFILE_H264_HIGH},
{"VC1_SIMPLE",   VDP_DECODER_PROFILE_VC1_SIMPLE},
{"VC1_MAIN",     VDP_DECODER_PROFILE_VC1_MAIN},
{"VC1_ADVANCED", VDP_DECODER_PROFILE_VC1_ADVANCED},
{"MPEG4_PART2_ASP", VDP_DECODER_PROFILE_MPEG4_PART2_ASP},
#ifdef VDP_DECODER_PROFILE_HEVC_MAIN
{"HEVC_MAIN", VDP_DECODER_PROFILE_HEVC_MAIN},
#endif
};

//-----------------------------------------------------------------------------
//-----------------------------------------------------------------------------

CVDPAUContext *CVDPAUContext::m_context = 0;
CCriticalSection CVDPAUContext::m_section;
Display *CVDPAUContext::m_display = 0;
void *CVDPAUContext::m_dlHandle = 0;

CVDPAUContext::CVDPAUContext()
{
  m_context = 0;
  m_refCount = 0;
}

void CVDPAUContext::Release()
{
  CSingleLock lock(m_section);

  m_refCount--;
  if (m_refCount <= 0)
  {
    Close();
    delete this;
    m_context = 0;
  }
}

void CVDPAUContext::Close()
{
  CLog::Log(LOGNOTICE, "VDPAU::Close - closing decoder context");
  DestroyContext();
}

bool CVDPAUContext::EnsureContext(CVDPAUContext **ctx)
{
  CSingleLock lock(m_section);

  if (m_context)
  {
    m_context->m_refCount++;
    *ctx = m_context;
    return true;
  }

  m_context = new CVDPAUContext();
  *ctx = m_context;
  {
    CSingleLock gLock(CServiceBroker::GetWinSystem()->GetGfxContext());
    if (!m_context->LoadSymbols() || !m_context->CreateContext())
    {
      delete m_context;
      m_context = 0;
      *ctx = NULL;
      return false;
    }
  }

  m_context->m_refCount++;

  *ctx = m_context;
  return true;
}

VDPAU_procs& CVDPAUContext::GetProcs()
{
  return m_vdpProcs;
}

int CVDPAUContext::GetFeatureCount()
{
  return m_featureCount;
}

bool CVDPAUContext::LoadSymbols()
{
  if (!m_dlHandle)
  {
    m_dlHandle  = dlopen("libvdpau.so.1", RTLD_LAZY);
    if (!m_dlHandle)
    {
      const char* error = dlerror();
      if (!error)
        error = "dlerror() returned NULL";

      CLog::Log(LOGERROR,"VDPAU::LoadSymbols: Unable to get handle to lib: %s", error);
      return false;
    }
  }

  char* error;
  (void)dlerror();
  dl_vdp_device_create_x11 = (VdpStatus (*)(Display*, int, VdpDevice*, VdpStatus (**)(VdpDevice, VdpFuncId, void**)))dlsym(m_dlHandle, (const char*)"vdp_device_create_x11");
  error = dlerror();
  if (error)
  {
    CLog::Log(LOGERROR,"(VDPAU) - %s in %s",error,__FUNCTION__);
    m_vdpDevice = VDP_INVALID_HANDLE;
    return false;
  }
  return true;
}

bool CVDPAUContext::CreateContext()
{
  CLog::Log(LOGNOTICE,"VDPAU::CreateContext - creating decoder context");

  int screen=0;
  CSingleLock lock(CServiceBroker::GetWinSystem()->GetGfxContext());

  VdpStatus vdp_st;
  // Create Device
  vdp_st = dl_vdp_device_create_x11(m_display,
                                    screen,
                                   &m_vdpDevice,
                                   &m_vdpProcs.vdp_get_proc_address);

  CLog::Log(LOGNOTICE,"vdp_device = 0x%08x vdp_st = 0x%08x",m_vdpDevice,vdp_st);
  if (vdp_st != VDP_STATUS_OK)
  {
    CLog::Log(LOGERROR,"(VDPAU) unable to init VDPAU - vdp_st = 0x%x.  Falling back.",vdp_st);
    m_vdpDevice = VDP_INVALID_HANDLE;
    return false;
  }

  QueryProcs();
  SpewHardwareAvailable();
  return true;
}

void CVDPAUContext::QueryProcs()
{
  VdpStatus vdp_st;

#define VDP_PROC(id, proc) \
  do { \
    vdp_st = m_vdpProcs.vdp_get_proc_address(m_vdpDevice, id, (void**)&proc); \
    if (vdp_st != VDP_STATUS_OK) \
    { \
      CLog::Log(LOGERROR, "CVDPAUContext::GetProcs - failed to get proc id"); \
    } \
  } while(0);

  VDP_PROC(VDP_FUNC_ID_GET_ERROR_STRING                    , m_vdpProcs.vdp_get_error_string);
  VDP_PROC(VDP_FUNC_ID_DEVICE_DESTROY                      , m_vdpProcs.vdp_device_destroy);
  VDP_PROC(VDP_FUNC_ID_DECODER_CREATE                      , m_vdpProcs.vdp_decoder_create);
  VDP_PROC(VDP_FUNC_ID_DECODER_DESTROY                     , m_vdpProcs.vdp_decoder_destroy);
  VDP_PROC(VDP_FUNC_ID_DECODER_RENDER                      , m_vdpProcs.vdp_decoder_render);
  VDP_PROC(VDP_FUNC_ID_DECODER_QUERY_CAPABILITIES          , m_vdpProcs.vdp_decoder_query_caps);
  VDP_PROC(VDP_FUNC_ID_DECODER_GET_PARAMETERS              , m_vdpProcs.vdp_decoder_get_parameters);
#undef VDP_PROC
}

VdpDevice CVDPAUContext::GetDevice()
{
  return m_vdpDevice;
}

void CVDPAUContext::DestroyContext()
{
  if (!m_vdpProcs.vdp_device_destroy)
    return;

  m_vdpProcs.vdp_device_destroy(m_vdpDevice);
  m_vdpDevice = VDP_INVALID_HANDLE;
}

void CVDPAUContext::SpewHardwareAvailable()  //Copyright (c) 2008 Wladimir J. van der Laan  -- VDPInfo
{
  VdpStatus rv;
  CLog::Log(LOGNOTICE,"VDPAU Decoder capabilities:");
  CLog::Log(LOGNOTICE,"name          level macbs width height");
  CLog::Log(LOGNOTICE,"------------------------------------");
  for(const CDecoder::Desc& decoder_profile : decoder_profiles)
  {
    VdpBool is_supported = false;
    uint32_t max_level, max_macroblocks, max_width, max_height;
    rv = m_vdpProcs.vdp_decoder_query_caps(m_vdpDevice, decoder_profile.id,
                                &is_supported, &max_level, &max_macroblocks, &max_width, &max_height);
    if(rv == VDP_STATUS_OK && is_supported)
    {
      CLog::Log(LOGNOTICE,"%-16s %2i %5i %5i %5i\n", decoder_profile.name,
                max_level, max_macroblocks, max_width, max_height);
    }
  }
  m_featureCount = 0;
}

//-----------------------------------------------------------------------------
// CVDPAU
//-----------------------------------------------------------------------------

CDecoder::CDecoder(CProcessInfo& processInfo) :
  m_processInfo(processInfo)
{
  m_vdpauConfigured = false;
  m_DisplayState = VDPAU_OPEN;
  m_vdpauConfig.context = 0;
  m_vdpauConfig.processInfo = &m_processInfo;
  m_vdpauConfig.resetCounter = 0;
  
  m_interopState.Init(0,0,0);
  m_videoBufferPool = std::make_shared<CVideoBufferPoolSunxi>(m_interopState.GetInterop());
}

int CDecoder::FFGetBuffer(AVCodecContext *avctx, AVFrame *pic, int flags)
{
  ICallbackHWAccel* cb = static_cast<ICallbackHWAccel*>(avctx->opaque);
  CDecoder* vdp = static_cast<CDecoder*>(cb->GetHWAccel());
  std::shared_ptr<CVideoBufferPoolSunxi> pool = vdp->GetBufferPool(); //((CProcessInfoSunxi&)(vdp->m_processInfo)).GetBufferPool();
  return pool->FFGetBuffer(avctx, pic, flags);
}

bool CDecoder::Open(AVCodecContext* avctx, AVCodecContext* mainctx, const enum AVPixelFormat fmt)
{
  // this could be done better by querying actual hw capabilities
  // but since vdpau will be dropped anyway in v19, this should do
  if (avctx->sw_pix_fmt != AV_PIX_FMT_YUV420P &&
      avctx->sw_pix_fmt != AV_PIX_FMT_YUVJ420P)
    return false;

  // check if user wants to decode this format with VDPAU
  std::string gpuvendor = CServiceBroker::GetRenderSystem()->GetRenderVendor();
  std::transform(gpuvendor.begin(), gpuvendor.end(), gpuvendor.begin(), ::tolower);
  // nvidia is whitelisted despite for mpeg-4 we need to query user settings
  if ((gpuvendor.compare(0, 6, "nvidia") != 0)  || (avctx->codec_id == AV_CODEC_ID_MPEG4) || (avctx->codec_id == AV_CODEC_ID_H263))
  {
    std::map<AVCodecID, std::string> settings_map = {
      { AV_CODEC_ID_H263, CSettings::SETTING_VIDEOPLAYER_USEVDPAUMPEG4 },
      { AV_CODEC_ID_MPEG4, CSettings::SETTING_VIDEOPLAYER_USEVDPAUMPEG4 },
      { AV_CODEC_ID_WMV3, CSettings::SETTING_VIDEOPLAYER_USEVDPAUVC1 },
      { AV_CODEC_ID_VC1, CSettings::SETTING_VIDEOPLAYER_USEVDPAUVC1 },
      { AV_CODEC_ID_MPEG2VIDEO, CSettings::SETTING_VIDEOPLAYER_USEVDPAUMPEG2 },
    };
    auto entry = settings_map.find(avctx->codec_id);
    if (entry != settings_map.end())
    {
      const std::shared_ptr<CSettings> settings = CServiceBroker::GetSettingsComponent()->GetSettings();
      bool enabled = settings->GetBool(entry->second) && settings->GetSetting(entry->second)->IsVisible();
      if (!enabled)
        return false;
    }
  }

#if 0
  if (!CServiceBroker::GetRenderSystem()->IsExtSupported("GL_NV_vdpau_interop"))
  {
    CLog::Log(LOGNOTICE, "VDPAU::Open: required extension GL_NV_vdpau_interop not found");
    return false;
  }
#endif

  if (avctx->coded_width  == 0 ||
     avctx->coded_height == 0)
  {
    CLog::Log(LOGWARNING,"VDPAU::Open: no width/height available, can't init");
    return false;
  }
  m_vdpauConfig.numRenderBuffers = 5;
  m_vdpauConfig.timeOpened = CurrentHostCounter();

  if (!CVDPAUContext::EnsureContext(&m_vdpauConfig.context))
    return false;

  m_DisplayState = VDPAU_OPEN;
  m_vdpauConfigured = false;

  {
    VdpDecoderProfile profile = 0;

    // convert FFMPEG codec ID to VDPAU profile.
    bool found = ReadFormatOf(avctx->codec_id, profile, m_vdpauConfig.vdpChromaType);
    if(found)
    {
      VdpStatus vdp_st;
      VdpBool is_supported = false;
      uint32_t max_level, max_macroblocks, max_width, max_height;

      // query device capabilities to ensure that VDPAU can handle the requested codec
      vdp_st = m_vdpauConfig.context->GetProcs().vdp_decoder_query_caps(m_vdpauConfig.context->GetDevice(),
               profile, &is_supported, &max_level, &max_macroblocks, &max_width, &max_height);

      // test to make sure there is a possibility the codec will work
      if (CheckStatus(vdp_st, __LINE__))
      {
        CLog::Log(LOGERROR, "VDPAU::Open: error %s(%d) checking for decoder support", m_vdpauConfig.context->GetProcs().vdp_get_error_string(vdp_st), vdp_st);
        return false;
      }

      if(!is_supported)
      {
        CLog::Log(LOGWARNING,"VDPAU::Open: profile(%d) not supported.",
                  profile);
        return false;
      }
      if (max_width < (uint32_t) avctx->coded_width || max_height < (uint32_t) avctx->coded_height)
      {
        CLog::Log(LOGWARNING,"VDPAU::Open: requested picture dimensions (%i, %i) exceed hardware capabilities ( %i, %i).",
	                      avctx->coded_width, avctx->coded_height, max_width, max_height);
        return false;
      }

      if (!CDVDCodecUtils::IsVP3CompatibleWidth(avctx->coded_width))
        CLog::Log(LOGWARNING,"VDPAU::Open width %i might not be supported because of hardware bug", avctx->width);

      // attempt to create a decoder with this width/height, some sizes are not supported by hw
      vdp_st = m_vdpauConfig.context->GetProcs().vdp_decoder_create(m_vdpauConfig.context->GetDevice(), profile, avctx->coded_width, avctx->coded_height, 5, &m_vdpauConfig.vdpDecoder);

      if (CheckStatus(vdp_st, __LINE__))
      {
        CLog::Log(LOGERROR, "VDPAU::Open: error: %s(%d) checking for decoder support", m_vdpauConfig.context->GetProcs().vdp_get_error_string(vdp_st), vdp_st);
        return false;
      }

      m_vdpauConfig.context->GetProcs().vdp_decoder_destroy(m_vdpauConfig.vdpDecoder);
      CheckStatus(vdp_st, __LINE__);

      // finally setup ffmpeg
      memset(&m_hwContext, 0, sizeof(AVVDPAUContext));
      m_hwContext.render2 = CDecoder::Render;
      avctx->get_buffer2 = CDecoder::FFGetBuffer; //CDecoder::FFGetBuffer;
      avctx->slice_flags = SLICE_FLAG_CODED_ORDER|SLICE_FLAG_ALLOW_FIELD;
      avctx->hwaccel_context = &m_hwContext;

      CServiceBroker::GetWinSystem()->Register(this);
      m_avctx = mainctx;
      return true;
    }
  }
  return false;
}

CDecoder::~CDecoder()
{
  Close();
}

void CDecoder::Close()
{
  CLog::Log(LOGNOTICE, " (VDPAU) %s", __FUNCTION__);

  CServiceBroker::GetWinSystem()->Unregister(this);

  CSingleLock lock(m_DecoderSection);

  FiniVDPAUOutput();

  if (m_vdpauConfig.context)
    m_vdpauConfig.context->Release();
  m_vdpauConfig.context = 0;
}

long CDecoder::Release()
{
  // if ffmpeg holds any references, flush buffers
  if (m_avctx)
  {
    //avcodec_flush_buffers(m_avctx);
  }

  // check if we should do some pre-cleanup here
  // a second decoder might need resources
  if (m_vdpauConfigured == true)
  {
    CSingleLock lock(m_DecoderSection);
    CLog::Log(LOGNOTICE,"CVDPAU::Release pre-cleanup");
    while(! m_decodedPics.empty())
    {
      auto elem = m_decodedPics.front();
      m_decodedPics.pop();
      delete elem;
    }
  }
  return IHardwareDecoder::Release();
}

/*
long CDecoder::ReleasePicReference()
{
  return IHardwareDecoder::Release();
}
*/

void CDecoder::SetWidthHeight(int width, int height)
{
  m_vdpauConfig.outWidth = width;
  m_vdpauConfig.outHeight = height;
  CLog::Log(LOGDEBUG, LOGVIDEO, "CVDPAUSunxi::SetWidthHeight Setting OutWidth: %i OutHeight: %i", m_vdpauConfig.outWidth, m_vdpauConfig.outHeight);
}

void CDecoder::OnLostDisplay()
{
  CLog::Log(LOGNOTICE,"CVDPAUSunxi::OnLostDisplay event");

  int count = CServiceBroker::GetWinSystem()->GetGfxContext().exit();

  CSingleLock lock(m_DecoderSection);
//   FiniVDPAUOutput();
//   if (m_vdpauConfig.context)
//     m_vdpauConfig.context->Release();
//   m_vdpauConfig.context = 0;

  //m_DisplayState = VDPAU_LOST;
  lock.Leave();
  m_DisplayEvent.Reset();

  CServiceBroker::GetWinSystem()->GetGfxContext().restore(count);
}

void CDecoder::OnResetDisplay()
{
  CLog::Log(LOGNOTICE,"CVDPAUSunxi::OnResetDevice event");

  int count = CServiceBroker::GetWinSystem()->GetGfxContext().exit();

  CSingleLock lock(m_DecoderSection);
  if (m_DisplayState == VDPAU_LOST)
  {
    //m_DisplayState = VDPAU_RESET;
    lock.Leave();
    m_DisplayEvent.Set();
  }

  CServiceBroker::GetWinSystem()->GetGfxContext().restore(count);
}

CDVDVideoCodec::VCReturn CDecoder::Check(AVCodecContext* avctx)
{
  EDisplayState state;

  { CSingleLock lock(m_DecoderSection);
    state = m_DisplayState;
  }

  if (state == VDPAU_LOST)
  {
    CLog::Log(LOGNOTICE,"CVDPAU::Check waiting for display reset event");
    if (!m_DisplayEvent.WaitMSec(4000))
    {
      CLog::Log(LOGERROR, "CVDPAU::Check - device didn't reset in reasonable time");
      state = VDPAU_RESET;
    }
    else
    {
      CSingleLock lock(m_DecoderSection);
      state = m_DisplayState;
    }
  }
  if (state == VDPAU_RESET || state == VDPAU_ERROR)
  {
    CSingleLock lock(m_DecoderSection);

    avcodec_flush_buffers(avctx);
    FiniVDPAUOutput();
    if (m_vdpauConfig.context)
      m_vdpauConfig.context->Release();
    m_vdpauConfig.context = 0;

    if (CVDPAUContext::EnsureContext(&m_vdpauConfig.context))
    {
      m_DisplayState = VDPAU_OPEN;
      m_vdpauConfigured = false;
    }

    if (state == VDPAU_RESET)
      return CDVDVideoCodec::VC_FLUSHED;
    else
      return CDVDVideoCodec::VC_ERROR;
  }
  return CDVDVideoCodec::VC_NONE;
}

bool CDecoder::IsVDPAUFormat(AVPixelFormat format)
{
  if (format == AV_PIX_FMT_VDPAU)
    return true;
  else
    return false;
}

void CDecoder::FiniVDPAUOutput()
{
  if (!m_vdpauConfigured)
    return;

  CLog::Log(LOGNOTICE, " (VDPAU) %s", __FUNCTION__);

  // uninit output
  m_vdpauConfigured = false;

  VdpStatus vdp_st;

  vdp_st = m_vdpauConfig.context->GetProcs().vdp_decoder_destroy(m_vdpauConfig.vdpDecoder);
  if (CheckStatus(vdp_st, __LINE__))
    return;
  m_vdpauConfig.vdpDecoder = VDP_INVALID_HANDLE;
}

bool CDecoder::ReadFormatOf( AVCodecID codec
                           , VdpDecoderProfile &vdp_decoder_profile
                           , VdpChromaType     &vdp_chroma_type)
{
  bool found = true;
  
  switch (codec)
  {
    case AV_CODEC_ID_MPEG1VIDEO:
      vdp_decoder_profile = VDP_DECODER_PROFILE_MPEG1;
      vdp_chroma_type     = VDP_CHROMA_TYPE_420;
      break;
    case AV_CODEC_ID_MPEG2VIDEO:
      vdp_decoder_profile = VDP_DECODER_PROFILE_MPEG2_MAIN;
      vdp_chroma_type     = VDP_CHROMA_TYPE_420;
      break;
    case AV_CODEC_ID_H264:
      vdp_decoder_profile = VDP_DECODER_PROFILE_H264_HIGH;
      vdp_chroma_type     = VDP_CHROMA_TYPE_420;
      break;
#ifdef VDP_DECODER_PROFILE_HEVC_MAIN
    case AV_CODEC_ID_HEVC:
      vdp_decoder_profile = VDP_DECODER_PROFILE_HEVC_MAIN;
      vdp_chroma_type     = VDP_CHROMA_TYPE_420;
      break;
#endif
    case AV_CODEC_ID_WMV3:
      vdp_decoder_profile = VDP_DECODER_PROFILE_VC1_MAIN;
      vdp_chroma_type     = VDP_CHROMA_TYPE_420;
      break;
    case AV_CODEC_ID_VC1:
      vdp_decoder_profile = VDP_DECODER_PROFILE_VC1_ADVANCED;
      vdp_chroma_type     = VDP_CHROMA_TYPE_420;
      break;
    case AV_CODEC_ID_MPEG4:
      vdp_decoder_profile = VDP_DECODER_PROFILE_MPEG4_PART2_ASP;
      vdp_chroma_type     = VDP_CHROMA_TYPE_420;
      break;
    case AV_CODEC_ID_MSMPEG4V3:
      vdp_decoder_profile = VDP_DECODER_PROFILE_DIVX3_HOME_THEATER;
      vdp_chroma_type     = VDP_CHROMA_TYPE_420;
      break;
    default:
      vdp_decoder_profile = 0;
      vdp_chroma_type     = 0;
      found = false;
      break;
  }
  return found;
}

bool CDecoder::ConfigVDPAU(AVCodecContext* avctx, int ref_frames)
{
  FiniVDPAUOutput();

  VdpStatus vdp_st;
  VdpDecoderProfile vdp_decoder_profile;

  m_vdpauConfig.vidWidth = avctx->width;
  m_vdpauConfig.vidHeight = avctx->height;
  m_vdpauConfig.surfaceWidth = avctx->coded_width;
  m_vdpauConfig.surfaceHeight = avctx->coded_height;

  SetWidthHeight(avctx->width,avctx->height);

  CLog::Log(LOGNOTICE, " (VDPAU) screenWidth:%i vidWidth:%i surfaceWidth:%i",m_vdpauConfig.outWidth,m_vdpauConfig.vidWidth,m_vdpauConfig.surfaceWidth);
  CLog::Log(LOGNOTICE, " (VDPAU) screenHeight:%i vidHeight:%i surfaceHeight:%i",m_vdpauConfig.outHeight,m_vdpauConfig.vidHeight,m_vdpauConfig.surfaceHeight);

  ReadFormatOf(avctx->codec_id, vdp_decoder_profile, m_vdpauConfig.vdpChromaType);

  if (avctx->codec_id == AV_CODEC_ID_H264)
  {
    m_vdpauConfig.maxReferences = ref_frames;
    if (m_vdpauConfig.maxReferences > 16) m_vdpauConfig.maxReferences = 16;
    if (m_vdpauConfig.maxReferences < 5)  m_vdpauConfig.maxReferences = 5;
  }
  else if (avctx->codec_id == AV_CODEC_ID_HEVC)
  {
    // The DPB works quite differently in hevc and there isn't  a per-file max
    // reference number, so we force the maximum number (source: upstream ffmpeg)
    m_vdpauConfig.maxReferences = 16;
  }
  else
    m_vdpauConfig.maxReferences = 2;

  vdp_st = m_vdpauConfig.context->GetProcs().vdp_decoder_create(m_vdpauConfig.context->GetDevice(),
                              vdp_decoder_profile,
                              m_vdpauConfig.surfaceWidth,
                              m_vdpauConfig.surfaceHeight,
                              m_vdpauConfig.maxReferences,
                              &m_vdpauConfig.vdpDecoder);
  if (CheckStatus(vdp_st, __LINE__))
    return false;

  uint32_t adaptedSurfaceWidth;
  uint32_t adaptedSurfaceHeight;
  VdpDecoderProfile adaptedProfile;
  vdp_st = m_vdpauConfig.context->GetProcs().vdp_decoder_get_parameters(m_vdpauConfig.vdpDecoder,
                                           &adaptedProfile,
                                           &adaptedSurfaceWidth,
                                           &adaptedSurfaceHeight);
  if (CheckStatus(vdp_st, __LINE__))
    return false;
  if(adaptedSurfaceWidth != m_vdpauConfig.surfaceWidth || adaptedSurfaceHeight != m_vdpauConfig.surfaceHeight)
  {
    CLog::Log(LOGNOTICE, " (VDPAU) adapted surfaceWidth:%i",adaptedSurfaceWidth);
    CLog::Log(LOGNOTICE, " (VDPAU) adapted surfaceHeight:%i",adaptedSurfaceHeight);
    m_vdpauConfig.surfaceWidth = adaptedSurfaceWidth;
    m_vdpauConfig.surfaceHeight = adaptedSurfaceHeight;
  }
  // initialize output
  CSingleLock lock(CServiceBroker::GetWinSystem()->GetGfxContext());
  m_vdpauConfig.stats = &m_bufferStats;
  m_vdpauConfig.vdpau = this;
  m_bufferStats.Reset();
  
  m_vdpauConfigured = true;
  m_ErrorCount = 0;
  m_vdpauConfig.resetCounter++;
  return true;
}

int CDecoder::Render(struct AVCodecContext *s, struct AVFrame *src,
                     const VdpPictureInfo *info, uint32_t buffers_used,
                     const VdpBitstreamBuffer *buffers)
{
  ICallbackHWAccel* ctx = static_cast<ICallbackHWAccel*>(s->opaque);
  CDecoder* vdp = static_cast<CDecoder*>(ctx->GetHWAccel());

  // while we are waiting to recover we can't do anything
  CSingleLock lock(vdp->m_DecoderSection);

  if(vdp->m_DisplayState != VDPAU_OPEN)
    return -1;

  VdpStatus vdp_st;
  VdpVideoSurface surf = (VdpVideoSurface)(uintptr_t)src->buf[0]->data;

  uint32_t max_refs = 0;
  if(s->codec_id == AV_CODEC_ID_H264)
    max_refs = s->refs;

  if(vdp->m_vdpauConfig.vdpDecoder == VDP_INVALID_HANDLE
  || vdp->m_vdpauConfigured == false
  || vdp->m_vdpauConfig.maxReferences < max_refs)
  {
    if(!vdp->ConfigVDPAU(s, max_refs))
      return -1;
  }

  uint64_t startTime = CurrentHostCounter();
  uint16_t decoded, processed, rend;
  vdp->m_bufferStats.Get(decoded, processed, rend);
  CLog::Log(LOGDEBUG, " (VDPAUSunxi)::%s render surface:%i",__FUNCTION__, surf);
  vdp_st = vdp->m_vdpauConfig.context->GetProcs().vdp_decoder_render(vdp->m_vdpauConfig.vdpDecoder,
                                                                     surf, info, buffers_used, buffers);
  if (vdp->CheckStatus(vdp_st, __LINE__))
    return -1;

  uint64_t diff = CurrentHostCounter() - startTime;
  if (diff*1000/CurrentHostFrequency() > 30)
    CLog::Log(LOGDEBUG, LOGVIDEO, "CVDPAU::DrawSlice - VdpDecoderRender long decoding: %d ms, dec: %d, proc: %d, rend: %d", (int)((diff*1000)/CurrentHostFrequency()), decoded, processed, rend);

  return 0;
}

void CDecoder::SetCodecControl(int flags)
{
  m_codecControl = flags & (DVD_CODEC_CTRL_DRAIN | DVD_CODEC_CTRL_HURRY | DVD_CODEC_CTRL_NO_POSTPROC);
  if (m_codecControl & DVD_CODEC_CTRL_DRAIN)
    m_bufferStats.SetDraining(true);
  else
    m_bufferStats.SetDraining(false);
}

CDVDVideoCodec::VCReturn CDecoder::Decode(AVCodecContext *avctx, AVFrame *pFrame)
{
  CDVDVideoCodec::VCReturn result = Check(avctx);
  if (result != CDVDVideoCodec::VC_NONE)
    return result;

  CSingleLock lock(m_DecoderSection);

  if (!m_vdpauConfigured)
    return CDVDVideoCodec::VC_ERROR;

  if(pFrame)
  { // we have a new frame from decoder
    CVideoBufferSunxi* buffer = dynamic_cast<CVideoBufferSunxi*>(m_videoBufferPool->Get());
    assert(buffer);
    buffer->SetRef(pFrame);
    
/*    AVFrame *pic = av_frame_alloc();
    av_frame_move_ref(pic, pFrame);*/
    m_decodedPics.push(buffer);

    m_bufferStats.IncDecoded();
  }

  uint16_t decoded, processed, render;

  bool drain = (m_codecControl & DVD_CODEC_CTRL_DRAIN);

  m_bufferStats.Get(decoded, processed, render);
  // if all pics are drained, break the loop by setting VC_EOF
  if (drain && decoded <= 0)
    return CDVDVideoCodec::VC_EOF;

  uint64_t startTime = CurrentHostCounter();
  if (!drain && (decoded < 1))
  {
    return CDVDVideoCodec::VC_BUFFER;
  }
  else 
  {
    uint64_t diff = CurrentHostCounter() - startTime;
    m_bufferStats.SetParams(diff, m_codecControl);
    return CDVDVideoCodec::VC_PICTURE;
  }

  return CDVDVideoCodec::VC_ERROR;
}

bool CDecoder::GetPicture(AVCodecContext* avctx, VideoPicture* picture)
{
  if (picture->videoBuffer)
  {
    picture->videoBuffer->Release();
    picture->videoBuffer = nullptr;
  }

  CSingleLock lock(m_DecoderSection);

  if (m_DisplayState != VDPAU_OPEN)
    return false;

  //if(m_pFrame)
  //  av_frame_free(&m_pFrame);

  CVideoBufferSunxi* buffer = m_decodedPics.front();
  m_decodedPics.pop();
  m_bufferStats.DecDecoded();

  buffer->SetPictureParams(picture);

  //std::shared_ptr<CVideoBufferPoolSunxi> pool = ((CProcessInfoSunxi&)(m_processInfo)).GetBufferPool();
//   CVideoBufferSunxi* buffer = dynamic_cast<CVideoBufferSunxi*>(m_videoBufferPool->Get());
//   assert(buffer);
//   buffer->SetRef(m_pFrame);

  picture->videoBuffer = buffer;
  return true;
}

void CDecoder::Reset()
{
  CLog::Log(LOGNOTICE, " (VDPAUSunxi) Reseet");
  CSingleLock lock(m_DecoderSection);

  if (!m_vdpauConfigured)
    return;

  while(! m_decodedPics.empty())
  {
    auto elem = m_decodedPics.front();
    m_decodedPics.pop();
    delete elem;
  }
  m_bufferStats.Reset();
}

bool CDecoder::CanSkipDeint()
{
  return m_bufferStats.CanSkipDeint();
}

bool CDecoder::CheckStatus(VdpStatus vdp_st, int line)
{
  if (vdp_st != VDP_STATUS_OK)
  {
    CLog::Log(LOGERROR, " (VDPAU) Error: %s(%d) at %s:%d\n", m_vdpauConfig.context->GetProcs().vdp_get_error_string(vdp_st), vdp_st, __FILE__, line);

    m_ErrorCount++;

    if(m_DisplayState == VDPAU_OPEN)
    {
      if (vdp_st == VDP_STATUS_DISPLAY_PREEMPTED)
      {
        m_DisplayEvent.Reset();
        m_DisplayState = VDPAU_LOST;
      }
      else if (m_ErrorCount > 2)
        m_DisplayState = VDPAU_ERROR;
    }

    return true;
  }
  m_ErrorCount = 0;
  return false;
}

IHardwareDecoder* CDecoder::Create(CDVDStreamInfo &hint, CProcessInfo &processInfo, AVPixelFormat fmt)
 {
   if (CDecoder::IsVDPAUFormat(fmt) && CServiceBroker::GetSettingsComponent()->GetSettings()->GetBool(CSettings::SETTING_VIDEOPLAYER_USEVDPAU))
     return new VDPAUSunxi::CDecoder(*CProcessInfoSunxi::Create());

   return nullptr;
 }

void CDecoder::Register()
{
  CVDPAUContext *context;
  if (!CVDPAUContext::EnsureContext(&context))
    return;

  context->Release();

  CDVDFactoryCodec::RegisterHWAccel("vdpausunxi", CDecoder::Create);
  std::string gpuvendor = CServiceBroker::GetRenderSystem()->GetRenderVendor();
  std::transform(gpuvendor.begin(), gpuvendor.end(), gpuvendor.begin(), ::tolower);
  bool isNvidia = (gpuvendor.compare(0, 6, "nvidia") == 0);

  const std::shared_ptr<CSettings> settings = CServiceBroker::GetSettingsComponent()->GetSettings();
  settings->GetSetting(CSettings::SETTING_VIDEOPLAYER_USEVDPAU)->SetVisible(true);

  if (!isNvidia)
  {
    settings->GetSetting(CSettings::SETTING_VIDEOPLAYER_USEVDPAUMPEG4)->SetVisible(true);
    settings->GetSetting(CSettings::SETTING_VIDEOPLAYER_USEVDPAUVC1)->SetVisible(true);
    settings->GetSetting(CSettings::SETTING_VIDEOPLAYER_USEVDPAUMPEG2)->SetVisible(true);
    settings->GetSetting(CSettings::SETTING_VIDEOPLAYER_USEVDPAUMIXER)->SetVisible(true);
  }
}
