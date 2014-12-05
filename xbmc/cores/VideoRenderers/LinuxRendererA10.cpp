/*
 *      Copyright (C) 2010-2012 Team XBMC
 *      http://www.xbmc.org
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

#include "system.h"
#if (defined HAVE_CONFIG_H) && (!defined WIN32)
  #include "config.h"
#endif

#if HAS_GLES == 2
#include "system_gl.h"

#include <locale.h>
#include <sys/ioctl.h>
#include "guilib/MatrixGLES.h"
#include "LinuxRendererA10.h"
#include "utils/log.h"
#include "utils/fastmemcpy.h"
#include "utils/MathUtils.h"
#include "utils/GLUtils.h"
#include "settings/Settings.h"
#include "settings/AdvancedSettings.h"
#include "settings/GUISettings.h"
#include "guilib/FrameBufferObject.h"
#include "VideoShaders/YUV2RGBShader.h"
#include "VideoShaders/VideoFilterShader.h"
#include "windowing/WindowingFactory.h"
#include "dialogs/GUIDialogKaiToast.h"
#include "guilib/Texture.h"
#include "threads/SingleLock.h"
#include "RenderCapture.h"
#include "RenderFormats.h"
#include "xbmc/Application.h"

using namespace Shaders;

CLinuxRendererA10::CLinuxRendererA10()
{
  m_textureTarget = GL_TEXTURE_2D;

  for (int i = 0; i < NUM_BUFFERS; i++)
  {
    m_eventTexturesDone[i] = new CEvent(false,true);
    memset(&m_buffers, 0, sizeof(m_buffers));
  }

  m_renderMethod = RENDER_GLSL;
  m_oldRenderMethod = m_renderMethod;
  m_renderQuality = RQ_SINGLEPASS;
  m_iFlags = 0;
  m_format = RENDER_FMT_NONE;

  m_iYV12RenderBuffer = 0;
  m_flipindex = 0;
  m_currentField = FIELD_FULL;
  m_reloadShaders = 0;
  m_pYUVShader = NULL;
  m_pVideoFilterShader = NULL;
  m_scalingMethod = VS_SCALINGMETHOD_LINEAR;
  m_scalingMethodGui = (ESCALINGMETHOD)-1;

  // default texture handlers to YUV
  m_textureUpload = &CLinuxRendererA10::UploadYV12Texture;
  m_textureCreate = &CLinuxRendererA10::CreateYV12Texture;
  m_textureDelete = &CLinuxRendererA10::DeleteYV12Texture;

}

CLinuxRendererA10::~CLinuxRendererA10()
{
  UnInit();
  for (int i = 0; i < NUM_BUFFERS; i++)
    delete m_eventTexturesDone[i];

  if (m_pYUVShader)
  {
    m_pYUVShader->Free();
    delete m_pYUVShader;
    m_pYUVShader = NULL;
  }
}

void CLinuxRendererA10::ManageTextures()
{
  //m_iYV12RenderBuffer = 0;
  return;
}

bool CLinuxRendererA10::ValidateRenderTarget()
{
  if (!m_bValidated)
  {
    CLog::Log(LOGNOTICE,"Using GL_TEXTURE_2D");

     // create the yuv textures
    LoadShaders();

    for (int i = 0 ; i < NUM_BUFFERS ; i++)
      (this->*m_textureCreate)(i);

    m_bValidated = true;
    return true;
  }
  return false;
}

bool CLinuxRendererA10::Configure(unsigned int width, unsigned int height, unsigned int d_width, unsigned int d_height, float fps, unsigned flags, ERenderFormat format, unsigned extended_format, unsigned int orientation)
{
  m_sourceWidth = width;
  m_sourceHeight = height;
  m_renderOrientation = orientation;

  // Save the flags.
  m_iFlags = flags;
  m_format = format;

  // Calculate the input frame aspect ratio.
  CalculateFrameAspectRatio(d_width, d_height);
  ChooseBestResolution(fps);
  SetViewMode(g_settings.m_currentVideoSettings.m_ViewMode);
  ManageDisplay();

  m_bConfigured = true;
  m_bImageReady = false;
  m_scalingMethodGui = (ESCALINGMETHOD)-1;

  // Ensure that textures are recreated and rendering starts only after the 1st
  // frame is loaded after every call to Configure().
  m_bValidated = false;

  for (int i = 0 ; i< NUM_BUFFERS; i++)
    m_buffers[i].image.flags = 0;

  m_iLastRenderBuffer = -1;

  m_RenderUpdateCallBackFn = NULL;
  m_RenderUpdateCallBackCtx = NULL;
  if ((m_format == RENDER_FMT_BYPASS) && g_application.GetCurrentPlayer())
  {
    g_application.m_pPlayer->GetRenderFeatures(m_renderFeatures);
    g_application.m_pPlayer->GetDeinterlaceMethods(m_deinterlaceMethods);
    g_application.m_pPlayer->GetDeinterlaceModes(m_deinterlaceModes);
    g_application.m_pPlayer->GetScalingMethods(m_scalingMethods);
  }

  return true;
}

int CLinuxRendererA10::NextYV12Texture()
{
  return (m_iYV12RenderBuffer + 1) % NUM_BUFFERS;
}

int CLinuxRendererA10::GetImage(YV12Image *image, int source, bool readonly)
{
  if (!image) return -1;
  if (!m_bValidated) return -1;

  /* take next available buffer */
  if( source == AUTOSOURCE )
   source = NextYV12Texture();

  if (m_renderMethod & RENDER_A10BUF )
  {
    return source;
  }

  YV12Image &im = m_buffers[source].image;

  if ((im.flags&(~IMAGE_FLAG_READY)) != 0)
  {
     CLog::Log(LOGDEBUG, "CLinuxRenderer::GetImage - request image but none to give");
     return -1;
  }

  if( readonly )
    im.flags |= IMAGE_FLAG_READING;
  else
  {
    if( !m_eventTexturesDone[source]->WaitMSec(500) )
      CLog::Log(LOGWARNING, "%s - Timeout waiting for texture %d", __FUNCTION__, source);

    im.flags |= IMAGE_FLAG_WRITING;
  }

  // copy the image - should be operator of YV12Image
  for (int p=0;p<MAX_PLANES;p++)
  {
    image->plane[p]  = im.plane[p];
    image->stride[p] = im.stride[p];
  }
  image->width    = im.width;
  image->height   = im.height;
  image->flags    = im.flags;
  image->cshift_x = im.cshift_x;
  image->cshift_y = im.cshift_y;
  image->bpp      = 1;

  return source;

  return -1;
}

void CLinuxRendererA10::ReleaseImage(int source, bool preserve)
{
  YV12Image &im = m_buffers[source].image;

  if( im.flags & IMAGE_FLAG_WRITING )
    m_eventTexturesDone[source]->Set();

  im.flags &= ~IMAGE_FLAG_INUSE;
  im.flags |= IMAGE_FLAG_READY;
  /* if image should be preserved reserve it so it's not auto seleceted */

  if( preserve )
    im.flags |= IMAGE_FLAG_RESERVED;

  m_bImageReady = true;
}

void CLinuxRendererA10::CalculateTextureSourceRects(int source, int num_planes)
{
  YUVBUFFER& buf    =  m_buffers[source];
  YV12Image* im     = &buf.image;
  YUVFIELDS& fields =  buf.fields;

  // calculate the source rectangle
  for(int field = 0; field < 3; field++)
  {
    for(int plane = 0; plane < num_planes; plane++)
    {
      YUVPLANE& p = fields[field][plane];

      p.rect = m_sourceRect;
      p.width  = im->width;
      p.height = im->height;

      if(field != FIELD_FULL)
      {
        /* correct for field offsets and chroma offsets */
        float offset_y = 0.5;
        if(plane != 0)
          offset_y += 0.5;
        if(field == FIELD_BOT)
          offset_y *= -1;

        p.rect.y1 += offset_y;
        p.rect.y2 += offset_y;

        /* half the height if this is a field */
        p.height  *= 0.5f;
        p.rect.y1 *= 0.5f;
        p.rect.y2 *= 0.5f;
      }

      if(plane != 0)
      {
        p.width   /= 1 << im->cshift_x;
        p.height  /= 1 << im->cshift_y;

        p.rect.x1 /= 1 << im->cshift_x;
        p.rect.x2 /= 1 << im->cshift_x;
        p.rect.y1 /= 1 << im->cshift_y;
        p.rect.y2 /= 1 << im->cshift_y;
      }

      if (m_textureTarget == GL_TEXTURE_2D)
      {
        p.height  /= p.texheight;
        p.rect.y1 /= p.texheight;
        p.rect.y2 /= p.texheight;
        p.width   /= p.texwidth;
        p.rect.x1 /= p.texwidth;
        p.rect.x2 /= p.texwidth;
      }
    }
  }
}

