/*
 *      Copyright (C) 2007-2015 Team Kodi
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
 *  along with Kodi; see the file COPYING.  If not, see
 *  <http://www.gnu.org/licenses/>.
 *
 */

#include "RendererVDPAUAllwinner.h"

#if defined(HAVE_LIBVDPAU)
#include "cores/VideoPlayer/DVDCodecs/Video/VDPAU.h"
#include "utils/log.h"
#include "utils/GLUtils.h"
#include "windowing/WindowingFactory.h"
#include "cores/VideoPlayer/VideoRenderers/RenderCapture.h"
#include "cores/VideoPlayer/VideoRenderers/RenderFlags.h"
#include "windowing/hwlayer/HwLayerFactory.h"
#include "settings/Settings.h"
#include "settings/AdvancedSettings.h"

#define CS_MODE_BT709 1
#define CS_MODE_BT601 0

using namespace VDPAUAllwinner;

CRendererVDPAUAllwinner::CRendererVDPAUAllwinner():
    m_dlHandle(NULL), m_needReconfigure(false), m_lastRenderTime(0)
{  
  bool status = m_vdpauAdaptor.initialize();
  if(! status )
    CLog::Log(LOGERROR, "CRendererVDPAUAllwinner:%s error initializing vdpauAdaptor", __FUNCTION__);
}

CRendererVDPAUAllwinner::~CRendererVDPAUAllwinner()
{
  g_HwLayer.hideLayer(CHwLayerManagerAW::HwLayerType::Video);
  CHwLayerManagerAW::CPropertyValue prop(CHwLayerManagerAW::PropertyKey::ScalerType,
                                         CHwLayerManagerAW::ScalerType::Type_Normal);
  g_HwLayer.setProperty(CHwLayerManagerAW::HwLayerType::Video, prop);
  
  for (int i = 0; i < NUM_BUFFERS; ++i)
    DeleteTexture(i);

}

bool CRendererVDPAUAllwinner::RenderCapture(CRenderCapture* capture)
{
  CRect rect(0, 0, capture->GetWidth(), capture->GetHeight());

  return false;
}
bool CRendererVDPAUAllwinner::Configure(unsigned int width, unsigned int height, unsigned int d_width, unsigned int d_height, float fps, unsigned flags, ERenderFormat format, unsigned extended_formatunsigned, unsigned int orientation)
{
  bool status;
  CHwLayerManagerAW::CPropertyValue prop(CHwLayerManagerAW::PropertyKey::ScalerType,
                                         CHwLayerManagerAW::ScalerType::Type_Scale);
  status = g_HwLayer.setProperty(CHwLayerManagerAW::HwLayerType::Video, prop);
  if(! status )
    CLog::Log(LOGERROR, "CRendererVDPAUAllwinner:%s error setting property scaler", __FUNCTION__);

  status = g_HwLayer.showLayer(CHwLayerManagerAW::HwLayerType::Video);
  if(! status )
    CLog::Log(LOGERROR, "CRendererVDPAUAllwinner:%s error calling showlayer", __FUNCTION__);

  bool result = CLinuxRendererGLES::Configure(width, height, d_width, d_height, fps, 
                                              flags, format, extended_formatunsigned, orientation);
  m_needReconfigure = true;
  m_lastRenderTime = XbmcThreads::SystemClockMillis();
  return result;
}

void CRendererVDPAUAllwinner::AddVideoPictureHW(DVDVideoPicture &picture, int index)
{
  if(m_format == RENDER_FMT_VDPAU || m_format == RENDER_FMT_VDPAU_420)
  {
    VDPAU::CVdpauRenderPicture *vdpau = picture.vdpau;
    YUVBUFFER &buf = m_buffers[index];
    VDPAU::CVdpauRenderPicture *pic = vdpau->Acquire();
    if (buf.hwDec)
      ((VDPAU::CVdpauRenderPicture*)buf.hwDec)->Release();
    pic->Sync();
    buf.hwDec = pic;
    CLog::Log(LOGERROR, "CRendererVDPAUAllwinner::AddVideoPictureHW: vdpau:%X pic:%X index:%d", vdpau, pic, index);
  }
  else
  {
    YUVBUFFER &buf = m_buffers[index];
    if (buf.hwDec)
      ReleaseBuffer(index);
    CHwRenderPicture *pic = (CHwRenderPicture *)picture.cedarRender->opaque;
    pic->vdp->AcquireBuffer(picture.cedarRender);
    pic->Sync();
    buf.hwDec = (void*)picture.cedarRender;
    CLog::Log(LOGERROR, "CRendererVDPAUAllwinner::AddVideoPictureHW non VDPAU: pic:%X index:%d", pic, index);
  }
}

