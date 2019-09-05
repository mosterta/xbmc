/*
 *  Copyright (C) 2017-2018 Team Kodi
 *  This file is part of Kodi - https://kodi.tv
 *
 *  SPDX-License-Identifier: GPL-2.0-or-later
 *  See LICENSES/README.md for more information.
 */

#include "RendererVDPAUSunxi.h"

#include "cores/VideoPlayer/VideoRenderers/RenderCapture.h"
#include "cores/VideoPlayer/VideoRenderers/RenderFactory.h"
#include "cores/VideoPlayer/VideoRenderers/RenderFlags.h"
#include "settings/lib/Setting.h"
#include "settings/DisplaySettings.h"
#include "settings/Settings.h"
#include "settings/SettingsComponent.h"
#include "utils/log.h"
#include "windowing/allwinner/WinSystemSunxi.h"
#include "windowing/GraphicContext.h"
#include "ServiceBroker.h"
#include "windowing/allwinner/hwlayer/HwLayerManagerAllwinner.h"
#include "utils/CPUInfo.h"

#include "cores/VideoPlayer/Process/allwinner/ProcessInfoSunxi.h"

CRendererVDPAUSunxi::CRendererVDPAUSunxi()
{
  //g_cpuInfo.setCPUMinFrequency(800);
  if(!m_vdpauAdaptor.initialize())
  {
    CLog::Log(LOGERROR, "CRendererVDPAUSunxi:%s initialize failed", __FUNCTION__);
  }
  m_lastRenderTime = XbmcThreads::SystemClockMillis();
}

CRendererVDPAUSunxi::~CRendererVDPAUSunxi()
{
  g_HwLayer.hideLayer(CHwLayerManagerAW::HwLayerType::Video);
  CHwLayerManagerAW::CPropertyValue prop(CHwLayerManagerAW::PropertyKey::ScalerType,
                                         CHwLayerManagerAW::ScalerType::Type_Normal);
  g_HwLayer.setProperty(CHwLayerManagerAW::HwLayerType::Video, prop);

  Flush(false);
  //g_cpuInfo.restoreCPUMinFrequency();
}

CBaseRenderer* CRendererVDPAUSunxi::Create(CVideoBuffer* buffer)
{
  if (buffer && 
      (dynamic_cast<VDPAU::CVdpauRenderPicture*>(buffer) || dynamic_cast<CVideoBufferSunxi*>(buffer)))
  {
#if 0
    CWinSystemGbm* winSystem = dynamic_cast<CWinSystemGbm*>(CServiceBroker::GetWinSystem());
    if (winSystem && winSystem->GetDrm()->GetVideoPlane()->plane &&
        std::dynamic_pointer_cast<CDRMAtomic>(winSystem->GetDrm()))
#endif
      return new CRendererVDPAUSunxi();
  }

  return nullptr;
}

void CRendererVDPAUSunxi::Register()
{
  CWinSystemSunxi* winSystem = dynamic_cast<CWinSystemSunxi*>(CServiceBroker::GetWinSystem());
  if (winSystem /* && winSystem->GetDrm()->GetVideoPlane()->plane && */
//      std::dynamic_pointer_cast<CDRMAtomic>(winSystem->GetDrm())
     )
  {
    VIDEOPLAYER::CRendererFactory::RegisterRenderer("vdpauAllwinner", CRendererVDPAUSunxi::Create);
    return;
  }
}

bool CRendererVDPAUSunxi::Configure(const VideoPicture& picture, float fps, unsigned int orientation)
{
  bool status;
  void* dev = NULL;
  void* procFunc = NULL;
  uint64_t ident = 0;

  m_format = picture.videoBuffer->GetFormat();
  m_sourceWidth = picture.iWidth;
  m_sourceHeight = picture.iHeight;
  m_renderOrientation = orientation;

  m_iFlags = GetFlagsChromaPosition(picture.chroma_position) |
             GetFlagsColorMatrix(picture.color_space, picture.iWidth, picture.iHeight) |
             GetFlagsColorPrimaries(picture.color_primaries) |
             GetFlagsStereoMode(picture.stereoMode);

  // Calculate the input frame aspect ratio.
  CalculateFrameAspectRatio(picture.iDisplayWidth, picture.iDisplayHeight);
  SetViewMode(m_videoSettings.m_ViewMode);
  ManageRenderArea();

  Flush(false);

  VDPAU::CVdpauRenderPicture *buffer = dynamic_cast<VDPAU::CVdpauRenderPicture*>(picture.videoBuffer);
  if( buffer )
  {
    dev = buffer->device;
    procFunc = buffer->procFunc;
    ident = buffer->ident;
  }

  status = m_interopState.Init(dev, procFunc, ident);
  if(! status )
    CLog::Log(LOGERROR, "CRendererVDPAUSunxi:%s error setting property scaler", __FUNCTION__);

  
  for (auto &buf : m_buffers)
  {
    g_HwLayer.initSyncFence(CHwLayerManagerAW::HwLayerType::Video, buf.fence);
    buf.texture.Init(m_interopState.GetInterop());
  }

  CHwLayerManagerAW::CPropertyValue prop(CHwLayerManagerAW::PropertyKey::ScalerType,
                                         CHwLayerManagerAW::ScalerType::Type_Scale);
  status = g_HwLayer.setProperty(CHwLayerManagerAW::HwLayerType::Video, prop);
  if(! status )
    CLog::Log(LOGERROR, "CRendererVDPAUSunxi:%s error setting property scaler", __FUNCTION__);

  status = g_HwLayer.showLayer(CHwLayerManagerAW::HwLayerType::Video);
  if(! status )
    CLog::Log(LOGERROR, "CRendererVDPAUSunxi:%s error calling showlayer", __FUNCTION__);

  m_lastRenderTime = XbmcThreads::SystemClockMillis();

  m_bConfigured = true;
  return true;
}