void CLinuxRendererA10::LoadPlane( YUVPLANE& plane, int type, unsigned flipindex
                                , unsigned width, unsigned height
                                , int stride, void* data )
{
  if(plane.flipindex == flipindex)
    return;

  const GLvoid *pixelData = data;

  int bps = glFormatElementByteCount(type);

  glBindTexture(m_textureTarget, plane.id);

  // OpenGL ES does not support strided texture input.
  if(stride != width * bps)
  {
    unsigned char* src = (unsigned char*)data;
    for (int y = 0; y < height;++y, src += stride)
      glTexSubImage2D(m_textureTarget, 0, 0, y, width, 1, type, GL_UNSIGNED_BYTE, src);
  } else {
    glTexSubImage2D(m_textureTarget, 0, 0, 0, width, height, type, GL_UNSIGNED_BYTE, pixelData);
  }

  /* check if we need to load any border pixels */
  if(height < plane.texheight)
    glTexSubImage2D( m_textureTarget, 0
                   , 0, height, width, 1
                   , type, GL_UNSIGNED_BYTE
                   , (unsigned char*)pixelData + stride * (height-1));

  if(width  < plane.texwidth)
    glTexSubImage2D( m_textureTarget, 0
                   , width, 0, 1, height
                   , type, GL_UNSIGNED_BYTE
                   , (unsigned char*)pixelData + bps * (width-1));

  glBindTexture(m_textureTarget, 0);

  plane.flipindex = flipindex;
}

void CLinuxRendererA10::Reset()
{
  for(int i=0; i<NUM_BUFFERS; i++)
  {
    /* reset all image flags, this will cleanup textures later */
    m_buffers[i].image.flags = 0;
    /* reset texture locks, a bit ugly, could result in tearing */
    m_eventTexturesDone[i]->Set();
  }
}

void CLinuxRendererA10::Update(bool bPauseDrawing)
{
  if (!m_bConfigured) return;
  ManageDisplay();
  ManageTextures();
}

void CLinuxRendererA10::RenderUpdate(bool clear, DWORD flags, DWORD alpha)
{
  if (!m_bConfigured) return;

  // if its first pass, just init textures and return
  if (ValidateRenderTarget())
    return;

  if (m_renderMethod & RENDER_BYPASS)
  {
    ManageDisplay();
    ManageTextures();
    // if running bypass, then the player might need the src/dst rects
    // for sizing video playback on a layer other than the gles layer.
    if (m_RenderUpdateCallBackFn)
      (*m_RenderUpdateCallBackFn)(m_RenderUpdateCallBackCtx, m_sourceRect, m_destRect);

    RESOLUTION res = GetResolution();
    int iWidth = g_settings.m_ResInfo[res].iWidth;
    int iHeight = g_settings.m_ResInfo[res].iHeight;

    g_graphicsContext.BeginPaint();

    glScissor(m_destRect.x1,
              iHeight - m_destRect.y2,
              m_destRect.x2 - m_destRect.x1,
              m_destRect.y2 - m_destRect.y1);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glClearColor(0, 0, 0, 0);
    glClear(GL_COLOR_BUFFER_BIT);
    glScissor(0, 0, iWidth, iHeight);

    g_graphicsContext.EndPaint();
    return;
  }
  else if (m_renderMethod & RENDER_A10BUF)
  {
    ManageDisplay();
    ManageTextures();

    if (m_RenderUpdateCallBackFn)
      (*m_RenderUpdateCallBackFn)(m_RenderUpdateCallBackCtx, m_sourceRect, m_destRect);

    A10VLWaitVSYNC();

    g_graphicsContext.BeginPaint();

    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glClearColor(1.0/255, 2.0/255, 3.0/255, 0);
    glClear(GL_COLOR_BUFFER_BIT);
    glClearColor(0, 0, 0, 0);

    g_graphicsContext.EndPaint();
  }

  // this needs to be checked after texture validation
  if (!m_bImageReady) return;

  int index = m_iYV12RenderBuffer;
  YUVBUFFER& buf =  m_buffers[index];

  if (m_renderMethod & RENDER_A10BUF)
  {
    A10VLDisplayQueueItem(buf.a10buffer, m_sourceRect, m_destRect);
    m_iLastRenderBuffer = index;
    VerifyGLState();
    return;
  }

  if (!buf.fields[FIELD_FULL][0].id || !buf.image.flags) return;

  ManageDisplay();
  ManageTextures();

  g_graphicsContext.BeginPaint();

  if( !m_eventTexturesDone[index]->WaitMSec(500))
  {
    CLog::Log(LOGWARNING, "%s - Timeout waiting for texture %d", __FUNCTION__, index);

    // render the previous frame if this one isn't ready yet
    if (m_iLastRenderBuffer > -1)
    {
      m_iYV12RenderBuffer = m_iLastRenderBuffer;
      index = m_iYV12RenderBuffer;
    }
  }
  else
    m_iLastRenderBuffer = index;

  if (clear)
  {
    glClearColor(m_clearColour, m_clearColour, m_clearColour, 0);
    glClear(GL_COLOR_BUFFER_BIT);
    glClearColor(0,0,0,0);
  }

  if (alpha<255)
  {
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    if (m_pYUVShader)
      m_pYUVShader->SetAlpha(alpha / 255.0f);
  }
  else
  {
    glDisable(GL_BLEND);
    if (m_pYUVShader)
      m_pYUVShader->SetAlpha(1.0f);
  }

  if ((flags & RENDER_FLAG_TOP) && (flags & RENDER_FLAG_BOT))
    CLog::Log(LOGERROR, "GLES: Cannot render stipple!");
  else
    Render(flags, index);

  VerifyGLState();
  glEnable(GL_BLEND);

  g_graphicsContext.EndPaint();
}

void CLinuxRendererA10::FlipPage(int source)
{
  if( source >= 0 && source < NUM_BUFFERS )
    m_iYV12RenderBuffer = source;
  else
    m_iYV12RenderBuffer = NextYV12Texture();

  m_buffers[m_iYV12RenderBuffer].flipindex = ++m_flipindex;

  return;
}

unsigned int CLinuxRendererA10::PreInit()
{
  CSingleLock lock(g_graphicsContext);
  m_bConfigured = false;
  m_bValidated = false;
  UnInit();
  m_resolution = g_guiSettings.m_LookAndFeelResolution;
  if ( m_resolution == RES_WINDOW )
    m_resolution = RES_DESKTOP;

  m_iYV12RenderBuffer = 0;

  m_formats.push_back(RENDER_FMT_YUV420P);
  m_formats.push_back(RENDER_FMT_BYPASS);
  m_formats.push_back(RENDER_FMT_A10BUF);

  // setup the background colour
  m_clearColour = (float)(g_advancedSettings.m_videoBlackBarColour & 0xff) / 0xff;

  return true;
}

void CLinuxRendererA10::UpdateVideoFilter()
{
  if (m_scalingMethodGui == g_settings.m_currentVideoSettings.m_ScalingMethod)
    return;
  m_scalingMethodGui = g_settings.m_currentVideoSettings.m_ScalingMethod;
  m_scalingMethod    = m_scalingMethodGui;

  if(!Supports(m_scalingMethod))
  {
    CLog::Log(LOGWARNING, "CLinuxRendererA10::UpdateVideoFilter - choosen scaling method %d, is not supported by renderer", (int)m_scalingMethod);
    m_scalingMethod = VS_SCALINGMETHOD_LINEAR;
  }

  if (m_pVideoFilterShader)
  {
    m_pVideoFilterShader->Free();
    delete m_pVideoFilterShader;
    m_pVideoFilterShader = NULL;
  }
  m_fbo.Cleanup();

  VerifyGLState();

  switch (m_scalingMethod)
  {
  case VS_SCALINGMETHOD_NEAREST:
    SetTextureFilter(GL_NEAREST);
    m_renderQuality = RQ_SINGLEPASS;
    return;

  case VS_SCALINGMETHOD_LINEAR:
    SetTextureFilter(GL_LINEAR);
    m_renderQuality = RQ_SINGLEPASS;
    return;

  case VS_SCALINGMETHOD_CUBIC:
    CLog::Log(LOGERROR, "GLES: CUBIC not supported!");
    break;

  case VS_SCALINGMETHOD_LANCZOS2:
  case VS_SCALINGMETHOD_LANCZOS3:
  case VS_SCALINGMETHOD_SINC8:
  case VS_SCALINGMETHOD_NEDI:
    CLog::Log(LOGERROR, "GL: TODO: This scaler has not yet been implemented");
    break;

  default:
    break;
  }

  CGUIDialogKaiToast::QueueNotification("Video Renderering", "Failed to init video filters/scalers, falling back to bilinear scaling");
  CLog::Log(LOGERROR, "GL: Falling back to bilinear due to failure to init scaler");
  if (m_pVideoFilterShader)
  {
    m_pVideoFilterShader->Free();
    delete m_pVideoFilterShader;
    m_pVideoFilterShader = NULL;
  }
  m_fbo.Cleanup();

  SetTextureFilter(GL_LINEAR);
  m_renderQuality = RQ_SINGLEPASS;
}

