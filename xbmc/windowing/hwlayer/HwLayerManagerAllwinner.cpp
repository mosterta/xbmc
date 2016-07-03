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

#include "HwLayerManagerAllwinner.h"

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

template<typename CHwLayer,typename CVideoDataProvider>
CHwLayerManagerAllwinner<CHwLayer,CVideoDataProvider>::CHwLayerManagerAllwinner(): 
    CHwLayerManager<CHwLayer, CHwLayerManagerConfigAllwinner>(),
    m_fbFd(-1), m_dispFd(-1)
{
};

template<typename CHwLayer,typename CVideoDataProvider>
CHwLayerManagerAllwinner<CHwLayer,CVideoDataProvider>::~CHwLayerManagerAllwinner()
{
  for (auto& layerPair  : Base::m_layers)
  {
    if(layerPair.second != NULL)
      delete layerPair.second;
  };
  Base::m_layers.clear();
  
  if(m_fbFd < 0)
  {
    close(m_fbFd);
    m_fbFd = -1;
  }
  if(m_dispFd < 0)
  {
    close(m_dispFd);
    m_dispFd = -1;
  }
};

template<class CHwLayer,typename CVideoDataProvider>
bool CHwLayerManagerAllwinner<CHwLayer,CVideoDataProvider>::initialize(CHwLayerManagerConfigAllwinner & config)
{
  Base:m_config = config;
  string fbName = "/dev/fb" + std::to_string(Base::m_config.m_fbNum);
  m_fbFd = open(fbName.c_str(), O_RDWR);
  if(m_fbFd < 0)
  {
    CLog::Log(LOGERROR, "CHwLayerManagerAllwinner::%s Could not open %s, error=%s", 
              fbName.c_str(), strerror(errno));
    return false;
  }
  
  string dispName = "/dev/disp";
  m_dispFd = open(dispName.c_str(), O_RDWR);
  if(m_dispFd < 0)
  {
    CLog::Log(LOGERROR, "CHwLayerManagerAllwinner::%s Could not open %s, error=%s", 
              dispName.c_str(), strerror(errno));
    return false;
  }
#if 0  
  int ver = SUNXI_DISP_VERSION;
  if (ioctl(m_dispFd, DISP_CMD_VERSION, &ver) < 0)
  {
    CLog::Log(LOGERROR, "CHwLayerManagerAllwinner: version failed. (%d)", errno);
    return false;
  }
#endif
  CHwLayerConfigAllwinner commonConfig;
  commonConfig.m_screenId = m_config.m_screenId;
  commonConfig.m_dispFd = m_dispFd;
  commonConfig.m_fbFd = m_fbFd;

  m_commonFunc = new CHwLayerCommonAllwinner(commonConfig);
  if(! m_commonFunc )
  {
    CLog::Log(LOGERROR, "CHwLayerManagerAllwinner::%s Could not create common layer");
    return false;
  }
};

template<class CHwLayer,typename CVideoDataProvider>
bool CHwLayerManagerAllwinner<CHwLayer,CVideoDataProvider>::setup()
{
  createLayer(HwLayerType::GUI);
  
  //Allwinner A10 needs to use 2 scaler layer, display backend does not work properly
  if (std::string("sun4i") == g_cpuInfo.getCPUHardware())
  {
    CPropertyValue prop(PropertyKey::ScalerType, (int)ScalerType::Type_Scale);
    setProperty(HwLayerType::GUI, prop);
  }
  else
  {
    CPropertyValue prop(PropertyKey::ScalerType, (int)ScalerType::Type_Normal);
    setProperty(HwLayerType::GUI, prop);
  }

  m_commonFunc->destroyOrphanedLayers();

  //Blue/Green screen fix
  createLayer(HwLayerType::Video);
  CPropertyValue prop(PropertyKey::ScalerType, (int)ScalerType::Type_Scale);
  setProperty(HwLayerType::Video, prop);
  destroyLayer(HwLayerType::Video);

  createLayer(HwLayerType::Video);

  CColorKey ck;
  ck.min_red = 0x1;
  ck.min_green = 0x1;
  ck.min_blue = 0x1;
  ck.min_alpha = 0x0;
  ck.max_red = 0xff;
  ck.max_green = 0xff;
  ck.max_blue = 0xff;
  ck.max_alpha = 0xff;
  // 3 means alpha blending for color < min && > max
  ck.red_match_rule = 3;
  ck.green_match_rule = 3;
  ck.blue_match_rule = 3;
  m_commonFunc->setProperty(ck);

  //bring Video layer behind GUI layer
  sendTop(HwLayerType::Video);
  sendTop(HwLayerType::GUI);

  //if not a A10, but A20 and hopefully later hardware as well,
  //alpha blending is switched off, since HW does alpha blending automatically
  if(std::string("sun4i") != g_cpuInfo.getCPUHardware())
  {
    CPropertyValue prop(PropertyKey::AlphaEnable, 0);
    setProperty(HwLayerType::GUI, prop);
  }
  else
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
  {
    CPropertyValue prop(PropertyKey::ColorKeyEnable, 1);
    setProperty(HwLayerType::Video, prop);
  }
};