void CRendererVDPAUSunxi::ManageRenderArea()
{
  CBaseRenderer::ManageRenderArea();

  RESOLUTION_INFO info = CServiceBroker::GetWinSystem()->GetGfxContext().GetResInfo();
  if (info.iScreenWidth != info.iWidth)
  {
    CalcNormalRenderRect(0, 0, info.iScreenWidth, info.iScreenHeight,
                         GetAspectRatio() * CDisplaySettings::GetInstance().GetPixelRatio(),
                         CDisplaySettings::GetInstance().GetZoomAmount(),
                         CDisplaySettings::GetInstance().GetVerticalShift());
  }
}

void CRendererVDPAUSunxi::AddVideoPicture(const VideoPicture& picture, int index)
{
  BUFFER& buf = m_buffers[index];
  if (buf.videoBuffer)
  {
    CLog::LogF(LOGERROR, "unreleased video buffer");
    ReleaseBuffer(index);
  }
  buf.videoBuffer = picture.videoBuffer;
  buf.videoBuffer->Acquire();
}

bool CRendererVDPAUSunxi::Flush(bool saveBuffers)
{
  if (!saveBuffers)
    for (int i = 0; i < NUM_BUFFERS; i++)
      ReleaseBuffer(i);

  m_iLastRenderBuffer = -1;
  return saveBuffers;
}

void CRendererVDPAUSunxi::ReleaseBuffer(int index)
{
  BUFFER& buf = m_buffers[index];
  if (buf.videoBuffer)
  {
    if (buf.fence != -1)
    {
      g_HwLayer.destroySyncFence(CHwLayerManagerAW::HwLayerType::Video, buf.fence);
    }
    m_buffers[index].texture.Unmap();
    buf.videoBuffer->Release();
    buf.videoBuffer = nullptr;
  }
}

bool CRendererVDPAUSunxi::NeedBuffer(int index)
{
  bool needBuffer;

  if (m_iLastRenderBuffer == index)
    return true;

  CVideoBuffer* buffer = (m_buffers[index].videoBuffer);
  if (buffer && m_buffers[index].fence != -1)
  {
    CHwLayerManagerAW::HwLayerSyncValue value;
    bool status = g_HwLayer.getSyncFenceValue(CHwLayerManagerAW::HwLayerType::Video, m_buffers[index].fence, value);
    if(value == CHwLayerManagerAW::HwLayerSyncValue::HWLayerFenceSignaled)
    {
      g_HwLayer.destroySyncFence(CHwLayerManagerAW::HwLayerType::Video, m_buffers[index].fence);
      needBuffer = false;
    }
    else
      needBuffer = true;

    return needBuffer;
  }

  return false;
}

CRenderInfo CRendererVDPAUSunxi::GetRenderInfo()
{
  CRenderInfo info;
  info.max_buffer_size = NUM_BUFFERS;
  return info;
}

void CRendererVDPAUSunxi::Update()
{
  if (!m_bConfigured)
    return;

  ManageRenderArea();
}

