/*
 *      Copyright (C) 2010-2016 Team XBMC
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


#include "HwLayerAdaptorVdpau.h"

#include <dlfcn.h>
#include <stdio.h>

#define INTERNAL_YCBCR_FORMAT (0xffff)

CHwLayerAdaptorVdpauAllwinner::CHwLayerAdaptorVdpauAllwinner() : m_dlVdpauNvHandle(0) 
{
}
bool CHwLayerAdaptorVdpauAllwinner::initialize()
{
  bool status = true;
  const char VDPAU_MODULE_DIR[] = "/usr/lib";
  {
    char driver_path[255];
    snprintf(driver_path, 255, "%s/libcedarDisplay.so.1", VDPAU_MODULE_DIR);
    m_dlVdpauNvHandle = dlopen(driver_path, RTLD_LAZY);
    if(!m_dlVdpauNvHandle)
    {
      const char* error = dlerror();
      if (!error)
        error = "dlerror() returned NULL";
      status = false;
    }
  }
  glVDPAUGetVideoFrameConfig = (PFGLVDPAUGETVIDEOFRAMECCONFIG)dlsym(m_dlVdpauNvHandle, "glVDPAUGetVideoFrameConfig");
  if (!glVDPAUGetVideoFrameConfig)
    status = false;

  return status;
}

CHwLayerAdaptorVdpauAllwinner::~CHwLayerAdaptorVdpauAllwinner()
{
  if(m_dlVdpauNvHandle)
    dlclose(m_dlVdpauNvHandle);
}

bool CHwLayerAdaptorVdpauAllwinner::getFrameConfig(struct cFrameConfig &config)
{
  int format;
  int status = glVDPAUGetVideoFrameConfig(m_surface, &format, &config.addrY, &config.addrU, 
                                          &config.addrV, &config.fbSize.height, &config.fbSize.width);
  FrameFormat fmt;
  switch(format) {
    case VDP_YCBCR_FORMAT_YUYV:
      fmt = YCBCR_FORMAT_YUYV;
      break;
    case VDP_YCBCR_FORMAT_UYVY:
      fmt = YCBCR_FORMAT_UYVY;
      break;
    case VDP_YCBCR_FORMAT_NV12:
      fmt = YCBCR_FORMAT_NV12;
      break;
    case VDP_YCBCR_FORMAT_YV12:
      fmt = YCBCR_FORMAT_YV12;
      break;
    case INTERNAL_YCBCR_FORMAT:
      fmt = YCBCR_FORMAT_INTERNAL;
      break;
    default:
      fmt = YCBCR_FORMAT_INTERNAL;
      break;
  };
  config.dataFormat = fmt;

  return status;
}

void CHwLayerAdaptorVdpauAllwinner::setFrame(vdpauSurfaceCedar frame)
{
  m_surface = frame;
}

