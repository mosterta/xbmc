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

#pragma once

#include "platform/linux/input/LibInputHandler.h"
#include "platform/linux/OptionalsReg.h"
#include "rendering/gles/RenderSystemGLES.h"
#include "threads/CriticalSection.h"
#include "windowing/WinSystem.h"
#include "threads/SystemClock.h"
#include "EGL/eglplatform_fb.h"
#include "EGL/egl.h"
#include "windowing/allwinner/SunxiDispBase.h"

class IDispResource;

class CWinSystemSunxi : public CWinSystemBase
{
public:
  CWinSystemSunxi();
  virtual ~CWinSystemSunxi();

  bool InitWindowSystem() override;
  bool DestroyWindowSystem() override;

  bool CreateNewWindow(const std::string& name,
                       bool fullScreen,
                       RESOLUTION_INFO& res) override;

  bool DestroyWindow() override;
  void UpdateResolutions() override;

  bool Hide() override;
  bool Show(bool show = true) override;
  virtual void Register(IDispResource *resource);
  virtual void Unregister(IDispResource *resource);
  bool CanDoWindowed() override { return false; }

protected:
  std::string m_framebuffer_name;
  EGLDisplay m_nativeDisplay;
  EGLNativeWindowType m_nativeWindow;

  int m_displayWidth;
  int m_displayHeight;

  RENDER_STEREO_MODE m_stereo_mode;

  bool m_delayDispReset;
  XbmcThreads::EndTime m_dispResetTimer;

  CCriticalSection m_resourceSection;
  std::vector<IDispResource*> m_resources;
  bool ModeToResolution(std::string mode, RESOLUTION_INFO *res) const;
  bool FindMatchingResolution(const RESOLUTION_INFO &res, const std::vector<RESOLUTION_INFO> &resolutions);
  bool GetNativeResolution(RESOLUTION_INFO *res);
  bool ProbeResolutions(std::vector<RESOLUTION_INFO> &resolutions);
  bool ModeToResolution(std::string mode, RESOLUTION_INFO *res);
  bool SetDisplayResolution(const RESOLUTION_INFO &res, std::string framebuffer_name);
  void SetFramebufferResolution(const RESOLUTION_INFO &res, std::string framebuffer_name);
  void SetFramebufferResolution(int width, int height, std::string framebuffer_name);

  bool VLInit();
  void VLExit();
  
  int     m_hfb;
  int     m_hdisp;
  bool m_readonly;
  int     m_screenid{0};
  std::unique_ptr<OPTIONALS::CLircContainer, OPTIONALS::delete_CLircContainer> m_lirc;
  std::unique_ptr<CLibInputHandler> m_libinput;
  std::unique_ptr<SunxiDispBase> m_dispBase;
};
