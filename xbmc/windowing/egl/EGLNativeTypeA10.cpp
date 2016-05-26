/*
 *      Copyright (C) 2011-2012 Team XBMC
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
#include <EGL/egl.h>
#include "EGL/eglplatform.h"
#include "EGLNativeTypeA10.h"
#include "utils/log.h"
#include "guilib/gui3d.h"
#include "utils/StringUtils.h"

#include <sys/ioctl.h>
#include <drv_display_sun4i.h>
#ifndef CEDARV_FRAME_HAS_PHY_ADDR
#include <os_adapter.h>
#endif

#if defined(ALLWINNERA10) && !defined(TARGET_ANDROID)

struct mali_native_window {
        unsigned short width;
        unsigned short height;
};

static struct mali_native_window g_fbwin;
static double       g_refreshRate;
#endif

static int             g_hfb = -1;
static int             g_hdisp = -1;
static int             m_screenid = 0;
//static int             g_syslayer = 0x64;
static int             g_hlayer = 0;
static int             g_width;
static int             g_height;

CEGLNativeTypeA10::CEGLNativeTypeA10()
{
#if defined(ALLWINNERA10) && !defined(TARGET_ANDROID)
  int width, height;

  VLInit(width, height, g_refreshRate);
  g_fbwin.width  = width;
  g_fbwin.height = height;
#endif
}

CEGLNativeTypeA10::~CEGLNativeTypeA10()
{
#if defined(ALLWINNERA10) && !defined(TARGET_ANDROID) 
  VLExit();
#endif
} 

bool CEGLNativeTypeA10::CheckCompatibility()
{
#if defined(ALLWINNERA10) && !defined(TARGET_ANDROID)
  return true;
#endif
  return false;
}

void CEGLNativeTypeA10::Initialize()
{
  return;
}
void CEGLNativeTypeA10::Destroy()
{
  return;
}

bool CEGLNativeTypeA10::CreateNativeDisplay()
{
  m_nativeDisplay = EGL_DEFAULT_DISPLAY;
  return true;
}

bool CEGLNativeTypeA10::CreateNativeWindow()
{
#if defined(ALLWINNERA10) && !defined(TARGET_ANDROID)
  m_nativeWindow = &g_fbwin;
  return true;
#else
  return false;
#endif
}  

bool CEGLNativeTypeA10::GetNativeDisplay(XBNativeDisplayType **nativeDisplay) const
{
  if (!nativeDisplay)
    return false;
  *nativeDisplay = (XBNativeDisplayType*) &m_nativeDisplay;
  return true;
}

bool CEGLNativeTypeA10::GetNativeWindow(XBNativeWindowType **nativeWindow) const
{
  if (!nativeWindow)
    return false;
  *nativeWindow = (XBNativeWindowType*) &m_nativeWindow;
  return true;
}

bool CEGLNativeTypeA10::DestroyNativeDisplay()
{
  return true;
}

bool CEGLNativeTypeA10::DestroyNativeWindow()
{
  return true;
}

bool CEGLNativeTypeA10::GetNativeResolution(RESOLUTION_INFO *res) const
{
#if defined(ALLWINNERA10) && !defined(TARGET_ANDROID)
  res->iWidth = g_fbwin.width;
  res->iHeight= g_fbwin.height;

  res->fRefreshRate = g_refreshRate;
  res->dwFlags= D3DPRESENTFLAG_PROGRESSIVE | D3DPRESENTFLAG_WIDESCREEN;
  res->iScreen       = 0;
  res->bFullScreen   = true;
  res->iSubtitles    = (int)(0.965 * res->iHeight);
  res->fPixelRatio   = 1.0f;
  res->iScreenWidth  = res->iWidth;
  res->iScreenHeight = res->iHeight;
  res->strMode       = StringUtils::Format("%dx%d @ %.2f%s - Full Screen", res->iScreenWidth, res->iScreenHeight, res->fRefreshRate,
  res->dwFlags & D3DPRESENTFLAG_INTERLACED ? "i" : "");
  CLog::Log(LOGNOTICE,"Current resolution: %s\n",res->strMode.c_str());
  return true;
#else
  return false;
#endif
}

bool CEGLNativeTypeA10::SetNativeResolution(const RESOLUTION_INFO &res)
{
  return false;
}

bool CEGLNativeTypeA10::ProbeResolutions(std::vector<RESOLUTION_INFO> &resolutions)
{
  RESOLUTION_INFO res;
  bool ret = false;
  ret = GetNativeResolution(&res);
  if (ret && res.iWidth > 1 && res.iHeight > 1)
  {
    resolutions.push_back(res);
    return true;
  }
  return false;
}

bool CEGLNativeTypeA10::GetPreferredResolution(RESOLUTION_INFO *res) const
{
  return false;
}

bool CEGLNativeTypeA10::ShowWindow(bool show)
{
  return false;
}

bool CEGLNativeTypeA10::GetVideoLayerHandle(void*& handle)
{
  bool ret = false;
  if(m_hVideoLayer != 0)
  {
    handle = (void*)m_hVideoLayer;
    ret = true;
  }
  return ret;
}

bool CEGLNativeTypeA10::GetDispIdHandle(void*& handle)
{
  bool ret = false;
  if(m_hVideoLayer != 0)
  {
    handle = (void*)g_hdisp;
    ret = true;
  }
  return ret;
}

bool CEGLNativeTypeA10::VLInit(int &width, int &height, double &refreshRate)
{
  unsigned long       args[4];
  __disp_layer_info_t layera;
  unsigned int        i;

  g_hfb = open("/dev/fb0", O_RDWR);

  g_hdisp = open("/dev/disp", O_RDWR);
  if (g_hdisp == -1)
  {
    CLog::Log(LOGERROR, "A10: open /dev/disp failed. (%d)", errno);
    return false;
  }
#if 0
  int ver = SUNXI_DISP_VERSION;
  if (ioctl(g_hdisp, DISP_CMD_VERSION, &ver) < 0)
  {
    CLog::Log(LOGERROR, "A10: version failed. (%d)", errno);
    return false;
  }
#endif

  args[0] = m_screenid;
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

  if (ioctl(g_hfb, FBIOGET_LAYER_HDL_0, &m_hGuiLayer))
  {
    CLog::Log(LOGERROR, "A10: get fb0 layer handle failed. (%d)", errno);
    return false;
  }

  if ((g_height > 720) && (getenv("A10AB") == NULL))
  {
    //set workmode scaler (system layer)
    args[0] = m_screenid;
    args[1] = m_hGuiLayer;
    args[2] = (unsigned long) (&layera);
    args[3] = 0;
    ioctl(g_hdisp, DISP_CMD_LAYER_GET_PARA, args);
    layera.mode = DISP_LAYER_WORK_MODE_SCALER;
    args[0] = m_screenid;
    args[1] = m_hGuiLayer;
    args[2] = (unsigned long) (&layera);
    args[3] = 0;
    ioctl(g_hdisp, DISP_CMD_LAYER_SET_PARA, args);
  }
  else
  {
    //set workmode normal (system layer)
    args[0] = m_screenid;
    args[1] = m_hGuiLayer;
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
    args[0] = m_screenid;
    args[1] = m_hGuiLayer;
    args[2] = (unsigned long) (&layera);
    args[3] = 0;
    ioctl(g_hdisp, DISP_CMD_LAYER_SET_PARA, args);

  }

  
  for (i = 0x65; i <= 0x67; i++)
  {
    //release possibly lost allocated layers
    args[0] = m_screenid;
    args[1] = i;
    args[2] = 0;
    args[3] = 0;
    ioctl(g_hdisp, DISP_CMD_LAYER_RELEASE, args);
  }

  args[0] = m_screenid;
  args[1] = DISP_LAYER_WORK_MODE_SCALER;
  args[2] = 0;
  args[3] = 0;
  m_hVideoLayer = ioctl(g_hdisp, DISP_CMD_LAYER_REQUEST, args);
  if (m_hVideoLayer <= 0)
  {
    m_hVideoLayer = 0;
    CLog::Log(LOGERROR, "A10: DISP_CMD_LAYER_REQUEST failed.\n");
    return false;
  }

#if 0
  args[0] = m_screenid;
  args[1] = m_hVideoLayer;
  if (ioctl(g_hdisp, DISP_CMD_LAYER_OPEN, &args) < 0)
  {
    printf("layer open failed\n");
  }
#endif
#if 1
  __disp_colorkey_t ck;
  ck.ck_max.red = ck.ck_min.red = 0x0;
  ck.ck_max.green = ck.ck_min.green = 0x0;
  ck.ck_max.blue = ck.ck_min.blue = 0x0;
  ck.ck_max.alpha = ck.ck_min.alpha = 0xff;
  ck.red_match_rule = 2;
  ck.green_match_rule = 2;
  ck.blue_match_rule = 2;

  args[0] = m_hGuiLayer;
  args[1] = (unsigned long)(&ck);
  ioctl(g_hdisp, DISP_CMD_SET_COLORKEY, args);
#endif

#if 0
  args[0] = m_screenid;
  args[1] = m_hVideoLayer;
  args[2] = (unsigned long) (&layera);
  args[3] = 0;
  ioctl(g_hdisp, DISP_CMD_LAYER_GET_PARA, args);
  layera.src_win.x      = 0;
  layera.src_win.y      = 0;
  layera.src_win.width  = g_width;
  layera.src_win.height = g_height;
    //screen window information
  layera.scn_win.x      = 0;
  layera.scn_win.y      = 0;
  layera.scn_win.width  = g_width;
  layera.scn_win.height = g_height;
  layera.mode = DISP_LAYER_WORK_MODE_SCALER;
  layera.alpha_en = 1;
  layera.alpha_val = 0xff;
  layera.pipe = 1;
  args[0] = m_screenid;
  args[1] = m_hVideoLayer;
  args[2] = (unsigned long) (&layera);
  args[3] = 0;
  ioctl(g_hdisp, DISP_CMD_LAYER_SET_PARA, args);
#endif

  args[0] = m_screenid;
  args[1] = m_hVideoLayer;
  args[2] = 0;
  args[3] = 0;
  if (ioctl(g_hdisp, DISP_CMD_LAYER_TOP, args))
  {
    CLog::Log(LOGERROR, "A10: DISP_CMD_LAYER_BOTTOM video layer failed.\n");
    return false;
  }

  args[0] = m_screenid;
  args[1] = m_hGuiLayer;
  args[2] = 0;
  args[3] = 0;
  if (ioctl(g_hdisp, DISP_CMD_LAYER_TOP, args))
  {
    CLog::Log(LOGERROR, "A10: DISP_CMD_LAYER_BOTTOM video layer failed.\n");
    return false;
  }
  
  args[0] = m_screenid;
  args[1] = m_hGuiLayer;
  if (ioctl(g_hdisp, DISP_CMD_LAYER_ALPHA_OFF, &args) < 0)
  {
    printf("alpha on failed\n");
  }

  args[0] = m_screenid;
  args[1] = m_hGuiLayer;
  args[2] = 0x0;
  if (ioctl(g_hdisp,DISP_CMD_LAYER_SET_ALPHA_VALUE,(void*)args) < 0)
  {
    printf("set alpha value failed\n");
  }

  return true;
}

void CEGLNativeTypeA10::VLExit()
{
  unsigned long args[4];

  if (m_hVideoLayer)
  {
    //stop video
    args[0] = m_screenid;
    args[1] = m_hVideoLayer;
    args[2] = 0;
    args[3] = 0;
    ioctl(g_hdisp, DISP_CMD_VIDEO_STOP, args);

    //close layer
    args[0] = m_screenid;
    args[1] = m_hVideoLayer;
    args[2] = 0;
    args[3] = 0;
    ioctl(g_hdisp, DISP_CMD_LAYER_CLOSE, args);

    //release layer
    args[0] = m_screenid;
    args[1] = m_hVideoLayer;
    args[2] = 0;
    args[3] = 0;
    ioctl(g_hdisp, DISP_CMD_LAYER_RELEASE, args);
    m_hVideoLayer = 0;
  }
  if (m_hGuiLayer)
  {
    //close layer
    args[0] = m_screenid;
    args[1] = m_hGuiLayer;
    args[2] = 0;
    args[3] = 0;
    ioctl(g_hdisp, DISP_CMD_LAYER_CLOSE, args);

    //release layer
    args[0] = m_screenid;
    args[1] = m_hGuiLayer;
    args[2] = 0;
    args[3] = 0;
    ioctl(g_hdisp, DISP_CMD_LAYER_RELEASE, args);
    m_hGuiLayer = 0;

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


