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

#include "guilib/Geometry.h"

template<class CVideoDataProvider, class CHwLayerConfig>
class CHwLayer
{
  public:
   enum class HwLayerType {
      Video,
      GUI
    };
    class CPropertyValue 
    {
      public:
        enum Property {
          AlphaValue,
          AlphaEnable,
          ColorKeyEnable,
          ScalerType
        };
        enum ScalerTypeValues {Type_Scale, Type_Normal};
        CPropertyValue(Property k, int v) { key = k; value = v; };
        Property key;
        int value;
    };
    class CColorKey {
      public:
        CColorKey() : min_red(0), min_green(0), min_blue(0), min_alpha(0),
                  max_red(0), max_green(0), max_blue(0), max_alpha(0), 
                          red_match_rule(0), green_match_rule(0), blue_match_rule(0)
                          {};
                          unsigned char min_red;
                          unsigned char min_green;
                          unsigned char min_blue;
                          unsigned char min_alpha;
                          unsigned char max_red;
                          unsigned char max_green;
                          unsigned char max_blue;
                          unsigned char max_alpha;
                          unsigned char red_match_rule;
                          unsigned char green_match_rule;
                          unsigned char blue_match_rule;
    };

   CHwLayer(CHwLayerConfig &config) {};
   virtual ~CHwLayer() {};
   virtual bool initialize(CHwLayerConfig &config) = 0;
   virtual bool create(HwLayerType type) = 0;
   virtual bool destroy() = 0;
   virtual bool configure(CVideoDataProvider &frame, CRect &srcRect, CRect &dstRect) = 0;
   virtual bool hide() = 0;
   virtual bool show() = 0;
   virtual bool top() = 0;
   virtual bool back() = 0;
   virtual bool displayFrame(CVideoDataProvider &frame, int frameId) = 0;
};

