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

#include <string>

#include "HwLayerManager.h"
#include "HwLayer.h"
#include "HwLayerAllwinnerDefs.h"
#include "HwLayerAdaptorVdpau.h"

#include "DVDCodecs/Video/VDPAU.h"

class CHwLayerManagerAllwinnerItf : public CHwLayerManager<CHwLayerAllwinnerBase, CHwLayerManagerConfigAllwinner>
{
  public:
   typedef typename CHwLayerAllwinnerBase::HwLayerType HwLayerType;
   typedef typename CHwLayerAllwinnerBase::CPropertyValue CPropertyValue;
   typedef typename CHwLayerAllwinnerBase::CPropertyValue::Property PropertyKey;
   typedef typename CHwLayerAllwinnerBase::CPropertyValue::ScalerTypeValues ScalerType;
   typedef typename CHwLayerAllwinnerBase::CPropertyValue::ColorSpaceValues ColorSpace;
   typedef typename CHwLayerAllwinnerBase::CPropertyValue::InterlaceValues Interlace;
   typedef typename CHwLayerAllwinnerBase::CColorKey CColorKey;

   typedef CHwLayerManager<CHwLayerAllwinnerBase, CHwLayerManagerConfigAllwinner> Base;
   
   CHwLayerManagerAllwinnerItf() : CHwLayerManager() {};
   virtual ~CHwLayerManagerAllwinnerItf() {};
   virtual bool initialize(CHwLayerManagerConfigAllwinner & config) = 0;
   virtual bool setup() = 0;
   virtual bool createLayer(HwLayerType type) = 0;
   virtual bool showLayer(HwLayerType type) = 0;
   virtual bool hideLayer(HwLayerType type) = 0;
   virtual bool destroyLayer(HwLayerType type) = 0;
   virtual bool sendTop(HwLayerType type) = 0;
   virtual bool sendBack(HwLayerType type) = 0;
   virtual bool configure(HwLayerType type, CHwLayerAdaptorVdpauAllwinner &frame, CRect &srcRect, CRect &dstRect) = 0;
   virtual bool displayFrame(HwLayerType type, CHwLayerAdaptorVdpauAllwinner &frame, VDPAU::CVdpauRenderPicture *buffer, int top_field) = 0;
   virtual bool syncFrame(HwLayerType type, VDPAU::CVdpauRenderPicture *pic) = 0;

   virtual bool setProperty(HwLayerType type, CPropertyValue &prop) = 0;
   virtual bool setProperty(HwLayerType type, CColorKey &prop) = 0;
   
};
