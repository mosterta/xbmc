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

#include "WinSystemSunxi.h"

#include <string.h>
#include <float.h>

#include "ServiceBroker.h"
#include "settings/SettingsComponent.h"
#include "cores/RetroPlayer/process/sunxi/RPProcessInfoSunxi.h"
#include "cores/RetroPlayer/rendering/VideoRenderers/RPRendererGuiTexture.h"
#include "cores/RetroPlayer/rendering/VideoRenderers/RPRendererOpenGLES.h"
#include "cores/VideoPlayer/VideoRenderers/LinuxRendererGLES.h"
#include "cores/VideoPlayer/VideoRenderers/HwDecRender/RendererVDPAUSunxi.h"
#include "cores/VideoPlayer/DVDCodecs/Video/VDPAU_Sunxi.h"
// AESink Factory
#include "cores/AudioEngine/AESinkFactory.h"
#include "cores/AudioEngine/Sinks/AESinkALSA.h"
//#include "guilib/GraphicContext.h"
//#include "guilib/Resolution.h"
#include "settings/Settings.h"
#include "settings/DisplaySettings.h"
#include "guilib/DispResource.h"
#include "utils/log.h"
#include "threads/SingleLock.h"
//#include "../WinEventsLinux.h"
#include "utils/StringUtils.h"
#include "utils/RegExp.h"
#include "platform/linux/SysfsPath.h"

#include <linux/fb.h>

#include <EGL/egl.h>
#include "EGL/eglplatform.h"

#include <sys/ioctl.h>

#include "windowing/allwinner/hwlayer/HwLayerFactory.h"
#include "windowing/allwinner/SunxiDisp.h"
#include "windowing/allwinner/SunxiDisp2.h"
#include "utils/CPUInfo.h"

using namespace KODI;

CWinSystemSunxi::CWinSystemSunxi() :
  m_libinput(new CLibInputHandler)
{
  const char *env_framebuffer = getenv("FRAMEBUFFER");

  // default to framebuffer 0
  m_framebuffer_name = "fb0";
  if (env_framebuffer)
  {
    std::string framebuffer(env_framebuffer);
    std::string::size_type start = framebuffer.find("fb");
    m_framebuffer_name = framebuffer.substr(start);
  }

  m_nativeDisplay = EGL_NO_DISPLAY;
  m_nativeWindow = nullptr;

  m_displayWidth = 0;
  m_displayHeight = 0;

  m_stereo_mode = RENDER_STEREO_MODE_OFF;
  m_delayDispReset = false;

  // Register sink
  AE::CAESinkFactory::ClearSinks();
  CAESinkALSA::Register();
  m_lirc.reset(OPTIONALS::LircRegister());
  m_libinput->Start();

  int fd;
      
  // Check if we can change the framebuffer resolution
  fd = open("/sys/class/graphics/fb0/mode", O_RDWR);
  if (fd >= 0)
  {
     CLog::Log(LOGINFO, "%s - graphics sysfs is writable\n", __FUNCTION__);
     m_readonly = false;
  }
  else
  {
     CLog::Log(LOGINFO, "%s - graphics sysfs is read-only\n", __FUNCTION__);
     m_readonly = true;
  }
  close(fd);
          
  fd = open("/dev/fb0",O_RDWR);
  if (fd < 0)
  {
     CLog::Log(LOGERROR, "%s - Error while opening /dev/fb0.\n", __FUNCTION__);
     return;
  }
  // Unblank the fb
  if (ioctl(fd, FBIOBLANK, 0) < 0)
  {
     CLog::Log(LOGERROR, "%s - Error while unblanking fb0.\n", __FUNCTION__);
  }
  close(fd);
  VLInit();
}

CWinSystemSunxi::~CWinSystemSunxi()
{
  if(m_nativeWindow)
  {
    m_nativeWindow = nullptr;
  }
  VLExit();
}

