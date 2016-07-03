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

#include "HwLayerCommonAllwinner.h"
#include "utils/log.h"

typedef unsigned char 		__u8;
typedef unsigned short int 	__u16;
typedef unsigned int  		__u32;
typedef int 			__s32;
#include <linux/sunxi_disp_ioctl.h>
#include <sys/ioctl.h>

CHwLayerCommonAllwinner::CHwLayerCommonAllwinner(CHwLayerConfigAllwinner &config) : 
      m_config(config)
{
}
CHwLayerCommonAllwinner::~CHwLayerCommonAllwinner()
{
}

//This is a function only used for a hack to destroy all remained
//layers from the last start
bool CHwLayerCommonAllwinner::destroyOrphanedLayers()
{
  for(int i = 0x65; i <= 0x67; ++i)
  {
    unsigned long args[4];
    args[0] = m_config.m_screenId;
    args[1] = i;
    args[2] = 0;
    args[3] = 0;
    int error = ioctl(m_config.m_dispFd, DISP_CMD_LAYER_RELEASE, args);
    //ignore any errors
  }
  return true;
}

bool CHwLayerCommonAllwinner::setProperty(CPropertyValue &prop)
{
  return false;
}

bool CHwLayerCommonAllwinner::setProperty(CColorKey &ck)
{
  m_ck = ck;
  setColorKeyValue(m_ck);
}

//colorkey value is common for all layers
bool CHwLayerCommonAllwinner::setColorKeyValue(CColorKey &colorkey)
{
  unsigned long args[4];
  __disp_colorkey_t l_ck;
  l_ck.ck_min.alpha = colorkey.min_alpha;
  l_ck.ck_min.red   = colorkey.min_red;
  l_ck.ck_min.green = colorkey.min_green;
  l_ck.ck_min.blue  = colorkey.min_blue;
  l_ck.ck_max.alpha = colorkey.max_alpha;
  l_ck.ck_max.red   = colorkey.max_red;
  l_ck.ck_max.green = colorkey.max_green;
  l_ck.ck_max.blue  = colorkey.max_blue;
  l_ck.red_match_rule   = colorkey.red_match_rule;
  l_ck.green_match_rule = colorkey.green_match_rule;
  l_ck.blue_match_rule  = colorkey.blue_match_rule;

  int cmd = DISP_CMD_SET_COLORKEY;
  args[0] = m_config.m_screenId;
  args[1] = (int)&l_ck;
  if (ioctl(m_config.m_dispFd, cmd, &args) < 0)
  {
    CLog::Log(LOGERROR, "CHwLayerCommonAllwinner: DISP_CMD_LAYER_SET_COLOR_KEY for failed.\n");
    return false;
  }
  return true;
};