void CRendererVDPAUSunxi::RenderUpdate(int index, int index2, bool clear, unsigned int flags, unsigned int alpha)
{
  bool status;
  
  if (m_iLastRenderBuffer == index)
  {
    //m_videoLayerBridge->UpdateVideoPlane();
    XbmcThreads::ThreadSleep(10);

    return;
  }

  CVideoBuffer* buffer = (m_buffers[index].videoBuffer);
  if(!buffer)
    return;

  if(!m_buffers[index].texture.Map(buffer, m_vdpauAdaptor))
    return;

  if (m_iLastRenderBuffer == -1) 
  {
    bool status;
    CHwLayerManagerAW::CPropertyValue prop(CHwLayerManagerAW::PropertyKey::ScalerType,
                                           CHwLayerManagerAW::ScalerType::Type_Scale);
    status = g_HwLayer.setProperty(CHwLayerManagerAW::HwLayerType::Video, prop);
    if(! status )
      CLog::Log(LOGERROR, "CRendererVDPAUSunxi:%s error setting property scaler", __FUNCTION__);

    CHwLayerManagerAW::CPropertyValue cs_prop(CHwLayerManagerAW::PropertyKey::ColorSpace, 0);
    switch(CONF_FLAGS_YUVCOEF_MASK(flags)) 
    {
      case CONF_FLAGS_YUVCOEF_BT709:
        cs_prop.setValue(CHwLayerManagerAW::ColorSpace::BT709);
        break;
      case CONF_FLAGS_YUVCOEF_BT601:
        cs_prop.setValue(CHwLayerManagerAW::ColorSpace::BT601);
        break;
      default:
        cs_prop.setValue(CHwLayerManagerAW::ColorSpace::BT709);
        break;
    }

    status = g_HwLayer.setProperty(CHwLayerManagerAW::HwLayerType::Video, cs_prop);
    
    CHwLayerManagerAW::CPropertyValue ilace(CHwLayerManagerAW::PropertyKey::InterlaceMode, 0);
    if (flags & (RENDER_FLAG_TOP|RENDER_FLAG_BOT))
      ilace.setValue(CHwLayerManagerAW::Interlace::IlaceOn);
    else
      ilace.setValue(CHwLayerManagerAW::Interlace::IlaceOff) ;

    status = g_HwLayer.setProperty(CHwLayerManagerAW::HwLayerType::Video, ilace);
    status = g_HwLayer.showLayer(CHwLayerManagerAW::HwLayerType::Video);
    if(! status )
      CLog::Log(LOGERROR, "CRendererVDPAUSunxi:%s error calling showlayer", __FUNCTION__);
  }

  status = g_HwLayer.configure(CHwLayerManagerAW::HwLayerType::Video, m_vdpauAdaptor, 
                                m_sourceRect, m_destRect);

  status = g_HwLayer.showLayer(CHwLayerManagerAW::HwLayerType::Video);
  if(! status )
    CLog::Log(LOGERROR, "CRendererVDPAUSunxi:%s error calling showlayer", __FUNCTION__);

  int top_field = (flags & RENDER_FLAG_TOP) == RENDER_FLAG_TOP;

  status = g_HwLayer.displayFrame(CHwLayerManagerAW::HwLayerType::Video, m_vdpauAdaptor, 
                                       m_buffers[index].fence, top_field);

  m_iLastRenderBuffer = index;

    // This code reduces rendering fps of the video layer when playing videos in fullscreen mode
    // it makes only sense on architectures with multiple layers
    m_fps = CServiceBroker::GetWinSystem()->GetGfxContext().GetFPS();
    int fps = m_fps * 2;

    unsigned int now = XbmcThreads::SystemClockMillis();
    unsigned int frameTime = now - m_lastRenderTime;
    if (fps > 0 && frameTime * fps < 1000)
      XbmcThreads::ThreadSleep(1000/fps - frameTime);

    m_lastRenderTime = now;

}

bool CRendererVDPAUSunxi::RenderCapture(CRenderCapture* capture)
{
  capture->BeginRender();
  capture->EndRender();
  return true;
}

bool CRendererVDPAUSunxi::ConfigChanged(const VideoPicture& picture)
{
  void* dev = NULL;
  void* procFunc = NULL;
  uint64_t ident = 0;

  if (picture.videoBuffer->GetFormat() != m_format)
    return true;

  VDPAU::CVdpauRenderPicture* buffer = dynamic_cast<VDPAU::CVdpauRenderPicture*>(picture.videoBuffer);
  if( buffer )
  {
    dev = buffer->device;
    procFunc = buffer->procFunc;
    ident = buffer->ident;
  }
  if (m_interopState.NeedInit(dev, procFunc, ident))
    return true;

  return false;
}

bool CRendererVDPAUSunxi::Supports(ERENDERFEATURE feature)
{
  if (feature == RENDERFEATURE_ZOOM ||
      feature == RENDERFEATURE_STRETCH ||
      feature == RENDERFEATURE_PIXEL_RATIO)
    return true;

  return false;
}

bool CRendererVDPAUSunxi::Supports(ESCALINGMETHOD method)
{
  return false;
}
