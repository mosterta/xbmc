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
#include "HwLayerManagerAllwinnerDisp.h"
#include "HwLayerManagerAllwinnerDisp2.h"

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

CHwLayerManagerAllwinner::~CHwLayerManagerAllwinner()
{
   if(m_manager)
     delete m_manager;
};

bool CHwLayerManagerAllwinner::initialize(CHwLayerManagerConfigAllwinner & config)
{
  std::string hardware = g_cpuInfo.getCPUHardware();
  if(hardware == std::string("sun4i") || hardware == std::string("sun7i"))
    m_manager = new CHwLayerManagerAllwinnerDispType;
  else
    m_manager = new CHwLayerManagerAllwinnerDisp2Type;

  if(!m_manager)
    return false;

  bool init = m_manager->initialize(config);
  return init;
};

bool CHwLayerManagerAllwinner::setup()
{
  if(!m_manager)
    return false;
  return m_manager->setup();
};

bool CHwLayerManagerAllwinner::createLayer(HwLayerType type)
{
  if(!m_manager)
    return false;
  return m_manager->createLayer(type);
};

bool CHwLayerManagerAllwinner::showLayer(HwLayerType type)
{
  if(!m_manager)
    return false;
  return m_manager->showLayer(type);
};

bool CHwLayerManagerAllwinner::hideLayer(HwLayerType type)
{
  if(!m_manager)
    return false;
  return m_manager->hideLayer(type);
};

bool CHwLayerManagerAllwinner::destroyLayer(HwLayerType type)
{
  if(!m_manager)
    return false;
  return m_manager->destroyLayer(type);
};

bool CHwLayerManagerAllwinner::sendTop(HwLayerType type)
{
  if(!m_manager)
    return false;
  return m_manager->sendTop(type);
};

bool CHwLayerManagerAllwinner::sendBack(HwLayerType type)
{
  if(!m_manager)
    return false;
  return m_manager->sendBack(type);
};

bool CHwLayerManagerAllwinner::configure(HwLayerType type, CHwLayerAdaptorVdpauAllwinner& frame, CRect &srcRect, CRect &dstRect)
{
  if(!m_manager)
    return false;
  return m_manager->configure(type, frame, srcRect, dstRect);
};

bool CHwLayerManagerAllwinner::displayFrame(HwLayerType type, CHwLayerAdaptorVdpauAllwinner &frame, VDPAU::CVdpauRenderPicture *buffer, int top_field)
{
  if(!m_manager)
    return false;
  return m_manager->displayFrame(type, frame, buffer, top_field);
};

bool CHwLayerManagerAllwinner::syncFrame(HwLayerType type, VDPAU::CVdpauRenderPicture *pic)
{
  if(!m_manager)
    return false;
  return m_manager->syncFrame(type, pic);
}

bool CHwLayerManagerAllwinner::setProperty(HwLayerType type, CPropertyValue &prop)
{
  if(!m_manager)
    return false;
  return m_manager->setProperty(type, prop);
};

bool CHwLayerManagerAllwinner::setProperty(HwLayerType type, CColorKey &prop)
{
  if(!m_manager)
    return false;
  return m_manager->setProperty(type, prop);
};

