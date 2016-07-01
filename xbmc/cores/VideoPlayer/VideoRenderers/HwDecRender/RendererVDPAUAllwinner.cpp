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
#include <dlfcn.h>

CRendererVDPAUAllwinner::CRendererVDPAUAllwinner(): glVDPAUPresentSurfaceCedar(NULL), 
    m_dlHandle(NULL), m_needReconfigure(false), m_frameId(0)
{
  LoadSymbols();
}

CRendererVDPAUAllwinner::~CRendererVDPAUAllwinner()
{
  void* videoLayer;
  void* dispId;
//    CWinSystemEGL::CWinLayerInfoPtr layerInfo;

  g_Windowing.GetVideoLayer(videoLayer);
  g_Windowing.GetDispId(dispId);
  glVDPAUCloseVideoLayerCedar((int)videoLayer, (int)dispId);
}

bool CRendererVDPAUAllwinner::RenderCapture(CRenderCapture* capture)
{
  CRect rect(0, 0, capture->GetWidth(), capture->GetHeight());

  return false;
}
bool CRendererVDPAUAllwinner::Configure(unsigned int width, unsigned int height, unsigned int d_width, unsigned int d_height, float fps, unsigned flags, ERenderFormat format, unsigned extended_formatunsigned, unsigned int orientation)
{
  bool result = CLinuxRendererGLES::Configure(width, height, d_width, d_height, fps, 
                                              flags, format, extended_formatunsigned, orientation);
  m_needReconfigure = true;
  return result;
}

void CRendererVDPAUAllwinner::AddVideoPictureHW(DVDVideoPicture &picture, int index)
{
  VDPAU::CVdpauRenderPicture *vdpau = picture.vdpau;
  YUVBUFFER &buf = m_buffers[index];
  VDPAU::CVdpauRenderPicture *pic = vdpau->Acquire();
  if (buf.hwDec)
    ((VDPAU::CVdpauRenderPicture*)buf.hwDec)->Release();
  buf.hwDec = pic;
}

void CRendererVDPAUAllwinner::ReleaseBuffer(int idx)
{
  YUVBUFFER &buf = m_buffers[idx];
  if (buf.hwDec)
    ((VDPAU::CVdpauRenderPicture*)buf.hwDec)->Release();
  buf.hwDec = NULL;
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

//  if(method == VS_INTERLACEMETHOD_IMX_FASTMOTION
//  || method == VS_INTERLACEMETHOD_IMX_FASTMOTION_DOUBLE)
    return true;
//  else
//    return false;
}

bool CRendererVDPAUAllwinner::Supports(EDEINTERLACEMODE mode)
{
  if(mode == VS_DEINTERLACEMODE_AUTO
  || mode == VS_DEINTERLACEMODE_FORCE
  || mode == VS_DEINTERLACEMODE_OFF)
    return true;

  return false;
}

bool CRendererVDPAUAllwinner::Supports(ESCALINGMETHOD method)
{
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
  CLog::Log(LOGNOTICE, "GL: Using Allwinner Layer render method");
  m_textureTarget = GL_TEXTURE_2D;
  m_renderMethod = RENDER_VDPAU_ALLWINNER;
  return true;
}

bool CRendererVDPAUAllwinner::RenderHook(int index)
{
  return true;// nothing to be done 
}

bool CRendererVDPAUAllwinner::RenderUpdateVideoHook(bool clear, DWORD flags, DWORD alpha)
{
#if 0
  static unsigned long long previous = 0;
  unsigned long long current = XbmcThreads::SystemClockMillis();
  printf("r->r: %d\n", (int)(current-previous));
  previous = current;
#endif
  
  ManageRenderArea();

  VDPAU::CVdpauRenderPicture *buffer = static_cast<VDPAU::CVdpauRenderPicture*>(m_buffers[m_iYV12RenderBuffer].hwDec);
  if (buffer != NULL && buffer->valid)
  {
    void* videoLayer;
    void* dispId;
//    CWinSystemEGL::CWinLayerInfoPtr layerInfo;

    g_Windowing.GetVideoLayer(videoLayer);
    g_Windowing.GetDispId(dispId);
//    g_Windowing.GetLayerInformation(layerInfo);
    if(m_needReconfigure || (m_oldSrc != m_sourceRect || m_oldDst != m_destRect))
    {
      cdRect_t src = { (uint32_t)m_sourceRect.x1, (uint32_t)m_sourceRect.y1, 
        (uint32_t)(m_sourceRect.x2 - m_sourceRect.x1), (uint32_t)(m_sourceRect.y2 - m_sourceRect.y1) };
      cdRect_t dst = { (uint32_t)m_destRect.x1, (uint32_t)m_destRect.y1, 
        (uint32_t)(m_destRect.x2 - m_destRect.x1), (uint32_t)(m_destRect.y2 - m_destRect.y1) };

      glVDPAUConfigureSurfaceCedar(buffer->surfaceCedar, (int)videoLayer, (int)dispId, src, dst);
      m_needReconfigure = false;
      m_oldSrc = m_sourceRect;
      m_oldDst = m_destRect;
    }
    buffer->frameId = m_frameId++;
    glVDPAUPresentSurfaceCedar(buffer->surfaceCedar, (int)videoLayer, (int)dispId, buffer->frameId);
  }

#if 0
  unsigned long long current2 = XbmcThreads::SystemClockMillis();
  printf("r: %d  %d\n", m_iYV12RenderBuffer, (int)(current2-current));
#endif

  return true;
}

bool CRendererVDPAUAllwinner::CreateTexture(int index)
{
  return true;
}

void CRendererVDPAUAllwinner::DeleteTexture(int index)
{
  ReleaseBuffer(index);
}

bool CRendererVDPAUAllwinner::UploadTexture(int index)
{
  return true;// nothing todo for IMX
}

bool CRendererVDPAUAllwinner::LoadSymbols()
{
  if (!m_dlHandle)
  {
    m_dlHandle  = dlopen("libcedarDisplay.so.1", RTLD_LAZY);
    if (!m_dlHandle)
    {
      const char* error = dlerror();
      if (!error)
        error = "dlerror() returned NULL";

      CLog::Log(LOGERROR,"CRendererVDPAUAllwinner::LoadSymbols: Unable to get handle to lib: %s", error);
      return false;
    }
  }

  char* error;
  (void)dlerror();
  glVDPAUPresentSurfaceCedar = (PFNGLVDPAUPRESENTSURFACECEDAR) dlsym(m_dlHandle, (const char*)"glVDPAUPresentSurfaceCedar");
  glVDPAUConfigureSurfaceCedar = (PFNGLVDPAUCONFIGURESURFACECEDAR) dlsym(m_dlHandle, (const char*)"glVDPAUConfigureSurfaceCedar");
  glVDPAUCloseVideoLayerCedar = (PFGLVDPAUCLOSEVIDEOLAYERCEDAR) dlsym(m_dlHandle, (const char*)"glVDPAUCloseVideoLayerCedar");
  error = dlerror();
  if (error)
  {
    CLog::Log(LOGERROR,"(CRendererVDPAUAllwinner) - %s in %s",error,__FUNCTION__);
    return false;
  }
  return true;
}

#endif

