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

#include "HwLayerManagerAllwinnerDisp2.h"
#include "HwLayerAllwinnerDisp.h"

#include "utils/log.h"
#include "utils/CPUInfo.h"

#include <errno.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <string.h>
#include <sys/ioctl.h>

using namespace std;

CHwLayerManagerAllwinnerDisp2::CHwLayerManagerAllwinnerDisp2()
{
};

CHwLayerManagerAllwinnerDisp2::~CHwLayerManagerAllwinnerDisp2()
{
  for (auto& layerPair  : Base::m_layers)
  {
    if(layerPair.second != NULL)
      delete layerPair.second;
  };
  Base::m_layers.clear();
  
  if(m_fbFd > 0)
  {
    close(m_fbFd);
    m_fbFd = -1;
  }
  if(m_dispFd > 0)
  {
    close(m_dispFd);
    m_dispFd = -1;
  }
};

bool CHwLayerManagerAllwinnerDisp2::initialize(CHwLayerManagerConfigAllwinner & config)
{
  Base:m_config = config;
  string fbName = "/dev/fb" + std::to_string(Base::m_config.m_fbNum);
  m_fbFd = open(fbName.c_str(), O_RDWR);
  if(m_fbFd < 0)
  {
    CLog::Log(LOGERROR, "CHwLayerManagerAllwinnerDisp2::%s Could not open %s, error=%s", 
              __FUNCTION__, fbName.c_str(), strerror(errno));
    return false;
  }
  
  string dispName = "/dev/disp";
  m_dispFd = open(dispName.c_str(), O_RDWR);
  if(m_dispFd < 0)
  {
    CLog::Log(LOGERROR, "CHwLayerManagerAllwinnerDisp2::%s Could not open %s, error=%s", 
              __FUNCTION__, dispName.c_str(), strerror(errno));
    return false;
  }
  CHwLayerConfigAllwinner commonConfig;
  commonConfig.m_screenId = Base::m_config.m_screenId;
  commonConfig.m_dispFd = m_dispFd;
  commonConfig.m_fbFd = m_fbFd;

  return true;
};

bool CHwLayerManagerAllwinnerDisp2::setup()
{
  createLayer(HwLayerType::GUI);
  createLayer(HwLayerType::Video);

  //bring Video layer behind GUI layer
  //sendTop(HwLayerType::Video);
  //sendTop(HwLayerType::GUI);

  {
    CPropertyValue prop(PropertyKey::AlphaEnable, 1);
    setProperty(HwLayerType::GUI, prop);
  }
  //Enable alpha blending and value inside colorkey
  {
    CPropertyValue prop(PropertyKey::AlphaValue, 0xA8);
    setProperty(HwLayerType::GUI, prop);
  }
  {
    CPropertyValue prop(PropertyKey::AlphaValue, 0xff);
    setProperty(HwLayerType::Video, prop);
  }
  {
    CPropertyValue prop(PropertyKey::AlphaEnable, 1);
    setProperty(HwLayerType::Video, prop);
  }
  return true;
};

bool CHwLayerManagerAllwinnerDisp2::createLayer(HwLayerType type)
{
  if(type >= HwLayerType::Video && type <= HwLayerType::GUI)
  {
    CHwLayerAllwinnerDisp2 *layer;
    CHwLayerConfigAllwinner config;
    config.m_screenId = Base::m_config.m_screenId;
    config.m_dispFd = m_dispFd;
    config.m_fbFd = m_fbFd;
    
    layer = new CHwLayerAllwinnerDisp2(config);
    if(! layer )
      return false;
    layer->initialize(config);
    
    Base::m_layers.insert(std::pair<HwLayerType, CHwLayerAllwinnerDisp2*>(type, layer));
    return layer->create(type);
  }
  else
  {
    return false;
  }
};

bool CHwLayerManagerAllwinnerDisp2::showLayer(HwLayerType type)
{
  if(type >= HwLayerType::Video && type <= HwLayerType::GUI)
  {
    CHwLayerAllwinnerBase *layer = Base::m_layers[type];
    if(layer)
      return layer->show();
    else
      return false;
  }
  else
  {
    return false;
  }
};

bool CHwLayerManagerAllwinnerDisp2::hideLayer(HwLayerType type)
{
  if(type >= HwLayerType::Video && type <= HwLayerType::GUI)
  {
    CHwLayerAllwinnerBase *layer = Base::m_layers[type];
    if(layer)
      return layer->hide();
    else
      return false;
  }
  else
  {
    return false;
  }
};

bool CHwLayerManagerAllwinnerDisp2::destroyLayer(HwLayerType type)
{
  if(type >= HwLayerType::Video && type <= HwLayerType::GUI)
  {
    CHwLayerAllwinnerBase *layer = Base::m_layers[type];
    if(layer)
    {
      bool status = layer->destroy();
      delete layer;
      Base::m_layers.erase(type);
      return status;
    }
    else
      return false;
  }
  else
  {
    return false;
  }
};

bool CHwLayerManagerAllwinnerDisp2::sendTop(HwLayerType type)
{
  if(type >= HwLayerType::Video && type <= HwLayerType::GUI)
  {
    CHwLayerAllwinnerBase *layer = Base::m_layers[type];
    if(layer)
      return layer->top();
    else
      return false;
  }
  else
  {
    return false;
  }
};

bool CHwLayerManagerAllwinnerDisp2::sendBack(HwLayerType type)
{
  if(type >= HwLayerType::Video && type <= HwLayerType::GUI)
  {
    CHwLayerAllwinnerBase *layer = Base::m_layers[type];
    if(layer)
      return layer->back();
    else
      return false;
  }
  else
  {
    return false;
  }
};

bool CHwLayerManagerAllwinnerDisp2::configure(HwLayerType type, CHwLayerAdaptorVdpauAllwinner& frame, CRect &srcRect, CRect &dstRect)
{
  if(type >= HwLayerType::Video && type <= HwLayerType::GUI)
  {
    CHwLayerAllwinnerBase *layer = Base::m_layers[type];
    if(layer)
      return layer->configure(frame, srcRect, dstRect);
    else
      return false;
  }
  else
  {
    return false;
  }
};

bool CHwLayerManagerAllwinnerDisp2::displayFrame(HwLayerType type, CHwLayerAdaptorVdpauAllwinner &frame, VDPAU::CVdpauRenderPicture *buffer, int top_field)
{
  if(type >= HwLayerType::Video && type <= HwLayerType::GUI)
  {
    CHwLayerAllwinnerBase *layer = Base::m_layers[type];
    if(layer)
      return layer->displayFrame(frame, buffer, top_field);
    else
      return false;
  }
  else
  {
    return false;
  }
};

bool CHwLayerManagerAllwinnerDisp2::syncFrame(HwLayerType type, VDPAU::CVdpauRenderPicture *pic)
{
  if(type >= HwLayerType::Video && type <= HwLayerType::GUI)
  {
    CHwLayerAllwinnerBase *layer = Base::m_layers[type];
    if(layer)
      return layer->syncFrame(pic);
    else
      return false;
  }
  else
  {
    return false;
  }
}

bool CHwLayerManagerAllwinnerDisp2::setProperty(HwLayerType type, CPropertyValue &prop)
{
  if(type >= HwLayerType::Video && type <= HwLayerType::GUI)
  {
    CHwLayerAllwinnerBase *layer = Base::m_layers[type];
    if(layer)
      return layer->setProperty(prop);
    else
      return false;
  }
  else
  {
    return false;
  }
};

bool CHwLayerManagerAllwinnerDisp2::setProperty(HwLayerType type, CColorKey &prop)
{
  return false;
};