void CRendererVDPAUAllwinner::ReleaseBuffer(int idx)
{
  if(m_format == RENDER_FMT_VDPAU || m_format == RENDER_FMT_VDPAU_420)
  {
    YUVBUFFER &buf = m_buffers[idx];
    if (buf.hwDec) {
      VDPAU::CVdpauRenderPicture* pic = (VDPAU::CVdpauRenderPicture*)buf.hwDec;
      pic->Release();
      buf.hwDec = NULL;
    }
  }
  else
  {
    YUVBUFFER &buf = m_buffers[idx];

    if (buf.hwDec && buf.fields[0].id != 0) {
      CHwRenderPicture *pic = (CHwRenderPicture *)((AVFrame*)buf.hwDec)->opaque;
      bool status = g_HwLayer.destroySyncFence(CHwLayerManagerAW::HwLayerType::Video, pic->GetFence());
      pic->vdp->ReleaseBuffer(buf.hwDec);
      buf.hwDec = NULL;
    }
  }
}

int CRendererVDPAUAllwinner::GetImageHook(YV12Image *image, int source, bool readonly)
{
  return source;
}

bool CRendererVDPAUAllwinner::IsGuiLayer()
{
  return false;
}

bool CRendererVDPAUAllwinner::Supports(EINTERLACEMETHOD method)
{
  if(method == VS_INTERLACEMETHOD_AUTO)
    return true;

  if(method == VS_INTERLACEMETHOD_RENDER_BLEND ||
     method == VS_INTERLACEMETHOD_RENDER_WEAVE ||
     method == VS_INTERLACEMETHOD_RENDER_BOB)
    return true;
  else
    return false;
}

bool CRendererVDPAUAllwinner::Supports(ESCALINGMETHOD method)
{
  return false;
}
bool CRendererVDPAUAllwinner::Supports(ERENDERFEATURE feature)
{
  if(feature == RENDERFEATURE_BRIGHTNESS ||
     feature == RENDERFEATURE_CONTRAST)
  {
    if ((m_renderMethod & RENDER_VDPAU) && !CSettings::GetInstance().GetBool(CSettings::SETTING_VIDEOSCREEN_LIMITEDRANGE))
      return true;

    return (m_renderMethod & RENDER_GLSL)
        || ((m_renderMethod & RENDER_SW) && g_Windowing.IsExtSupported("GL_ARB_imaging") == GL_TRUE);
  }
  else if (feature == RENDERFEATURE_NOISE ||
           feature == RENDERFEATURE_SHARPNESS)
  {
    if (m_format == RENDER_FMT_VDPAU)
      return true;
  }
  else if (feature == RENDERFEATURE_STRETCH         ||
           feature == RENDERFEATURE_ZOOM            ||
           feature == RENDERFEATURE_VERTICAL_SHIFT  ||
           feature == RENDERFEATURE_PIXEL_RATIO     ||
           feature == RENDERFEATURE_POSTPROCESS     ||
           feature == RENDERFEATURE_ROTATION        ||
           feature == RENDERFEATURE_NONLINSTRETCH)
    return true;

  return false;
}

  EINTERLACEMETHOD CRendererVDPAUAllwinner::AutoInterlaceMethod()
{
  return VS_INTERLACEMETHOD_NONE;
}

CRenderInfo CRendererVDPAUAllwinner::GetRenderInfo()
{
  CRenderInfo info;
  info.formats = m_formats;
  info.max_buffer_size = NUM_BUFFERS;
  // Let the codec control the buffer size
  info.optimal_buffer_size = info.max_buffer_size;
  return info;
}

bool CRendererVDPAUAllwinner::LoadShadersHook()
{
  bool loadShaders = false;
  
  CLog::Log(LOGNOTICE, "GL: Using Allwinner Layer render method");
  m_textureTarget = GL_TEXTURE_2D;
  m_renderMethod = RENDER_VDPAU_ALLWINNER;
  return loadShaders;
}

bool CRendererVDPAUAllwinner::NeedBuffer(int idx)
{
  bool needBuffer = false;
  
  if(m_format != RENDER_FMT_VDPAU && m_format != RENDER_FMT_VDPAU_420)
  {
    YUVBUFFER &buf = m_buffers[idx];
    if(buf.hwDec)
    {
      CHwRenderPicture *pic = (CHwRenderPicture *)((AVFrame*)buf.hwDec)->opaque;
    
      if(pic && pic->FenceEnabled())
      {
        CHwLayerManagerAW::HwLayerSyncValue value;
        bool status = g_HwLayer.getSyncFenceValue(CHwLayerManagerAW::HwLayerType::Video, pic->GetFence(), value);
        if(value != CHwLayerManagerAW::HwLayerSyncValue::HWLayerFenceSignaled)
        {
          needBuffer = true;
        }
      }
    }
  }
  return needBuffer;
}