void CLinuxRendererA10::LoadShaders(int field)
{
  int requestedMethod = g_guiSettings.GetInt("videoplayer.rendermethod");
  CLog::Log(LOGDEBUG, "GL: Requested render method: %d", requestedMethod);

  if (m_pYUVShader)
  {
    m_pYUVShader->Free();
    delete m_pYUVShader;
    m_pYUVShader = NULL;
  }

  switch(requestedMethod)
  {
    case RENDER_METHOD_AUTO:
    case RENDER_METHOD_GLSL:
      if (m_format == RENDER_FMT_A10BUF)
      {
        CLog::Log(LOGNOTICE, "using A10 render method");
        m_renderMethod = RENDER_A10BUF;
        break;
      }
      // Try GLSL shaders if supported and user requested auto or GLSL.
      // create regular progressive scan shader
      m_pYUVShader = new YUV2RGBProgressiveShader(false, m_iFlags, m_format);
      CLog::Log(LOGNOTICE, "GL: Selecting Single Pass YUV 2 RGB shader");

      if (m_pYUVShader && m_pYUVShader->CompileAndLink())
      {
        m_renderMethod = RENDER_GLSL;
        UpdateVideoFilter();
        break;
      }
      else
      {
        m_pYUVShader->Free();
        delete m_pYUVShader;
        m_pYUVShader = NULL;
        CLog::Log(LOGERROR, "GL: Error enabling YUV2RGB GLSL shader");
      }
      break;
    default:
      // Use software YUV 2 RGB conversion if user requested it or GLSL failed
      CLog::Log(LOGERROR, "no software rendering.");
      break;
  }

  // Now that we now the render method, setup texture function handlers
  if (m_format == RENDER_FMT_BYPASS || m_format == RENDER_FMT_A10BUF)
  {
    m_textureUpload = &CLinuxRendererA10::UploadBYPASSTexture;
    m_textureCreate = &CLinuxRendererA10::CreateBYPASSTexture;
    m_textureDelete = &CLinuxRendererA10::DeleteBYPASSTexture;
  }
  else
  {
    // default to YV12 texture handlers
    m_textureUpload = &CLinuxRendererA10::UploadYV12Texture;
    m_textureCreate = &CLinuxRendererA10::CreateYV12Texture;
    m_textureDelete = &CLinuxRendererA10::DeleteYV12Texture;
  }

  if (m_oldRenderMethod != m_renderMethod)
  {
    CLog::Log(LOGDEBUG, "CLinuxRendererA10: Reorder drawpoints due to method change from %i to %i", m_oldRenderMethod, m_renderMethod);
    ReorderDrawPoints();
    m_oldRenderMethod = m_renderMethod;
  }
}

void CLinuxRendererA10::UnInit()
{
  CLog::Log(LOGDEBUG, "LinuxRendererGL: Cleaning up GL resources");
  CSingleLock lock(g_graphicsContext);

  A10VLHide();

  // YV12 textures
  for (int i = 0; i < NUM_BUFFERS; ++i)
    (this->*m_textureDelete)(i);

  // cleanup framebuffer object if it was in use
  m_fbo.Cleanup();
  m_bValidated = false;
  m_bImageReady = false;
  m_bConfigured = false;
  m_RenderUpdateCallBackFn = NULL;
  m_RenderUpdateCallBackCtx = NULL;
}

inline void CLinuxRendererA10::ReorderDrawPoints()
{

  CBaseRenderer::ReorderDrawPoints();//call base impl. for rotating the points
}

void CLinuxRendererA10::Render(DWORD flags, int index)
{
  // If rendered directly by the hardware
  if (m_renderMethod & RENDER_BYPASS)
    return;

  // obtain current field, if interlaced
  if( flags & RENDER_FLAG_TOP)
    m_currentField = FIELD_TOP;

  else if (flags & RENDER_FLAG_BOT)
    m_currentField = FIELD_BOT;

  else
    m_currentField = FIELD_FULL;

  (this->*m_textureUpload)(index);

  if (m_renderMethod & RENDER_GLSL)
  {
    UpdateVideoFilter();
    switch(m_renderQuality)
    {
    case RQ_LOW:
    case RQ_SINGLEPASS:
      RenderSinglePass(index, m_currentField);
      VerifyGLState();
      break;

    case RQ_MULTIPASS:
      RenderMultiPass(index, m_currentField);
      VerifyGLState();
      break;
    }
  }
}

void CLinuxRendererA10::RenderSinglePass(int index, int field)
{
  YV12Image &im     = m_buffers[index].image;
  YUVFIELDS &fields = m_buffers[index].fields;
  YUVPLANES &planes = fields[field];

  if (m_reloadShaders)
  {
    m_reloadShaders = 0;
    LoadShaders(field);
  }

  glDisable(GL_DEPTH_TEST);

  // Y
  glActiveTexture(GL_TEXTURE0);
  glEnable(m_textureTarget);
  glBindTexture(m_textureTarget, planes[0].id);

  // U
  glActiveTexture(GL_TEXTURE1);
  glEnable(m_textureTarget);
  glBindTexture(m_textureTarget, planes[1].id);

  // V
  glActiveTexture(GL_TEXTURE2);
  glEnable(m_textureTarget);
  glBindTexture(m_textureTarget, planes[2].id);

  glActiveTexture(GL_TEXTURE0);
  VerifyGLState();

  m_pYUVShader->SetBlack(g_settings.m_currentVideoSettings.m_Brightness * 0.01f - 0.5f);
  m_pYUVShader->SetContrast(g_settings.m_currentVideoSettings.m_Contrast * 0.02f);
  m_pYUVShader->SetWidth(im.width);
  m_pYUVShader->SetHeight(im.height);
  if     (field == FIELD_TOP)
    m_pYUVShader->SetField(1);
  else if(field == FIELD_BOT)
    m_pYUVShader->SetField(0);

  m_pYUVShader->SetMatrices(g_matrices.GetMatrix(MM_PROJECTION), g_matrices.GetMatrix(MM_MODELVIEW));
  m_pYUVShader->Enable();

  GLubyte idx[4] = {0, 1, 3, 2};        //determines order of triangle strip
  GLfloat m_vert[4][3];
  GLfloat m_tex[3][4][2];

  GLint vertLoc = m_pYUVShader->GetVertexLoc();
  GLint Yloc    = m_pYUVShader->GetYcoordLoc();
  GLint Uloc    = m_pYUVShader->GetUcoordLoc();
  GLint Vloc    = m_pYUVShader->GetVcoordLoc();

  glVertexAttribPointer(vertLoc, 3, GL_FLOAT, 0, 0, m_vert);
  glVertexAttribPointer(Yloc, 2, GL_FLOAT, 0, 0, m_tex[0]);
  glVertexAttribPointer(Uloc, 2, GL_FLOAT, 0, 0, m_tex[1]);
  glVertexAttribPointer(Vloc, 2, GL_FLOAT, 0, 0, m_tex[2]);

  glEnableVertexAttribArray(vertLoc);
  glEnableVertexAttribArray(Yloc);
  glEnableVertexAttribArray(Uloc);
  glEnableVertexAttribArray(Vloc);

  // Setup vertex position values
  for(int i = 0; i < 4; i++)
  {
    m_vert[i][0] = m_rotatedDestCoords[i].x;
    m_vert[i][1] = m_rotatedDestCoords[i].y;
    m_vert[i][2] = 0.0f;// set z to 0
  }

  // Setup texture coordinates
  for (int i=0; i<3; i++)
  {
    m_tex[i][0][0] = m_tex[i][3][0] = planes[i].rect.x1;
    m_tex[i][0][1] = m_tex[i][1][1] = planes[i].rect.y1;
    m_tex[i][1][0] = m_tex[i][2][0] = planes[i].rect.x2;
    m_tex[i][2][1] = m_tex[i][3][1] = planes[i].rect.y2;
  }

  glDrawElements(GL_TRIANGLE_STRIP, 4, GL_UNSIGNED_BYTE, idx);

  VerifyGLState();

  m_pYUVShader->Disable();
  VerifyGLState();

  glDisableVertexAttribArray(vertLoc);
  glDisableVertexAttribArray(Yloc);
  glDisableVertexAttribArray(Uloc);
  glDisableVertexAttribArray(Vloc);

  glActiveTexture(GL_TEXTURE1);
  glDisable(m_textureTarget);

  glActiveTexture(GL_TEXTURE2);
  glDisable(m_textureTarget);

  glActiveTexture(GL_TEXTURE0);
  glDisable(m_textureTarget);

  g_matrices.MatrixMode(MM_MODELVIEW);

  VerifyGLState();
}