bool CWinSystemSunxi::InitWindowSystem()
{
  m_nativeDisplay = EGL_DEFAULT_DISPLAY;
  if (std::string("sun8i") == CServiceBroker::GetCPUInfo()->GetCPUHardware())
    m_dispBase.reset(new SunxiDisp2(m_hdisp, 0));
  else
    m_dispBase.reset(new SunxiDisp(m_hdisp, 0));

  CProcessInfoSunxi::Register();
  VDPAUSunxi::CDecoder::Register();
  //CLinuxRendererGLES::Register();
  RETRO::CRPProcessInfoSunxi::Register();
  RETRO::CRPProcessInfoSunxi::RegisterRendererFactory(new RETRO::CRendererFactoryOpenGLES);
  CRendererVDPAUSunxi::Register();

  CHwLayerManagerConfigAllwinner layerConfig;
  layerConfig.m_screenId = 0;
  layerConfig.m_fbNum = 0;
  g_HwLayer.initialize(layerConfig);
  g_HwLayer.setup();

  return CWinSystemBase::InitWindowSystem();
}

bool CWinSystemSunxi::DestroyWindowSystem()
{
  return true;
}

bool CWinSystemSunxi::CreateNewWindow(const std::string& name,
                                    bool fullScreen,
                                    RESOLUTION_INFO& res)
{
  RESOLUTION_INFO current_resolution;
  current_resolution.iWidth = current_resolution.iHeight = 0;
  RENDER_STEREO_MODE stereo_mode = CServiceBroker::GetWinSystem()->GetGfxContext().GetStereoMode();

  m_nWidth        = res.iWidth;
  m_nHeight       = res.iHeight;
  m_displayWidth  = res.iScreenWidth;
  m_displayHeight = res.iScreenHeight;
  m_fRefreshRate  = res.fRefreshRate;

  if ((m_bWindowCreated && GetNativeResolution(&current_resolution)) &&
    current_resolution.iWidth == res.iWidth && current_resolution.iHeight == res.iHeight &&
    current_resolution.iScreenWidth == res.iScreenWidth && current_resolution.iScreenHeight == res.iScreenHeight &&
    m_bFullScreen == fullScreen && current_resolution.fRefreshRate == res.fRefreshRate &&
    (current_resolution.dwFlags & D3DPRESENTFLAG_MODEMASK) == (res.dwFlags & D3DPRESENTFLAG_MODEMASK) &&
    m_stereo_mode == stereo_mode)
  {
    CLog::Log(LOGDEBUG, "CWinSystemEGL::CreateNewWindow: No need to create a new window");
    return true;
  }

  int delay = CServiceBroker::GetSettingsComponent()->GetSettings()->GetInt("videoscreen.delayrefreshchange");
  if (delay > 0)
  {
    m_delayDispReset = true;
    m_dispResetTimer.Set(delay * 100);
  }

  {
    CSingleLock lock(m_resourceSection);
    for (std::vector<IDispResource *>::iterator i = m_resources.begin(); i != m_resources.end(); ++i)
    {
      (*i)->OnLostDisplay();
    }
  }

  m_stereo_mode = stereo_mode;
  m_bFullScreen = fullScreen;

  if(m_nativeWindow)
    delete m_nativeWindow;
  mali_native_window *nativeWindow = new mali_native_window;
  nativeWindow->width = res.iWidth;
  nativeWindow->height = res.iHeight;
  m_nativeWindow = static_cast<EGLNativeWindowType>(nativeWindow);

  SetDisplayResolution(res, "fb0");
  SetFramebufferResolution(res, "fb0");

  if (!m_delayDispReset)
  {
    CSingleLock lock(m_resourceSection);
    // tell any shared resources
    for (std::vector<IDispResource *>::iterator i = m_resources.begin(); i != m_resources.end(); ++i)
    {
      (*i)->OnResetDisplay();
    }
  }

  return true;
}

