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

#include <string>

#include "HwLayerManager.h"
#include "HwLayerAllwinnerA10.h"
#include "HwLayerAdaptorVdpau.h"
#include "HwLayerCommonAllwinner.h"
#include "utils/GlobalsHandling.h"
#include "guilib/Geometry.h"


template<typename CHwLayer, typename CVideoDataProvider>
class CHwLayerManagerAllwinner : public CHwLayerManager<CHwLayer, CHwLayerManagerConfigAllwinner>
{
  public:
   typedef typename CHwLayer::HwLayerType HwLayerType;
   typedef typename CHwLayer::CPropertyValue CPropertyValue;
   typedef typename CHwLayer::CPropertyValue::Property PropertyKey;
   typedef typename CHwLayer::CPropertyValue::ScalerTypeValues ScalerType;
   typedef typename CHwLayer::CColorKey CColorKey;

   typedef CHwLayerManager<CHwLayer, CHwLayerManagerConfigAllwinner> Base;
   
   CHwLayerManagerAllwinner();
   virtual ~CHwLayerManagerAllwinner();
   virtual bool initialize(CHwLayerManagerConfigAllwinner & config);
   virtual bool setup();
   virtual bool createLayer(HwLayerType type);
   virtual bool showLayer(HwLayerType type);
   virtual bool hideLayer(HwLayerType type);
   virtual bool destroyLayer(HwLayerType type);
   virtual bool sendTop(HwLayerType type);
   virtual bool sendBack(HwLayerType type);
   virtual bool configure(HwLayerType type, CVideoDataProvider &frame, CRect &srcRect, CRect &dstRect);
   virtual bool displayFrame(HwLayerType type, CVideoDataProvider &frame, int frameId);
   virtual bool getCurrentFrameId(HwLayerType type, int &frameId);
   virtual bool setProperty(HwLayerType type, CPropertyValue &prop);
   virtual bool setProperty(HwLayerType type, CColorKey &prop);
   
  protected:
   CHwLayer* getLayer(HwLayerType type);
   bool version();
   CHwLayerManagerConfigAllwinner m_config; 
   int m_fbFd;
   int m_dispFd;
   CHwLayerCommonAllwinner *m_commonFunc;
};

typedef CHwLayerManagerAllwinner<CHwLayerAllwinnerA10, CHwLayerAdaptorVdpauAllwinner> CHwLayerManagerAW;
XBMC_GLOBAL_REF(CHwLayerManagerAW,g_HwLayer);
#define g_HwLayer XBMC_GLOBAL_USE(CHwLayerManagerAW)