void CLinuxRendererA10::RenderMultiPass(int index, int field)
{
  // TODO: Multipass rendering does not currently work! FIX!
  CLog::Log(LOGERROR, "GLES: MULTIPASS rendering was called! But it doesnt work!!!");
  return;

  YV12Image &im     = m_buffers[index].image;
  YUVPLANES &planes = m_buffers[index].fields[field];

  if (m_reloadShaders)
  {
    m_reloadShaders = 0;
    LoadShaders(m_currentField);
  }

  glDisable(GL_DEPTH_TEST);

  // Y
  glEnable(m_textureTarget);
  glActiveTexture(GL_TEXTURE0);
  glBindTexture(m_textureTarget, planes[0].id);
  VerifyGLState();

  // U
  glActiveTexture(GL_TEXTURE1);
  glEnable(m_textureTarget);
  glBindTexture(m_textureTarget, planes[1].id);
  VerifyGLState();

  // V
  glActiveTexture(GL_TEXTURE2);
  glEnable(m_textureTarget);
  glBindTexture(m_textureTarget, planes[2].id);
  VerifyGLState();

  glActiveTexture(GL_TEXTURE0);
  VerifyGLState();

  // make sure the yuv shader is loaded and ready to go
  if (!m_pYUVShader || (!m_pYUVShader->OK()))
  {
    CLog::Log(LOGERROR, "GL: YUV shader not active, cannot do multipass render");
    return;
  }

  m_fbo.BeginRender();
  VerifyGLState();

  m_pYUVShader->SetBlack(g_settings.m_currentVideoSettings.m_Brightness * 0.01f - 0.5f);
  m_pYUVShader->SetContrast(g_settings.m_currentVideoSettings.m_Contrast * 0.02f);
  m_pYUVShader->SetWidth(im.width);
  m_pYUVShader->SetHeight(im.height);
  if     (field == FIELD_TOP)
    m_pYUVShader->SetField(1);
  else if(field == FIELD_BOT)
    m_pYUVShader->SetField(0);

  VerifyGLState();
//TODO
//  glPushAttrib(GL_VIEWPORT_BIT);
//  glPushAttrib(GL_SCISSOR_BIT);
  g_matrices.MatrixMode(MM_MODELVIEW);
  g_matrices.PushMatrix();
  g_matrices.LoadIdentity();
  VerifyGLState();

  g_matrices.MatrixMode(MM_PROJECTION);
  g_matrices.PushMatrix();
  g_matrices.LoadIdentity();
  VerifyGLState();
  g_matrices.Ortho2D(0, m_sourceWidth, 0, m_sourceHeight);
  glViewport(0, 0, m_sourceWidth, m_sourceHeight);
  glScissor(0, 0, m_sourceWidth, m_sourceHeight);
  g_matrices.MatrixMode(MM_MODELVIEW);
  VerifyGLState();


  if (!m_pYUVShader->Enable())
  {
    CLog::Log(LOGERROR, "GL: Error enabling YUV shader");
  }

  float imgwidth  = planes[0].rect.x2 - planes[0].rect.x1;
  float imgheight = planes[0].rect.y2 - planes[0].rect.y1;
  if (m_textureTarget == GL_TEXTURE_2D)
  {
    imgwidth  *= planes[0].texwidth;
    imgheight *= planes[0].texheight;
  }

  // 1st Pass to video frame size
//TODO
//  glBegin(GL_QUADS);
//
//  glMultiTexCoord2fARB(GL_TEXTURE0, planes[0].rect.x1, planes[0].rect.y1);
//  glMultiTexCoord2fARB(GL_TEXTURE1, planes[1].rect.x1, planes[1].rect.y1);
//  glMultiTexCoord2fARB(GL_TEXTURE2, planes[2].rect.x1, planes[2].rect.y1);
//  glVertex2f(0.0f    , 0.0f);
//
//  glMultiTexCoord2fARB(GL_TEXTURE0, planes[0].rect.x2, planes[0].rect.y1);
//  glMultiTexCoord2fARB(GL_TEXTURE1, planes[1].rect.x2, planes[1].rect.y1);
//  glMultiTexCoord2fARB(GL_TEXTURE2, planes[2].rect.x2, planes[2].rect.y1);
//  glVertex2f(imgwidth, 0.0f);
//
//  glMultiTexCoord2fARB(GL_TEXTURE0, planes[0].rect.x2, planes[0].rect.y2);
//  glMultiTexCoord2fARB(GL_TEXTURE1, planes[1].rect.x2, planes[1].rect.y2);
//  glMultiTexCoord2fARB(GL_TEXTURE2, planes[2].rect.x2, planes[2].rect.y2);
//  glVertex2f(imgwidth, imgheight);
//
//  glMultiTexCoord2fARB(GL_TEXTURE0, planes[0].rect.x1, planes[0].rect.y2);
//  glMultiTexCoord2fARB(GL_TEXTURE1, planes[1].rect.x1, planes[1].rect.y2);
//  glMultiTexCoord2fARB(GL_TEXTURE2, planes[2].rect.x1, planes[2].rect.y2);
//  glVertex2f(0.0f    , imgheight);
//
//  glEnd();
//  VerifyGLState();

  m_pYUVShader->Disable();

  g_matrices.MatrixMode(MM_MODELVIEW);
  g_matrices.PopMatrix(); // pop modelview
  g_matrices.MatrixMode(MM_PROJECTION);
  g_matrices.PopMatrix(); // pop projection
//TODO
//  glPopAttrib(); // pop scissor
//  glPopAttrib(); // pop viewport
  g_matrices.MatrixMode(MM_MODELVIEW);
  VerifyGLState();

  m_fbo.EndRender();

  glActiveTexture(GL_TEXTURE1);
  glDisable(m_textureTarget);
  glActiveTexture(GL_TEXTURE2);
  glDisable(m_textureTarget);
  glActiveTexture(GL_TEXTURE0);
  glDisable(m_textureTarget);

  glEnable(GL_TEXTURE_2D);
  glBindTexture(GL_TEXTURE_2D, m_fbo.Texture());
  VerifyGLState();

  // Use regular normalized texture coordinates

  // 2nd Pass to screen size with optional video filter

  if (m_pVideoFilterShader)
  {
    m_fbo.SetFiltering(GL_TEXTURE_2D, GL_NEAREST);
    m_pVideoFilterShader->SetSourceTexture(0);
    m_pVideoFilterShader->SetWidth(m_sourceWidth);
    m_pVideoFilterShader->SetHeight(m_sourceHeight);
    m_pVideoFilterShader->Enable();
  }
  else
    m_fbo.SetFiltering(GL_TEXTURE_2D, GL_LINEAR);

  VerifyGLState();

  imgwidth  /= m_sourceWidth;
  imgheight /= m_sourceHeight;

//TODO
//  glBegin(GL_QUADS);
//
//  glMultiTexCoord2fARB(GL_TEXTURE0, 0.0f    , 0.0f);
//  glVertex4f(m_destRect.x1, m_destRect.y1, 0, 1.0f );
//
//  glMultiTexCoord2fARB(GL_TEXTURE0, imgwidth, 0.0f);
//  glVertex4f(m_destRect.x2, m_destRect.y1, 0, 1.0f);
//
//  glMultiTexCoord2fARB(GL_TEXTURE0, imgwidth, imgheight);
//  glVertex4f(m_destRect.x2, m_destRect.y2, 0, 1.0f);
//
//  glMultiTexCoord2fARB(GL_TEXTURE0, 0.0f    , imgheight);
//  glVertex4f(m_destRect.x1, m_destRect.y2, 0, 1.0f);
//
//  glEnd();

  VerifyGLState();

  if (m_pVideoFilterShader)
    m_pVideoFilterShader->Disable();

  VerifyGLState();

  glDisable(m_textureTarget);
  VerifyGLState();
}

