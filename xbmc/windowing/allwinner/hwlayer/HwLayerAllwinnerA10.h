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
#include "HwLayerAdaptorVdpau.h"
#include "HwLayerAllwinnerA10.h"
#include "HwLayerCommonAllwinner.h"
#include "utils/Geometry.h"
#include "DVDCodecs/Video/VDPAU.h"
#include "HwLayerAllwinnerDefs.h"

class CHwLayerAllwinnerA10 : public CHwLayerAllwinnerBase
{
  public:
    typedef typename CHwLayerAllwinnerBase::HwLayerType HwLayerType;

    typedef typename CHwLayerAllwinnerBase::CPropertyValue::Property PropertyKey;
    typedef typename CHwLayerAllwinnerBase::CPropertyValue::ScalerTypeValues ScalerType;
    typedef typename CHwLayerAllwinnerBase::CPropertyValue::ColorSpaceValues ColorSpace;
    typedef typename CHwLayerAllwinnerBase::CPropertyValue::InterlaceValues Interlace;

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
   virtual bool displayFrame(CHwLayerAdaptorVdpauAllwinner &frame, int &fence, int top_field);
   virtual bool getCurrentFrameId(int &frameId);
   virtual bool getSyncFenceValue(int fence, HwLayerSyncValue &value);
   virtual bool initSyncFence(int &fence) {fence = -1; return true; };
   virtual bool destroySyncFence(int &fence) { return initSyncFence(fence); }
   virtual bool setProperty(CHwLayerAllwinnerBase::CPropertyValue &prop);

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
   int m_fence;
};

