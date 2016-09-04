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

#include "HwLayer.h"
#include "HwLayerAllwinnerA10.h"
#include "HwLayerAdaptorVdpau.h"
#include "xbmc/guilib/Geometry.h"
#include "DVDCodecs/Video/VDPAU.h"

class CHwLayerConfigAllwinner
{
public:
  int m_screenId;
  int m_dispFd;
  int m_fbFd;
};

class CHwLayerAllwinnerA10 : public CHwLayer<CHwLayerAdaptorVdpauAllwinner, CHwLayerConfigAllwinner>
{
  public:
    typedef typename CHwLayer<CHwLayerAdaptorVdpauAllwinner, CHwLayerConfigAllwinner>::HwLayerType HwLayerType;

    typedef typename CHwLayer<CHwLayerAdaptorVdpauAllwinner, CHwLayerConfigAllwinner>::CPropertyValue::Property PropertyKey;
    typedef typename CHwLayer<CHwLayerAdaptorVdpauAllwinner, CHwLayerConfigAllwinner>::CPropertyValue::ScalerTypeValues ScalerType;
    typedef typename CHwLayer<CHwLayerAdaptorVdpauAllwinner, CHwLayerConfigAllwinner>::CPropertyValue::ColorSpaceValues ColorSpace;
    typedef typename CHwLayer<CHwLayerAdaptorVdpauAllwinner, CHwLayerConfigAllwinner>::CPropertyValue::InterlaceValues Interlace;

   CHwLayerAllwinnerA10(CHwLayerConfigAllwinner &config);
   virtual ~CHwLayerAllwinnerA10();
   virtual bool initialize(CHwLayerConfigAllwinner &config);
   virtual bool create(HwLayerType type);
   virtual bool destroy();
   virtual bool configure(CHwLayerAdaptorVdpauAllwinner &frame, CRect &srcRect, CRect &dstRect);
   virtual bool hide();
   virtual bool show();
   virtual bool top();
   virtual bool back();
   virtual bool displayFrame(CHwLayerAdaptorVdpauAllwinner &frame, int frameId, int top_field);
   virtual bool getCurrentFrameId(int &frameId);
   virtual bool syncFrame(VDPAU::CVdpauRenderPicture *pic);

   virtual bool setProperty(CPropertyValue &prop);

  protected:
   bool setAlphaEnable(int alphaEnable);
   bool setAlphaValue(int alphaValue);
   bool setColorKeyEnable(int ckEnable);
   bool setScalerType(ScalerType scaler);
   
   CHwLayerConfigAllwinner m_config;
   HwLayerType m_layerType;
   int m_layerId;
   bool m_layerCreated;
   bool m_layerOpened;
   bool m_layerVideoStarted;
   CPropertyValue m_alphaValue;
   CPropertyValue m_alphaEnable;
   CPropertyValue m_colorKeyEnable;
   CPropertyValue m_scalerType;
   CPropertyValue m_colorSpace;
   CPropertyValue m_interlaceMode;
};