bool CLinuxRendererA10::RenderCapture(CRenderCapture* capture)
{
  if (!m_bValidated)
    return false;

  // save current video rect
  CRect saveSize = m_destRect;
  saveRotatedCoords();//backup current m_rotatedDestCoords

  // new video rect is thumbnail size
  m_destRect.SetRect(0, 0, (float)capture->GetWidth(), (float)capture->GetHeight());
  MarkDirty();
  syncDestRectToRotatedPoints();//syncs the changed destRect to m_rotatedDestCoords
  // clear framebuffer and invert Y axis to get non-inverted image
  glDisable(GL_BLEND);

  g_matrices.MatrixMode(MM_MODELVIEW);
  g_matrices.PushMatrix();
  g_matrices.Translatef(0.0f, capture->GetHeight(), 0.0f);
  g_matrices.Scalef(1.0f, -1.0f, 1.0f);

  capture->BeginRender();

  Render(RENDER_FLAG_NOOSD, m_iYV12RenderBuffer);
  // read pixels
  glReadPixels(0, g_graphicsContext.GetHeight() - capture->GetHeight(), capture->GetWidth(), capture->GetHeight(),
               GL_RGBA, GL_UNSIGNED_BYTE, capture->GetRenderBuffer());

  // OpenGLES returns in RGBA order but CRenderCapture needs BGRA order
  // XOR Swap RGBA -> BGRA
  unsigned char* pixels = (unsigned char*)capture->GetRenderBuffer();
  for (int i = 0; i < capture->GetWidth() * capture->GetHeight(); i++, pixels+=4)
  {
    std::swap(pixels[0], pixels[2]);
  }

  capture->EndRender();

  // revert model view matrix
  g_matrices.MatrixMode(MM_MODELVIEW);
  g_matrices.PopMatrix();

  // restore original video rect
  m_destRect = saveSize;
  restoreRotatedCoords();//restores the previous state of the rotated dest coords

  return true;
}

//********************************************************************************************************
// YV12 Texture creation, deletion, copying + clearing
//********************************************************************************************************
void CLinuxRendererA10::UploadYV12Texture(int source)
{
  YUVBUFFER& buf    =  m_buffers[source];
  YV12Image* im     = &buf.image;
  YUVFIELDS& fields =  buf.fields;


  if (!(im->flags&IMAGE_FLAG_READY))
  {
    m_eventTexturesDone[source]->Set();
    return;
  }

  bool deinterlacing;
  if (m_currentField == FIELD_FULL)
    deinterlacing = false;
  else
    deinterlacing = true;

  glEnable(m_textureTarget);
  VerifyGLState();

  glPixelStorei(GL_UNPACK_ALIGNMENT,1);

  if (deinterlacing)
  {
    // Load Y fields
    LoadPlane( fields[FIELD_TOP][0] , GL_LUMINANCE, buf.flipindex
        , im->width, im->height >> 1
        , im->stride[0]*2, im->plane[0] );

    LoadPlane( fields[FIELD_BOT][0], GL_LUMINANCE, buf.flipindex
        , im->width, im->height >> 1
        , im->stride[0]*2, im->plane[0] + im->stride[0]) ;
  }
  else
  {
    // Load Y plane
    LoadPlane( fields[FIELD_FULL][0], GL_LUMINANCE, buf.flipindex
        , im->width, im->height
        , im->stride[0], im->plane[0] );
  }

  VerifyGLState();

  glPixelStorei(GL_UNPACK_ALIGNMENT,1);

  if (deinterlacing)
  {
    // Load Even U & V Fields
    LoadPlane( fields[FIELD_TOP][1], GL_LUMINANCE, buf.flipindex
        , im->width >> im->cshift_x, im->height >> (im->cshift_y + 1)
        , im->stride[1]*2, im->plane[1] );

    LoadPlane( fields[FIELD_TOP][2], GL_LUMINANCE, buf.flipindex
        , im->width >> im->cshift_x, im->height >> (im->cshift_y + 1)
        , im->stride[2]*2, im->plane[2] );

    // Load Odd U & V Fields
    LoadPlane( fields[FIELD_BOT][1], GL_LUMINANCE, buf.flipindex
        , im->width >> im->cshift_x, im->height >> (im->cshift_y + 1)
        , im->stride[1]*2, im->plane[1] + im->stride[1] );

    LoadPlane( fields[FIELD_BOT][2], GL_LUMINANCE, buf.flipindex
        , im->width >> im->cshift_x, im->height >> (im->cshift_y + 1)
        , im->stride[2]*2, im->plane[2] + im->stride[2] );

  }
  else
  {
    LoadPlane( fields[FIELD_FULL][1], GL_LUMINANCE, buf.flipindex
        , im->width >> im->cshift_x, im->height >> im->cshift_y
        , im->stride[1], im->plane[1] );

    LoadPlane( fields[FIELD_FULL][2], GL_LUMINANCE, buf.flipindex
        , im->width >> im->cshift_x, im->height >> im->cshift_y
        , im->stride[2], im->plane[2] );
  }

  m_eventTexturesDone[source]->Set();

  CalculateTextureSourceRects(source, 3);

  glDisable(m_textureTarget);
}

void CLinuxRendererA10::DeleteYV12Texture(int index)
{
  YV12Image &im     = m_buffers[index].image;
  YUVFIELDS &fields = m_buffers[index].fields;

  if( fields[FIELD_FULL][0].id == 0 ) return;

  /* finish up all textures, and delete them */
  g_graphicsContext.BeginPaint();  //FIXME
  for(int f = 0;f<MAX_FIELDS;f++)
  {
    for(int p = 0;p<MAX_PLANES;p++)
    {
      if( fields[f][p].id )
      {
        if (glIsTexture(fields[f][p].id))
          glDeleteTextures(1, &fields[f][p].id);
        fields[f][p].id = 0;
      }
    }
  }
  g_graphicsContext.EndPaint();

  for(int p = 0;p<MAX_PLANES;p++)
  {
    if (im.plane[p])
    {
      delete [] im.plane[p];
      im.plane[p] = NULL;
    }
  }
}

