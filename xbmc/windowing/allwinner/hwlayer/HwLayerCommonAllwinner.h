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
#include "HwLayerAllwinnerDefs.h"

class CHwLayerCommonAllwinner
{
  public:
    CHwLayerCommonAllwinner(CHwLayerConfigAllwinner &config);
    ~CHwLayerCommonAllwinner();

    typedef typename CHwLayer<CHwLayerAdaptorVdpauAllwinner, CHwLayerConfigAllwinner>::CPropertyValue CPropertyValue;
    typedef typename CHwLayer<CHwLayerAdaptorVdpauAllwinner, CHwLayerConfigAllwinner>::CColorKey CColorKey;
    typedef typename CHwLayer<CHwLayerAdaptorVdpauAllwinner, CHwLayerConfigAllwinner>::CPropertyValue::Property PropertyKey;
    typedef typename CHwLayer<CHwLayerAdaptorVdpauAllwinner, CHwLayerConfigAllwinner>::CPropertyValue::ScalerTypeValues ScalerType;

    virtual bool destroyOrphanedLayers();
    virtual bool setProperty(CPropertyValue &prop);
    virtual bool setProperty(CColorKey &ck);
  protected:
    bool setColorKeyValue(CColorKey &colorkey);

    CHwLayerConfigAllwinner m_config;
    CColorKey m_ck;

};
