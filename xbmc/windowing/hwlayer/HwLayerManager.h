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
#include "xbmc/guilib/Geometry.h"
#include <map>


template<typename CHwLayer, typename CHwLayerManagerConfig>
class CHwLayerManager
{
  public:
   typedef typename CHwLayer::HwLayerType HwLayerType;
   //typedef typename CHwLayer::RENDERBUFFER RENDERBUFFER;
   typedef typename CHwLayer::CPropertyValue CPropertyValue;
   typedef typename CHwLayer::CPropertyValue::Property PropertyKey;
   typedef typename CHwLayer::CColorKey CColorKey;

   CHwLayerManager() {};
   virtual ~CHwLayerManager() {};
   virtual bool initialize(CHwLayerManagerConfig &config) { return false; };
   virtual bool createLayer(HwLayerType type) { return false; };
   virtual bool showLayer(HwLayerType type) { return false; };
   virtual bool hideLayer(HwLayerType type) { return false; };
   virtual bool destroyLayer(HwLayerType type) { return false; };
   virtual bool sendTop(HwLayerType type) { return false; };
   virtual bool sendBack(HwLayerType type) { return false; };
   virtual bool configure(HwLayerType type, CRect srcRect, CRect dstRect) { return false; };
#if 0
   virtual bool displayFrame(HwLayerType type, CVideoDataProvider &frame, RENDERBUFFER *buffer, int top_field);
   virtual bool syncFrame(HwLayerType type, RENDERBUFFER *pic);

   virtual bool setProperty(HwLayerType type, CPropertyValue &prop);
   virtual bool setProperty(HwLayerType type, CColorKey &prop);
#endif
   
  protected:
   CHwLayerManagerConfig m_config;
   typedef std::map<HwLayerType, CHwLayer*> LayerStore;
   LayerStore m_layers;
};

