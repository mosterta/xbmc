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

#include "HwLayerManagerAllwinnerItf.h"
#include "HwLayerAdaptorVdpau.h"

#include "DVDCodecs/Video/VDPAU.h"

class CHwLayerManagerAllwinnerDisp2 : public CHwLayerManagerAllwinnerItf
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
   
   CHwLayerManagerAllwinnerDisp2();
   virtual ~CHwLayerManagerAllwinnerDisp2();
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
   virtual bool syncFrame(HwLayerType type, VDPAU::CVdpauRenderPicture *pic);

   virtual bool setProperty(HwLayerType type, CPropertyValue &prop);
   virtual bool setProperty(HwLayerType type, CColorKey &prop);
   
  protected:
   //CHwLayerManagerConfigAllwinner m_config; 
   int m_fbFd;
   int m_dispFd;
};

typedef CHwLayerManagerAllwinnerDisp2 CHwLayerManagerAllwinnerDisp2Type;
