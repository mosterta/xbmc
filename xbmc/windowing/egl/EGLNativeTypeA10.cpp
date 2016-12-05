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
#include "utils/CPUInfo.h"

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

CEGLNativeTypeA10::CEGLNativeTypeA10() : m_hfb(-1), m_hdisp(-1), m_screenid(0)
{
}

CEGLNativeTypeA10::~CEGLNativeTypeA10()
{
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
#if defined(ALLWINNERA10) && !defined(TARGET_ANDROID)
  int width, height;

  VLInit(width, height, g_refreshRate);
  g_fbwin.width  = width;
  g_fbwin.height = height;
#endif
  return;
}
void CEGLNativeTypeA10::Destroy()
{
#if defined(ALLWINNERA10) && !defined(TARGET_ANDROID) 
  VLExit();
#endif
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

int CEGLNativeTypeA10::GetWidth()
{
  unsigned long       args[4];
  args[0] = m_screenid;
  args[1] = 0;
  args[2] = 0;
  args[3] = 0;
  return ioctl(m_hdisp, DISP_CMD_SCN_GET_WIDTH , args);
}

int CEGLNativeTypeA10::GetHeight()
{
  unsigned long       args[4];
  args[0] = m_screenid;
  args[1] = 0;
  args[2] = 0;
  args[3] = 0;

  return ioctl(m_hdisp, DISP_CMD_SCN_GET_HEIGHT, args);
}

int CEGLNativeTypeA10::GetRefreshRate()
{
  unsigned long       args[4];
  unsigned int        i;
  int _refreshRate;

  args[0] = m_screenid;
  args[1] = 0;
  args[2] = 0;
  args[3] = 0;
  i = ioctl(m_hdisp, DISP_CMD_HDMI_GET_MODE, args);
  switch(i)
  {
    case DISP_TV_MOD_720P_50HZ:
    case DISP_TV_MOD_1080I_50HZ:
    case DISP_TV_MOD_1080P_50HZ:
      _refreshRate = 50.0;
      break;
    case DISP_TV_MOD_720P_60HZ:
    case DISP_TV_MOD_1080I_60HZ:
    case DISP_TV_MOD_1080P_60HZ:
      _refreshRate = 60.0;
      break;
    case DISP_TV_MOD_1080P_24HZ:
      _refreshRate = 24.0;
      break;
    default:
      CLog::Log(LOGERROR, "A10: display mode %d is unknown. Assume refreh rate 60Hz\n", i);
      _refreshRate = 60.0;
      break;
  }
  return _refreshRate;
}

bool CEGLNativeTypeA10::VLInit(int &width, int &height, double &refreshRate)
{
  __disp_layer_info_t layera;
  int status;

  m_hfb = open("/dev/fb0", O_RDWR);

  m_hdisp = open("/dev/disp", O_RDWR);
  if (m_hdisp == -1)
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

  width = m_width  = GetWidth();
  height = m_height = GetHeight();
  refreshRate = GetRefreshRate();

  return true;
}

void CEGLNativeTypeA10::VLExit()
{
  if (m_hdisp != -1)
  {
    close(m_hdisp);
    m_hdisp = -1;
  }
  if (m_hfb != -1)
  {
    close(m_hfb);
    m_hfb = -1;
  }
}