template<class CHwLayer,typename CVideoDataProvider>
bool CHwLayerManagerAllwinner<CHwLayer,CVideoDataProvider>::createLayer(HwLayerType type)
{
  if(type >= HwLayerType::Video && type <= HwLayerType::GUI)
  {
    CHwLayer *layer;
    CHwLayerConfigAllwinner config;
    config.m_screenId = m_config.m_screenId;
    config.m_dispFd = m_dispFd;
    config.m_fbFd = m_fbFd;
    
    std::string hardware = g_cpuInfo.getCPUHardware();
    if(hardware == std::string("sun4i") || hardware == std::string("sun7i"))
      layer = new CHwLayerAllwinnerA10(config);
    else
      return false;
    
    Base::m_layers.insert(std::pair<HwLayerType, CHwLayer*>(type, layer));
    return layer->create(type);
  }
  else
  {
    return false;
  }
};

template<class CHwLayer,typename CVideoDataProvider>
bool CHwLayerManagerAllwinner<CHwLayer,CVideoDataProvider>::showLayer(HwLayerType type)
{
  if(type >= HwLayerType::Video && type <= HwLayerType::GUI)
  {
    CHwLayer *layer = Base::m_layers[type];
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

template<class CHwLayer,typename CVideoDataProvider>
bool CHwLayerManagerAllwinner<CHwLayer,CVideoDataProvider>::hideLayer(HwLayerType type)
{
  if(type >= HwLayerType::Video && type <= HwLayerType::GUI)
  {
    CHwLayer *layer = Base::m_layers[type];
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

template<class CHwLayer,typename CVideoDataProvider>
bool CHwLayerManagerAllwinner<CHwLayer,CVideoDataProvider>::destroyLayer(HwLayerType type)
{
  if(type >= HwLayerType::Video && type <= HwLayerType::GUI)
  {
    CHwLayer *layer = Base::m_layers[type];
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

template<class CHwLayer,typename CVideoDataProvider>
bool CHwLayerManagerAllwinner<CHwLayer,CVideoDataProvider>::sendTop(HwLayerType type)
{
  if(type >= HwLayerType::Video && type <= HwLayerType::GUI)
  {
    CHwLayer *layer = Base::m_layers[type];
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

template<class CHwLayer,typename CVideoDataProvider>
bool CHwLayerManagerAllwinner<CHwLayer,CVideoDataProvider>::sendBack(HwLayerType type)
{
  if(type >= HwLayerType::Video && type <= HwLayerType::GUI)
  {
    CHwLayer *layer = Base::m_layers[type];
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

template<class CHwLayer,typename CVideoDataProvider>
CHwLayer* CHwLayerManagerAllwinner<CHwLayer,CVideoDataProvider>::getLayer(HwLayerType type)
{
  CHwLayer* handle = NULL;
  if(type >= HwLayerType::Video && type <= HwLayerType::GUI)
  {
    handle = Base::m_layers[type];
  }
  return handle;
};

template<class CHwLayer,typename CVideoDataProvider>
bool CHwLayerManagerAllwinner<CHwLayer,CVideoDataProvider>::configure(HwLayerType type, CVideoDataProvider& frame, CRect &srcRect, CRect &dstRect)
{
  if(type >= HwLayerType::Video && type <= HwLayerType::GUI)
  {
    CHwLayer *layer = Base::m_layers[type];
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

template<class CHwLayer,typename CVideoDataProvider>
bool CHwLayerManagerAllwinner<CHwLayer,CVideoDataProvider>::displayFrame(HwLayerType type, CVideoDataProvider &frame, int frameId)
{
  if(type >= HwLayerType::Video && type <= HwLayerType::GUI)
  {
    CHwLayer *layer = Base::m_layers[type];
    if(layer)
      return layer->displayFrame(frame, frameId);
    else
      return false;
  }
  else
  {
    return false;
  }
};

template<class CHwLayer,typename CVideoDataProvider>
bool CHwLayerManagerAllwinner<CHwLayer,CVideoDataProvider>::getCurrentFrameId(HwLayerType type, int &frameId)
{
  if(type >= HwLayerType::Video && type <= HwLayerType::GUI)
  {
    CHwLayer *layer = Base::m_layers[type];
    if(layer)
      return layer->getCurrentFrameId(frameId);
    else
      return false;
  }
  else
  {
    return false;
  }
}

template<class CHwLayer,typename CVideoDataProvider>
bool CHwLayerManagerAllwinner<CHwLayer,CVideoDataProvider>::setProperty(HwLayerType type, CPropertyValue &prop)
{
  if(type >= HwLayerType::Video && type <= HwLayerType::GUI)
  {
    CHwLayer *layer = Base::m_layers[type];
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

template<class CHwLayer,typename CVideoDataProvider>
bool CHwLayerManagerAllwinner<CHwLayer,CVideoDataProvider>::setProperty(HwLayerType type, CColorKey &prop)
{
  if(type >= HwLayerType::Video && type <= HwLayerType::GUI)
  {
    if(m_commonFunc)
      return m_commonFunc->setProperty(prop);
    else
      return false;
  }
  else
  {
    return false;
  }
};