bool CWinSystemSunxi::SetDisplayResolution(const RESOLUTION_INFO &res, std::string framebuffer_name)
{
  std::string mode = res.strId.c_str();
  mode += "\n";
  std::string cur_mode;

  CSysfsPath fbMode{"/sys/class/graphics/" + framebuffer_name + "/mode"};
  if(fbMode.Exists())
     cur_mode = fbMode.Get<std::string>();
  CLog::Log(LOGINFO, "CWinSystemSunxi::%s: current resolution '%s'",__FUNCTION__, cur_mode);

  if (cur_mode == mode)
  {
    // Don't set the same mode as current
    return true;
  }

  CLog::Log(LOGINFO, "CWinSystemSunxi::%s: changing to resolution '%s'",__FUNCTION__, mode);

  fbMode.Set<std::string>(mode.c_str());

  return true;
}

void CWinSystemSunxi::SetFramebufferResolution(const RESOLUTION_INFO &res, std::string framebuffer_name)
{
  SetFramebufferResolution(res.iWidth, res.iHeight, framebuffer_name);
}

void CWinSystemSunxi::SetFramebufferResolution(int width, int height, std::string framebuffer_name)
{
  int fd0;
  std::string framebuffer = "/dev/" + framebuffer_name;

  if ((fd0 = open(framebuffer.c_str(), O_RDWR)) >= 0)
  {
    struct fb_var_screeninfo vinfo;
    if (ioctl(fd0, FBIOGET_VSCREENINFO, &vinfo) == 0)
    {
      CLog::Log(LOGINFO, "CWinSystemSunxi::%s: FBIOGET_VSCREENINFO to width:%d, height:%d",__FUNCTION__, width, height);
      vinfo.xres = width;
      vinfo.yres = height;
      vinfo.xres_virtual = 1920;
      vinfo.yres_virtual = 2160;
      vinfo.bits_per_pixel = 32;
      vinfo.activate = FB_ACTIVATE_ALL;
      if(ioctl(fd0, FBIOPUT_VSCREENINFO, &vinfo)<0)
        CLog::Log(LOGWARNING, "CWinSystemSunxi::%s: FBIOSET_VSCREENINFO failed",__FUNCTION__);
    }
    close(fd0);
  }
}

bool CWinSystemSunxi::DestroyWindow()
{
  delete m_nativeWindow;
  m_nativeWindow = nullptr;

  return true;
}