bool CRendererVDPAUAllwinner::RenderHook(int index)
{
#if 1
  YUVBUFFER &buf = m_buffers[index];
  if (buf.hwDec)
  {
    ((VDPAU::CVdpauRenderPicture*)buf.hwDec)->Sync();
  }
#endif
  return true;// nothing to be done 
}

bool CRendererVDPAUAllwinner::RenderUpdateVideoHook(bool clear, DWORD flags, DWORD alpha)
{
  ManageRenderArea();

  int top_field = (flags & RENDER_FLAG_TOP) == RENDER_FLAG_TOP;
  int surf;
  int *fencePtr=0;
  if(m_format == RENDER_FMT_VDPAU || m_format == RENDER_FMT_VDPAU_420)
  {
    VDPAU::CVdpauRenderPicture *buffer = static_cast<VDPAU::CVdpauRenderPicture*>(m_buffers[m_iYV12RenderBuffer].hwDec);
    if(buffer)
    {
      surf = buffer->surfaceCedar;
      fencePtr = &buffer->GetFence();
    }
  }
  else
  {
    CHwRenderPicture *pic = (CHwRenderPicture *)((AVFrame*)m_buffers[m_iYV12RenderBuffer].hwDec)->opaque;
    if(pic)
    {
      surf = pic->surfaceCedar;
      fencePtr = &pic->GetFence();
    }
  }
  
  if (surf)
  {
    m_vdpauAdaptor.setFrame(surf);
    
    if(m_needReconfigure || (m_oldSrc != m_sourceRect || m_oldDst != m_destRect))
    {
      cdRect_t src = { (uint32_t)m_sourceRect.x1, (uint32_t)m_sourceRect.y1, 
        (uint32_t)(m_sourceRect.x2 - m_sourceRect.x1), (uint32_t)(m_sourceRect.y2 - m_sourceRect.y1) };
      cdRect_t dst = { (uint32_t)m_destRect.x1, (uint32_t)m_destRect.y1, 
        (uint32_t)(m_destRect.x2 - m_destRect.x1), (uint32_t)(m_destRect.y2 - m_destRect.y1) };

      bool status;
      CHwLayerManagerAW::CPropertyValue prop(CHwLayerManagerAW::PropertyKey::ScalerType,
                                              CHwLayerManagerAW::ScalerType::Type_Scale);
      status = g_HwLayer.setProperty(CHwLayerManagerAW::HwLayerType::Video, prop);
      if(! status )
        CLog::Log(LOGERROR, "CRendererVDPAUAllwinner:%s error setting property scaler", __FUNCTION__);

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
      
      status = g_HwLayer.configure(CHwLayerManagerAW::HwLayerType::Video, m_vdpauAdaptor, 
                                   m_sourceRect, m_destRect);

      status = g_HwLayer.showLayer(CHwLayerManagerAW::HwLayerType::Video);
      if(! status )
        CLog::Log(LOGERROR, "CRendererVDPAUAllwinner:%s error calling showlayer", __FUNCTION__);
      
      m_needReconfigure = false;
      m_oldSrc = m_sourceRect;
      m_oldDst = m_destRect;
    }
    if(fencePtr)
    {
      int &fence = *fencePtr;
      bool status = g_HwLayer.displayFrame(CHwLayerManagerAW::HwLayerType::Video, m_vdpauAdaptor, fence,
                                          top_field);
    }
    
    // This code reduces rendering fps of the video layer when playing videos in fullscreen mode
    // it makes only sense on architectures with multiple layers
    int fps = m_fps*2;

    unsigned int now = XbmcThreads::SystemClockMillis();
    unsigned int frameTime = now - m_lastRenderTime;
    if (fps > 0 && frameTime * fps < 1000)
      XbmcThreads::ThreadSleep(1000/fps - frameTime);

    m_lastRenderTime = now;

  }

  return true;
}

bool CRendererVDPAUAllwinner::CreateTexture(int index)
{
  CLog::Log(LOGERROR, "CRendererVDPAUAllwinner:%s ", __FUNCTION__);
  return true;
}

void CRendererVDPAUAllwinner::DeleteTexture(int index)
{
  CLog::Log(LOGERROR, "CRendererVDPAUAllwinner:%s index=%d", __FUNCTION__, index);
}

bool CRendererVDPAUAllwinner::UploadTexture(int index)
{
  return true;// nothing todo
}

#endif

