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

#pragma once

#include <vdpau/vdpau.h>

typedef unsigned int vdpauSurfaceCedar;

typedef int (*PFGLVDPAUGETVIDEOFRAMECCONFIG)(vdpauSurfaceCedar surface, int *srcFormat, void** addrY, void** addrU, void** addrV, int *height, int * width);

class CHwLayerAdaptorVdpauAllwinner
{
  public:
    struct cFbSize {
      int  width;
      int height;
    };
    enum FrameFormat {
      YCBCR_FORMAT_YUYV,
      YCBCR_FORMAT_UYVY,
      YCBCR_FORMAT_NV12,
      YCBCR_FORMAT_YV12,
      YCBCR_FORMAT_INTERNAL
    };
    struct cFrameConfig {
      void *addrY;
      void *addrU;
      void *addrV;
      struct cFbSize fbSize;
      FrameFormat dataFormat;
    };
    CHwLayerAdaptorVdpauAllwinner();
    ~CHwLayerAdaptorVdpauAllwinner();
    bool getFrameConfig(struct cFrameConfig &config);
    bool initialize();
    void setFrame(vdpauSurfaceCedar surface);

  protected:
    PFGLVDPAUGETVIDEOFRAMECCONFIG glVDPAUGetVideoFrameConfig;
    void *m_dlVdpauNvHandle;
    vdpauSurfaceCedar m_surface;
};