void CWinSystemSunxi::UpdateResolutions()
{
  CWinSystemBase::UpdateResolutions();

  RESOLUTION_INFO resDesktop, curDisplay;
  std::vector<RESOLUTION_INFO> resolutions;

  if (!ProbeResolutions(resolutions) || resolutions.empty())
  {
    CLog::Log(LOGWARNING, "%s: ProbeResolutions failed.",__FUNCTION__);
  }

  /* ProbeResolutions includes already all resolutions.
   * Only get desktop resolution so we can replace xbmc's desktop res
   */
  if (GetNativeResolution(&curDisplay))
  {
    resDesktop = curDisplay;
  }

  RESOLUTION ResDesktop = RES_INVALID;
  RESOLUTION res_index  = RES_DESKTOP;

  for (size_t i = 0; i < resolutions.size(); i++)
  {
    // if this is a new setting,
    // create a new empty setting to fill in.
    if ((int)CDisplaySettings::GetInstance().ResolutionInfoSize() <= res_index)
    {
      RESOLUTION_INFO res;
      CDisplaySettings::GetInstance().AddResolutionInfo(res);
    }

    CServiceBroker::GetWinSystem()->GetGfxContext().ResetOverscan(resolutions[i]);
    CDisplaySettings::GetInstance().GetResolutionInfo(res_index) = resolutions[i];

    CLog::Log(LOGINFO, "Found resolution %d x %d with %d x %d%s @ %f Hz\n",
      resolutions[i].iWidth,
      resolutions[i].iHeight,
      resolutions[i].iScreenWidth,
      resolutions[i].iScreenHeight,
      resolutions[i].dwFlags & D3DPRESENTFLAG_INTERLACED ? "i" : "",
      resolutions[i].fRefreshRate);

    if(resDesktop.iWidth == resolutions[i].iWidth &&
       resDesktop.iHeight == resolutions[i].iHeight &&
       resDesktop.iScreenWidth == resolutions[i].iScreenWidth &&
       resDesktop.iScreenHeight == resolutions[i].iScreenHeight &&
       (resDesktop.dwFlags & D3DPRESENTFLAG_MODEMASK) == (resolutions[i].dwFlags & D3DPRESENTFLAG_MODEMASK) &&
       fabs(resDesktop.fRefreshRate - resolutions[i].fRefreshRate) < FLT_EPSILON)
    {
      ResDesktop = res_index;
    }

    res_index = (RESOLUTION)((int)res_index + 1);
  }

  // swap desktop index for desktop res if available
  if (ResDesktop != RES_INVALID)
  {
    CLog::Log(LOGINFO, "Found (%dx%d%s@%f) at %d, setting to RES_DESKTOP at %d",
      resDesktop.iWidth, resDesktop.iHeight,
      resDesktop.dwFlags & D3DPRESENTFLAG_INTERLACED ? "i" : "",
      resDesktop.fRefreshRate,
      (int)ResDesktop, (int)RES_DESKTOP);

    RESOLUTION_INFO desktop = CDisplaySettings::GetInstance().GetResolutionInfo(RES_DESKTOP);
    CDisplaySettings::GetInstance().GetResolutionInfo(RES_DESKTOP) = CDisplaySettings::GetInstance().GetResolutionInfo(ResDesktop);
    CDisplaySettings::GetInstance().GetResolutionInfo(ResDesktop) = desktop;
  }
}

bool CWinSystemSunxi::Hide()
{
  return false;
}

bool CWinSystemSunxi::Show(bool show)
{
  std::string blank_framebuffer = "/sys/class/graphics/" + m_framebuffer_name + "/blank";
  CSysfsPath fbBlank{blank_framebuffer};
  if(fbBlank.Exists())
     fbBlank.Set<int>(show ? 0 : 1);
  return true;
}

void CWinSystemSunxi::Register(IDispResource *resource)
{
  CSingleLock lock(m_resourceSection);
  m_resources.push_back(resource);
}

void CWinSystemSunxi::Unregister(IDispResource *resource)
{
  CSingleLock lock(m_resourceSection);
  std::vector<IDispResource*>::iterator i = find(m_resources.begin(), m_resources.end(), resource);
  if (i != m_resources.end())
    m_resources.erase(i);
}

bool CWinSystemSunxi::GetNativeResolution(RESOLUTION_INFO *res)
{
  res->iWidth = m_dispBase->GetWidth();
  res->iHeight= m_dispBase->GetHeight();
  res->fRefreshRate = m_dispBase->GetRefreshRate();

  res->dwFlags= D3DPRESENTFLAG_PROGRESSIVE | D3DPRESENTFLAG_WIDESCREEN;
  //res->iScreen       = 0;
  res->bFullScreen   = true;
  res->iSubtitles    = (int)(0.965 * res->iHeight);
  res->fPixelRatio   = 1.0f;
  res->iScreenWidth  = res->iWidth;
  res->iScreenHeight = res->iHeight;
  res->strMode       = StringUtils::Format("%dx%d @ %.2f%s - Full Screen", res->iScreenWidth, res->iScreenHeight, res->fRefreshRate,
                                           res->dwFlags & D3DPRESENTFLAG_INTERLACED ? "i" : "");
  res->Overscan.left = 0;
  res->Overscan.top = 0;
  res->Overscan.right = res->iWidth;
  res->Overscan.bottom = res->iHeight;

  CLog::Log(LOGINFO,"Current resolution: %s\n",res->strMode.c_str());
  return true;
}

