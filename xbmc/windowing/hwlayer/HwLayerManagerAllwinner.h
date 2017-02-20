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
#include "HwLayer.h"
#include "HwLayerManagerAllwinnerItf.h"
#include "HwLayerAllwinnerDefs.h"
#include "HwLayerAdaptorVdpau.h"
#include "utils/GlobalsHandling.h"
#include "guilib/Geometry.h"

#include "DVDCodecs/Video/VDPAU.h"

class CHwLayerManagerAllwinner
{
  public:
   typedef typename CHwLayerAllwinnerBase::HwLayerType HwLayerType;
   typedef typename CHwLayerAllwinnerBase::CPropertyValue CPropertyValue;
   typedef typename CHwLayerAllwinnerBase::CPropertyValue::Property PropertyKey;
   typedef typename CHwLayerAllwinnerBase::CPropertyValue::ScalerTypeValues ScalerType;
   typedef typename CHwLayerAllwinnerBase::CPropertyValue::ColorSpaceValues ColorSpace;
   typedef typename CHwLayerAllwinnerBase::CPropertyValue::InterlaceValues Interlace;
   typedef typename CHwLayerAllwinnerBase::CColorKey CColorKey;
   typedef typename CHwLayerAllwinnerBase::HwLayerSyncValue HwLayerSyncValue;

   typedef CHwLayerManager<CHwLayerAllwinnerBase, CHwLayerManagerConfigAllwinner> Base;
   
   CHwLayerManagerAllwinner() : m_manager(NULL) {};
   virtual ~CHwLayerManagerAllwinner();
   virtual bool initialize(CHwLayerManagerConfigAllwinner & config);
   virtual bool setup();
   virtual bool createLayer(HwLayerType type);
   virtual bool showLayer(HwLayerType type);
   virtual bool hideLayer(HwLayerType type);
   virtual bool destroyLayer(HwLayerType type);
   virtual bool sendTop(HwLayerType type);
   virtual bool sendBack(HwLayerType type);
   virtual bool configure(HwLayerType type, CHwLayerAdaptorVdpauAllwinner &frame, CRect &srcRect, CRect &dstRect);
   virtual bool displayFrame(HwLayerType type, CHwLayerAdaptorVdpauAllwinner &frame, VDPAU::CVdpauRenderPicture *buffer, int top_field);
   virtual bool getSyncFenceValue(HwLayerType type, VDPAU::CVdpauRenderPicture *pic, HwLayerSyncValue &value);
   virtual bool initSyncFence(HwLayerType type, VDPAU::CVdpauRenderPicture *pic);
   virtual bool destroySyncFence(HwLayerType type, VDPAU::CVdpauRenderPicture *pic);

   virtual bool setProperty(HwLayerType type, CPropertyValue &prop);
   virtual bool setProperty(HwLayerType type, CColorKey &prop);
   
  protected:
   CHwLayerManagerAllwinnerItf *m_manager;
};

typedef CHwLayerManagerAllwinner CHwLayerManagerAW;
XBMC_GLOBAL_REF(CHwLayerManagerAW,g_HwLayer);
#define g_HwLayer XBMC_GLOBAL_USE(CHwLayerManagerAW)
