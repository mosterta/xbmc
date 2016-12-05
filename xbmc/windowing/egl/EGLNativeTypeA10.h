#pragma once

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

#include "EGLNativeType.h"
#include "WinSystemEGL.h"

class CEGLNativeTypeA10 : public CEGLNativeType
{
public:
  class CWinLayerInfoAllwinner : public CWinSystemEGL::CWinLayerInformation
  {
public:
    int m_GuiLayer;
    int m_VideoLayer;
    int m_dispFd;
  };
  CEGLNativeTypeA10();
  virtual ~CEGLNativeTypeA10();
  virtual std::string GetNativeName() const { return "A10"; };
  virtual bool  CheckCompatibility();
  virtual void  Initialize();
  virtual void  Destroy();
  virtual int   GetQuirks() { return EGL_QUIRK_NEED_WINDOW_FOR_RES; };

  virtual bool  CreateNativeDisplay();
  virtual bool  CreateNativeWindow();
  virtual bool  GetNativeDisplay(XBNativeDisplayType **nativeDisplay) const;
  virtual bool  GetNativeWindow(XBNativeWindowType **nativeWindow) const;

  virtual bool  DestroyNativeWindow();
  virtual bool  DestroyNativeDisplay();

  virtual bool  GetNativeResolution(RESOLUTION_INFO *res) const;
  virtual bool  SetNativeResolution(const RESOLUTION_INFO &res);
  virtual bool  ProbeResolutions(std::vector<RESOLUTION_INFO> &resolutions);
  virtual bool  GetPreferredResolution(RESOLUTION_INFO *res) const;

  virtual bool  ShowWindow(bool show);

protected:
  virtual int GetWidth();
  virtual int GetHeight();
  virtual int GetRefreshRate();
  int     m_hfb;
  int     m_hdisp;
  int     m_screenid;
  int     m_width;
  int     m_height;

private:
  bool VLInit(int &width, int &height, double &refreshRate);
  void VLExit();
  bool VLBlueScreenFix();
};