bool CWinSystemSunxi::ProbeResolutions(std::vector<RESOLUTION_INFO> &resolutions)
{
  if (m_readonly)
     return false;
      
  std::string valstr;
  CSysfsPath fbModes{"/sys/class/graphics/fb0/modes"};
  if(fbModes.Exists())
     valstr = fbModes.GetBuf();
  std::vector<std::string> probe_str = StringUtils::Split(valstr, "\n");
      
  // lexical order puts the modes list into our preferred
  // order and by later filtering through FindMatchingResolution()
  // we make sure we read _all_ S modes, following U and V modes
  // while list will hold unique resolutions only
  std::sort(probe_str.begin(), probe_str.end());
  resolutions.clear();
  RESOLUTION_INFO res;
  for (size_t i = 0; i < probe_str.size(); i++)
  {
     if(!StringUtils::StartsWith(probe_str[i], "D:") && !StringUtils::StartsWith(probe_str[i], "U:") &&
        !StringUtils::StartsWith(probe_str[i], "V:"))
         continue;
    
     if(ModeToResolution(probe_str[i], &res))
        if(!FindMatchingResolution(res, resolutions))
           resolutions.push_back(res);
  }
  return resolutions.size() > 0;
}

bool CWinSystemSunxi::ModeToResolution(std::string mode, RESOLUTION_INFO *res)
{
  if (!res)
     return false;
  
  res->iWidth = 0;
  res->iHeight= 0;
  
  if(mode.empty())
     return false;
  
  std::string fromMode = StringUtils::Mid(mode, 2);
  StringUtils::Trim(fromMode);
  
  CRegExp split(true);
  split.RegComp("([0-9]+)x([0-9]+)([pi])-([0-9]+)");
  if (split.RegFind(fromMode) < 0)
     return false;
  
  int w = atoi(split.GetMatch(1).c_str());
  int h = atoi(split.GetMatch(2).c_str());
  std::string p = split.GetMatch(3);
  int r = atoi(split.GetMatch(4).c_str());
  if(r == 0)
    r = 50;
  res->iWidth = w;
  res->iHeight= h;
  res->iScreenWidth = w;
  res->iScreenHeight= h;
  res->fRefreshRate = r;
  res->dwFlags = p[0] == 'p' ? D3DPRESENTFLAG_PROGRESSIVE : D3DPRESENTFLAG_INTERLACED;

  //res->iScreen       = 0;
  res->bFullScreen   = true;
  res->iSubtitles    = (int)(0.965 * res->iHeight);
  
  res->fPixelRatio   = 1.0f;
  res->strMode       = StringUtils::Format("%dx%d @ %.2f%s - Full Screen", res->iScreenWidth, res->iScreenHeight, res->fRefreshRate,
                                           res->dwFlags & D3DPRESENTFLAG_INTERLACED ? "i" : "");
  res->strId         = mode;
  res->Overscan.left = 0;
  res->Overscan.top  = 0;
  res->Overscan.right = w;
  res->Overscan.bottom = h;
 
  return res->iWidth > 0 && res->iHeight> 0;
}

bool CWinSystemSunxi::FindMatchingResolution(const RESOLUTION_INFO &res, const std::vector<RESOLUTION_INFO> &resolutions)
{
  for (int i = 0; i < (int)resolutions.size(); i++)
  {
     if(resolutions[i].iScreenWidth == res.iScreenWidth &&
                     resolutions[i].iScreenHeight == res.iScreenHeight &&
                     resolutions[i].fRefreshRate == res.fRefreshRate &&
                     (resolutions[i].dwFlags & D3DPRESENTFLAG_MODEMASK) == (res.dwFlags & D3DPRESENTFLAG_MODEMASK))
     {
        return true;
     }
  }
  return false;
}

bool CWinSystemSunxi::VLInit()
{
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

  return true;
}

void CWinSystemSunxi::VLExit()
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