bool CLinuxRendererA10::CreateYV12Texture(int index)
{
  /* since we also want the field textures, pitch must be texture aligned */
  YV12Image &im     = m_buffers[index].image;
  YUVFIELDS &fields = m_buffers[index].fields;

  DeleteYV12Texture(index);

  im.height = m_sourceHeight;
  im.width  = m_sourceWidth;
  im.cshift_x = 1;
  im.cshift_y = 1;

  im.stride[0] = im.width;
  im.stride[1] = im.width >> im.cshift_x;
  im.stride[2] = im.width >> im.cshift_x;

  im.planesize[0] = im.stride[0] * im.height;
  im.planesize[1] = im.stride[1] * ( im.height >> im.cshift_y );
  im.planesize[2] = im.stride[2] * ( im.height >> im.cshift_y );

  for (int i = 0; i < MAX_PLANES; i++)
    im.plane[i] = new BYTE[im.planesize[i]];

  glEnable(m_textureTarget);
  for(int f = 0;f<MAX_FIELDS;f++)
  {
    for(int p = 0;p<MAX_PLANES;p++)
    {
      if (!glIsTexture(fields[f][p].id))
      {
        glGenTextures(1, &fields[f][p].id);
        VerifyGLState();
      }
    }
  }

  // YUV
  for (int f = FIELD_FULL; f<=FIELD_BOT ; f++)
  {
    int fieldshift = (f==FIELD_FULL) ? 0 : 1;
    YUVPLANES &planes = fields[f];

    planes[0].texwidth  = im.width;
    planes[0].texheight = im.height >> fieldshift;

    planes[1].texwidth  = planes[0].texwidth  >> im.cshift_x;
    planes[1].texheight = planes[0].texheight >> im.cshift_y;
    planes[2].texwidth  = planes[0].texwidth  >> im.cshift_x;
    planes[2].texheight = planes[0].texheight >> im.cshift_y;

    for(int p = 0; p < 3; p++)
    {
      YUVPLANE &plane = planes[p];
      if (plane.texwidth * plane.texheight == 0)
        continue;

      glBindTexture(m_textureTarget, plane.id);
      CLog::Log(LOGDEBUG,  "GL: Creating YUV NPOT texture of size %d x %d", plane.texwidth, plane.texheight);

      glTexImage2D(m_textureTarget, 0, GL_LUMINANCE, plane.texwidth, plane.texheight, 0, GL_LUMINANCE, GL_UNSIGNED_BYTE, NULL);

      glTexParameteri(m_textureTarget, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
      glTexParameteri(m_textureTarget, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
      glTexParameteri(m_textureTarget, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
      glTexParameteri(m_textureTarget, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
      VerifyGLState();
    }
  }
  glDisable(m_textureTarget);
  m_eventTexturesDone[index]->Set();
  return true;
}

//********************************************************************************************************
// BYPASS creation, deletion, copying + clearing
//********************************************************************************************************
void CLinuxRendererA10::UploadBYPASSTexture(int index)
{
  m_eventTexturesDone[index]->Set();
}

void CLinuxRendererA10::DeleteBYPASSTexture(int index)
{
}

bool CLinuxRendererA10::CreateBYPASSTexture(int index)
{
  m_eventTexturesDone[index]->Set();
  return true;
}

void CLinuxRendererA10::SetTextureFilter(GLenum method)
{
  for (int i = 0 ; i<NUM_BUFFERS ; i++)
  {
    YUVFIELDS &fields = m_buffers[i].fields;

    for (int f = FIELD_FULL; f<=FIELD_BOT ; f++)
    {
      glBindTexture(m_textureTarget, fields[f][0].id);
      glTexParameteri(m_textureTarget, GL_TEXTURE_MIN_FILTER, method);
      glTexParameteri(m_textureTarget, GL_TEXTURE_MAG_FILTER, method);
      VerifyGLState();

      glBindTexture(m_textureTarget, fields[f][1].id);
      glTexParameteri(m_textureTarget, GL_TEXTURE_MIN_FILTER, method);
      glTexParameteri(m_textureTarget, GL_TEXTURE_MAG_FILTER, method);
      VerifyGLState();

      glBindTexture(m_textureTarget, fields[f][2].id);
      glTexParameteri(m_textureTarget, GL_TEXTURE_MIN_FILTER, method);
      glTexParameteri(m_textureTarget, GL_TEXTURE_MAG_FILTER, method);
      VerifyGLState();
    }
  }
}

bool CLinuxRendererA10::Supports(ERENDERFEATURE feature)
{
  // Player controls render, let it dictate available render features
  if((m_renderMethod & RENDER_BYPASS))
  {
    Features::iterator itr = std::find(m_renderFeatures.begin(),m_renderFeatures.end(), feature);
    return itr != m_renderFeatures.end();
  }

  if(feature == RENDERFEATURE_BRIGHTNESS)
    return false;

  if(feature == RENDERFEATURE_CONTRAST)
    return false;

  if(feature == RENDERFEATURE_GAMMA)
    return false;

  if(feature == RENDERFEATURE_NOISE)
    return false;

  if(feature == RENDERFEATURE_SHARPNESS)
    return false;

  if (feature == RENDERFEATURE_NONLINSTRETCH)
    return false;

  if (feature == RENDERFEATURE_STRETCH         ||
      feature == RENDERFEATURE_CROP            ||
      feature == RENDERFEATURE_ZOOM            ||
      feature == RENDERFEATURE_VERTICAL_SHIFT  ||
      feature == RENDERFEATURE_PIXEL_RATIO     ||
      feature == RENDERFEATURE_POSTPROCESS     ||
      feature == RENDERFEATURE_ROTATION)
    return true;


  return false;
}

bool CLinuxRendererA10::SupportsMultiPassRendering()
{
  return false;
}

bool CLinuxRendererA10::Supports(EDEINTERLACEMODE mode)
{
  // Player controls render, let it dictate available deinterlace modes
  if((m_renderMethod & RENDER_BYPASS))
  {
    Features::iterator itr = std::find(m_deinterlaceModes.begin(),m_deinterlaceModes.end(), mode);
    return itr != m_deinterlaceModes.end();
  }

  if (mode == VS_DEINTERLACEMODE_OFF)
    return true;

  if(mode == VS_DEINTERLACEMODE_AUTO || mode == VS_DEINTERLACEMODE_FORCE)
    return true;

  return false;
}

bool CLinuxRendererA10::Supports(EINTERLACEMETHOD method)
{
  // Player controls render, let it dictate available deinterlace methods
  if((m_renderMethod & RENDER_BYPASS))
  {
    Features::iterator itr = std::find(m_deinterlaceMethods.begin(),m_deinterlaceMethods.end(), method);
    return itr != m_deinterlaceMethods.end();
  }

  if(method == VS_INTERLACEMETHOD_AUTO)
    return true;

#if defined(__i386__) || defined(__x86_64__)
  if(method == VS_INTERLACEMETHOD_DEINTERLACE
  || method == VS_INTERLACEMETHOD_DEINTERLACE_HALF
  || method == VS_INTERLACEMETHOD_SW_BLEND)
#else
  if(method == VS_INTERLACEMETHOD_SW_BLEND)
#endif
    return true;

  return false;
}

bool CLinuxRendererA10::Supports(ESCALINGMETHOD method)
{
  // Player controls render, let it dictate available scaling methods
  if((m_renderMethod & RENDER_BYPASS))
  {
    Features::iterator itr = std::find(m_scalingMethods.begin(),m_scalingMethods.end(), method);
    return itr != m_scalingMethods.end();
  }

  if(method == VS_SCALINGMETHOD_NEAREST
  || method == VS_SCALINGMETHOD_LINEAR)
    return true;

  return false;
}

EINTERLACEMETHOD CLinuxRendererA10::AutoInterlaceMethod()
{
  // Player controls render, let it pick the auto-deinterlace method
  if((m_renderMethod & RENDER_BYPASS))
  {
    if (m_deinterlaceMethods.size())
      return ((EINTERLACEMETHOD)m_deinterlaceMethods[0]);
    else
      return VS_INTERLACEMETHOD_NONE;
  }

#if defined(__i386__) || defined(__x86_64__)
  return VS_INTERLACEMETHOD_DEINTERLACE_HALF;
#else
  return VS_INTERLACEMETHOD_SW_BLEND;
#endif
}

void CLinuxRendererA10::AddProcessor(struct A10VLQueueItem *buffer)
{
  YUVBUFFER &buf = m_buffers[NextYV12Texture()];

  buf.a10buffer = buffer;
}

/*
 * Video layer functions
 */

static int             g_hfb = -1;
static int             g_hdisp = -1;
static int             g_screenid = 0;
static int             g_syslayer = 0x64;
static int             g_hlayer = 0;
static int             g_width;
static int             g_height;
static CRect           g_srcRect;
static CRect           g_dstRect;
static int             g_lastnr;
static int             g_decnr;
static int             g_wridx;
static int             g_rdidx;
static A10VLQueueItem  g_dispq[DISPQS];
static pthread_mutex_t g_dispq_mutex;

static bool A10VLBlueScreenFix()
{
  int hlayer;
  __disp_layer_info_t layera;
  unsigned long args[4];

  args[0] = g_screenid;
  args[1] = DISP_LAYER_WORK_MODE_SCALER;
  args[2] = 0;
  args[3] = 0;
  hlayer = ioctl(g_hdisp, DISP_CMD_LAYER_REQUEST, args);
  if (hlayer <= 0)
  {
    CLog::Log(LOGERROR, "A10: DISP_CMD_LAYER_REQUEST failed.\n");
    return false;
  }

  args[0] = g_screenid;
  args[1] = hlayer;
  args[2] = (unsigned long) &layera;
  args[3] = 0;
  ioctl(g_hdisp, DISP_CMD_LAYER_GET_PARA, args);

  layera.mode = DISP_LAYER_WORK_MODE_SCALER;
  layera.fb.mode = DISP_MOD_MB_UV_COMBINED;
  layera.fb.format = DISP_FORMAT_YUV420;
  layera.fb.seq = DISP_SEQ_UVUV;
  ioctl(g_hdisp, DISP_CMD_LAYER_SET_PARA, args);

  args[0] = g_screenid;
  args[1] = hlayer;
  args[2] = 0;
  args[3] = 0;
  ioctl(g_hdisp, DISP_CMD_LAYER_RELEASE, args);

  return true;
}

bool A10VLInit(int &width, int &height, double &refreshRate)
{
  unsigned long       args[4];
  __disp_layer_info_t layera;
  unsigned int        i;

  pthread_mutex_init(&g_dispq_mutex, NULL);

  g_hfb = open("/dev/fb0", O_RDWR);

  g_hdisp = open("/dev/disp", O_RDWR);
  if (g_hdisp == -1)
  {
    CLog::Log(LOGERROR, "A10: open /dev/disp failed. (%d)", errno);
    return false;
  }

  args[0] = g_screenid;
  args[1] = 0;
  args[2] = 0;
  args[3] = 0;
  width  = g_width  = ioctl(g_hdisp, DISP_CMD_SCN_GET_WIDTH , args);
  height = g_height = ioctl(g_hdisp, DISP_CMD_SCN_GET_HEIGHT, args);

  i = ioctl(g_hdisp, DISP_CMD_HDMI_GET_MODE, args);

  switch(i)
  {
  case DISP_TV_MOD_720P_50HZ:
  case DISP_TV_MOD_1080I_50HZ:
  case DISP_TV_MOD_1080P_50HZ:
    refreshRate = 50.0;
    break;
  case DISP_TV_MOD_720P_60HZ:
  case DISP_TV_MOD_1080I_60HZ:
  case DISP_TV_MOD_1080P_60HZ:
    refreshRate = 60.0;
    break;
  case DISP_TV_MOD_1080P_24HZ:
    refreshRate = 24.0;
    break;
  default:
    CLog::Log(LOGERROR, "A10: display mode %d is unknown. Assume refreh rate 60Hz\n", i);
    refreshRate = 60.0;
    break;
  }

  if ((g_height > 720) && (getenv("A10AB") == NULL))
  {
    //set workmode scaler (system layer)
    args[0] = g_screenid;
    args[1] = g_syslayer;
    args[2] = (unsigned long) (&layera);
    args[3] = 0;
    ioctl(g_hdisp, DISP_CMD_LAYER_GET_PARA, args);
    layera.mode = DISP_LAYER_WORK_MODE_SCALER;
    args[0] = g_screenid;
    args[1] = g_syslayer;
    args[2] = (unsigned long) (&layera);
    args[3] = 0;
    ioctl(g_hdisp, DISP_CMD_LAYER_SET_PARA, args);
  }
  else
  {
    //set workmode normal (system layer)
    args[0] = g_screenid;
    args[1] = g_syslayer;
    args[2] = (unsigned long) (&layera);
    args[3] = 0;
    ioctl(g_hdisp, DISP_CMD_LAYER_GET_PARA, args);
    //source window information
    layera.src_win.x      = 0;
    layera.src_win.y      = 0;
    layera.src_win.width  = g_width;
    layera.src_win.height = g_height;
    //screen window information
    layera.scn_win.x      = 0;
    layera.scn_win.y      = 0;
    layera.scn_win.width  = g_width;
    layera.scn_win.height = g_height;
    layera.mode = DISP_LAYER_WORK_MODE_NORMAL;
    args[0] = g_screenid;
    args[1] = g_syslayer;
    args[2] = (unsigned long) (&layera);
    args[3] = 0;
    ioctl(g_hdisp, DISP_CMD_LAYER_SET_PARA, args);

  }

  for (i = 0x65; i <= 0x67; i++)
  {
    //release possibly lost allocated layers
    args[0] = g_screenid;
    args[1] = i;
    args[2] = 0;
    args[3] = 0;
    ioctl(g_hdisp, DISP_CMD_LAYER_RELEASE, args);
  }

  // Hack: avoid blue picture background
  if (!A10VLBlueScreenFix())
    return false;

  args[0] = g_screenid;
  args[1] = DISP_LAYER_WORK_MODE_SCALER;
  args[2] = 0;
  args[3] = 0;
  g_hlayer = ioctl(g_hdisp, DISP_CMD_LAYER_REQUEST, args);
  if (g_hlayer <= 0)
  {
    g_hlayer = 0;
    CLog::Log(LOGERROR, "A10: DISP_CMD_LAYER_REQUEST failed.\n");
    return false;
  }

  memset(&g_srcRect, 0, sizeof(g_srcRect));
  memset(&g_dstRect, 0, sizeof(g_dstRect));

  g_lastnr = -1;
  g_decnr  = 0;
  g_rdidx  = 0;
  g_wridx  = 0;

  for (i = 0; i < DISPQS; i++)
    g_dispq[i].pict.id = -1;

  return true;
}

void A10VLExit()
{
  unsigned long args[4];

  if (g_hlayer)
  {
    //stop video
    args[0] = g_screenid;
    args[1] = g_hlayer;
    args[2] = 0;
    args[3] = 0;
    ioctl(g_hdisp, DISP_CMD_VIDEO_STOP, args);

    //close layer
    args[0] = g_screenid;
    args[1] = g_hlayer;
    args[2] = 0;
    args[3] = 0;
    ioctl(g_hdisp, DISP_CMD_LAYER_CLOSE, args);

    //release layer
    args[0] = g_screenid;
    args[1] = g_hlayer;
    args[2] = 0;
    args[3] = 0;
    ioctl(g_hdisp, DISP_CMD_LAYER_RELEASE, args);
    g_hlayer = 0;
  }
  if (g_hdisp != -1)
  {
    close(g_hdisp);
    g_hdisp = -1;
  }
  if (g_hfb != -1)
  {
    close(g_hfb);
    g_hfb = -1;
  }
}

void A10VLHide()
{
  unsigned long args[4];

  if (g_hlayer)
  {
    //stop video
    args[0] = g_screenid;
    args[1] = g_hlayer;
    args[2] = 0;
    args[3] = 0;
    ioctl(g_hdisp, DISP_CMD_VIDEO_STOP, args);

    //close layer
    args[0] = g_screenid;
    args[1] = g_hlayer;
    args[2] = 0;
    args[3] = 0;
    ioctl(g_hdisp, DISP_CMD_LAYER_CLOSE, args);
  }

  memset(&g_srcRect, 0, sizeof(g_srcRect));
  memset(&g_dstRect, 0, sizeof(g_dstRect));
}

#define FBIO_WAITFORVSYNC _IOW('F', 0x20, u32)

void A10VLWaitVSYNC()
{
  //ioctl(g_hfb, FBIO_WAITFORVSYNC, NULL);
}

A10VLQueueItem *A10VLPutQueue(A10VLCALLBACK     callback,
                              void             *callbackpriv,
                              void             *pictpriv,
                              cedarv_picture_t &pict)
{
  A10VLQueueItem *pRet;

  pthread_mutex_lock(&g_dispq_mutex);

  pRet = &g_dispq[g_wridx];

  pRet->decnr        = g_decnr++;
  pRet->callback     = callback;
  pRet->callbackpriv = callbackpriv;
  pRet->pictpriv     = pictpriv;
  pRet->pict         = pict;

  g_wridx++;
  if (g_wridx >= DISPQS)
    g_wridx = 0;

  pthread_mutex_unlock(&g_dispq_mutex);

  return pRet;
}

static void A10VLFreeQueueItem(A10VLQueueItem *pItem)
{
  if ((int)pItem->pict.id != -1)
  {
    if (pItem->callback)
      pItem->callback(pItem->callbackpriv, pItem->pictpriv, pItem->pict);
    pItem->pict.id = -1;
  }
}

void A10VLFreeQueue()
{
  int i;

  pthread_mutex_lock(&g_dispq_mutex);

  for (i = 0; i < DISPQS; i++)
    A10VLFreeQueueItem(&g_dispq[i]);

  pthread_mutex_unlock(&g_dispq_mutex);
}

void A10VLDisplayQueueItem(A10VLQueueItem *pItem, CRect &srcRect, CRect &dstRect)
{
  int i;
  int curnr;

  pthread_mutex_lock(&g_dispq_mutex);

  if (!pItem || (pItem->pict.id == -1) || (g_lastnr == pItem->decnr))
  {
    pthread_mutex_unlock(&g_dispq_mutex);
    return;
  }

  curnr = A10VLDisplayPicture(pItem->pict, pItem->decnr, srcRect, dstRect);

  if (curnr != g_lastnr)
  {
    //free older frames, displayed or not
    for (i = 0; i < DISPQS; i++)
    {
      if(g_dispq[g_rdidx].decnr < curnr)
      {
        A10VLFreeQueueItem(&g_dispq[g_rdidx]);

        g_rdidx++;
        if (g_rdidx >= DISPQS)
          g_rdidx = 0;

      } else break;
    }

  }

  g_lastnr = curnr;

  pthread_mutex_unlock(&g_dispq_mutex);
}

int A10VLDisplayPicture(cedarv_picture_t &picture,
                        int               refnr,
                        CRect            &srcRect,
                        CRect            &dstRect)
{
  unsigned long       args[4];
  __disp_layer_info_t layera;
  __disp_video_fb_t   frmbuf;
  __disp_colorkey_t   colorkey;

  memset(&frmbuf, 0, sizeof(__disp_video_fb_t));
  frmbuf.id              = refnr;
  frmbuf.interlace       = picture.is_progressive? 0 : 1;
  frmbuf.top_field_first = picture.top_field_first;
  //frmbuf.frame_rate      = picture.frame_rate;
#ifdef CEDARV_FRAME_HAS_PHY_ADDR
  frmbuf.addr[0]         = (u32)picture.y;
  frmbuf.addr[1]         = (u32)picture.u;
#else
  frmbuf.addr[0]         = mem_get_phy_addr((u32)picture.y);
  frmbuf.addr[1]         = mem_get_phy_addr((u32)picture.u);
#endif

  if ((g_srcRect != srcRect) || (g_dstRect != dstRect))
  {
    args[0] = g_screenid;
    args[1] = g_hlayer;
    args[2] = (unsigned long) (&layera);
    args[3] = 0;
    ioctl(g_hdisp, DISP_CMD_LAYER_GET_PARA, args);
    //set video layer attribute
    layera.mode          = DISP_LAYER_WORK_MODE_SCALER;
    layera.b_from_screen = 0; //what is this? if enabled all is black
    layera.pipe          = 1;
    //use alpha blend
    layera.alpha_en      = 0;
    layera.alpha_val     = 0xff;
    layera.ck_enable     = 0;
    layera.b_trd_out     = 0;
    layera.out_trd_mode  = (__disp_3d_out_mode_t)0;
    //frame buffer pst and size information
    if (picture.display_height < 720)
    {
      layera.fb.cs_mode = DISP_BT601;
    }
    else
    {
      layera.fb.cs_mode = DISP_BT709;
    }
    layera.fb.mode        = DISP_MOD_MB_UV_COMBINED;
    layera.fb.format      = picture.pixel_format == CEDARV_PIXEL_FORMAT_AW_YUV422 ? DISP_FORMAT_YUV422 : DISP_FORMAT_YUV420;
    layera.fb.br_swap     = 0;
    layera.fb.seq         = DISP_SEQ_UVUV;
    layera.fb.addr[0]     = frmbuf.addr[0];
    layera.fb.addr[1]     = frmbuf.addr[1];
    layera.fb.b_trd_src   = 0;
    layera.fb.trd_mode    = (__disp_3d_src_mode_t)0;
    layera.fb.size.width  = picture.display_width;
    layera.fb.size.height = picture.display_height;
    //source window information
    layera.src_win.x      = lrint(srcRect.x1);
    layera.src_win.y      = lrint(srcRect.y1);
    layera.src_win.width  = lrint(srcRect.x2-srcRect.x1);
    layera.src_win.height = lrint(srcRect.y2-srcRect.y1);
    //screen window information
    layera.scn_win.x      = lrint(dstRect.x1);
    layera.scn_win.y      = lrint(dstRect.y1);
    layera.scn_win.width  = lrint(dstRect.x2-dstRect.x1);
    layera.scn_win.height = lrint(dstRect.y2-dstRect.y1);

    CLog::Log(LOGDEBUG, "A10: srcRect=(%lf,%lf)-(%lf,%lf)\n", srcRect.x1, srcRect.y1, srcRect.x2, srcRect.y2);
    CLog::Log(LOGDEBUG, "A10: dstRect=(%lf,%lf)-(%lf,%lf)\n", dstRect.x1, dstRect.y1, dstRect.x2, dstRect.y2);

    if (    (layera.scn_win.x < 0)
         || (layera.scn_win.y < 0)
         || (layera.scn_win.width  > g_width)
         || (layera.scn_win.height > g_height)    )
    {
      double xzoom, yzoom;

      //TODO: this calculation is against the display fullscreen dimensions,
      //but should be against the fullscreen area of xbmc

      xzoom = (dstRect.x2 - dstRect.x1) / (srcRect.x2 - srcRect.x1);
      yzoom = (dstRect.y2 - dstRect.y1) / (srcRect.y2 - srcRect.x1);

      if (layera.scn_win.x < 0)
      {
        layera.src_win.x -= layera.scn_win.x / xzoom;
        layera.scn_win.x = 0;
      }
      if (layera.scn_win.width > g_width)
      {
        layera.src_win.width -= (layera.scn_win.width - g_width) / xzoom;
        layera.scn_win.width = g_width;
      }

      if (layera.scn_win.y < 0)
      {
        layera.src_win.y -= layera.scn_win.y / yzoom;
        layera.scn_win.y = 0;
      }
      if (layera.scn_win.height > g_height)
      {
        layera.src_win.height -= (layera.scn_win.height - g_height) / yzoom;
        layera.scn_win.height = g_height;
      }
    }

    args[0] = g_screenid;
    args[1] = g_hlayer;
    args[2] = (unsigned long)&layera;
    args[3] = 0;
    if(ioctl(g_hdisp, DISP_CMD_LAYER_SET_PARA, args))
      CLog::Log(LOGERROR, "A10: DISP_CMD_LAYER_SET_PARA failed.\n");

    //open layer
    args[0] = g_screenid;
    args[1] = g_hlayer;
    args[2] = 0;
    args[3] = 0;
    if (ioctl(g_hdisp, DISP_CMD_LAYER_OPEN, args))
      CLog::Log(LOGERROR, "A10: DISP_CMD_LAYER_OPEN failed.\n");

    //put behind system layer
    args[0] = g_screenid;
    args[1] = g_hlayer;
    args[2] = 0;
    args[3] = 0;
    if (ioctl(g_hdisp, DISP_CMD_LAYER_BOTTOM, args))
      CLog::Log(LOGERROR, "A10: DISP_CMD_LAYER_BOTTOM failed.\n");

    //turn off colorkey (system layer)
    args[0] = g_screenid;
    args[1] = g_syslayer;
    args[2] = 0;
    args[3] = 0;
    if (ioctl(g_hdisp, DISP_CMD_LAYER_CK_OFF, args))
      CLog::Log(LOGERROR, "A10: DISP_CMD_LAYER_CK_OFF failed.\n");

    if ((g_height > 720) && (getenv("A10AB") == NULL))
    {
      //no tearing at the cost off alpha blending...

      //set colorkey
      colorkey.ck_min.alpha = 0;
      colorkey.ck_min.red   = 1;
      colorkey.ck_min.green = 2;
      colorkey.ck_min.blue  = 3;
      colorkey.ck_max = colorkey.ck_min;
      colorkey.ck_max.alpha = 255;
      colorkey.red_match_rule   = 2;
      colorkey.green_match_rule = 2;
      colorkey.blue_match_rule  = 2;

      args[0] = g_screenid;
      args[1] = (unsigned long)&colorkey;
      args[2] = 0;
      args[3] = 0;
      if (ioctl(g_hdisp, DISP_CMD_SET_COLORKEY, args))
        CLog::Log(LOGERROR, "A10: DISP_CMD_SET_COLORKEY failed.\n");

      //turn on colorkey
      args[0] = g_screenid;
      args[1] = g_hlayer;
      args[2] = 0;
      args[3] = 0;
      if (ioctl(g_hdisp, DISP_CMD_LAYER_CK_ON, args))
        CLog::Log(LOGERROR, "A10: DISP_CMD_LAYER_CK_ON failed.\n");

      //turn on global alpha (system layer)
      args[0] = g_screenid;
      args[1] = g_syslayer;
      args[2] = 0;
      args[3] = 0;
      if (ioctl(g_hdisp, DISP_CMD_LAYER_ALPHA_ON, args))
        CLog::Log(LOGERROR, "A10: DISP_CMD_LAYER_ALPHA_ON failed.\n");
    }
    else
    {
      //turn off global alpha (system layer)
      args[0] = g_screenid;
      args[1] = g_syslayer;
      args[2] = 0;
      args[3] = 0;
      if (ioctl(g_hdisp, DISP_CMD_LAYER_ALPHA_OFF, args))
        CLog::Log(LOGERROR, "A10: DISP_CMD_LAYER_ALPHA_OFF failed.\n");
    }

    //enable vpp
    args[0] = g_screenid;
    args[1] = g_hlayer;
    args[2] = 0;
    args[3] = 0;
    if (ioctl(g_hdisp, DISP_CMD_LAYER_VPP_ON, args))
      CLog::Log(LOGERROR, "A10: DISP_CMD_LAYER_VPP_ON failed.\n");

    //enable enhance
    args[0] = g_screenid;
    args[1] = g_hlayer;
    args[2] = 0;
    args[3] = 0;
    if (ioctl(g_hdisp, DISP_CMD_LAYER_ENHANCE_ON, args))
      CLog::Log(LOGERROR, "A10: DISP_CMD_LAYER_ENHANCE_ON failed.\n");

    //start video
    args[0] = g_screenid;
    args[1] = g_hlayer;
    args[2] = 0;
    args[3] = 0;
    if (ioctl(g_hdisp, DISP_CMD_VIDEO_START, args))
      CLog::Log(LOGERROR, "A10: DISP_CMD_VIDEO_START failed.\n");

    g_srcRect = srcRect;
    g_dstRect = dstRect;
  }

  args[0] = g_screenid;
  args[1] = g_hlayer;
  args[2] = (unsigned long)&frmbuf;
  args[3] = 0;
  if (ioctl(g_hdisp, DISP_CMD_VIDEO_SET_FB, args))
    CLog::Log(LOGERROR, "A10: DISP_CMD_VIDEO_SET_FB failed.\n");

  //CLog::Log(LOGDEBUG, "A10: render %d\n", buffer->picture.id);

  args[0] = g_screenid;
  args[1] = g_hlayer;
  args[2] = 0;
  args[3] = 0;
  return ioctl(g_hdisp, DISP_CMD_VIDEO_GET_FRAME_ID, args);
}

#endif

