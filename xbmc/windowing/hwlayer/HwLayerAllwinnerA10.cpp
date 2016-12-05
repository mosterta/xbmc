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

#include "HwLayerAllwinnerA10.h"
#include "HwLayerAdaptorVdpau.h"
#include "utils/log.h"

typedef unsigned char 		__u8;
typedef unsigned short int 	__u16;
typedef unsigned int  		__u32;
typedef int 			__s32;
#include <linux/sunxi_disp_ioctl.h>
#include <sys/ioctl.h>
#include <string.h>

CHwLayerAllwinnerA10::CHwLayerAllwinnerA10(CHwLayerConfigAllwinner &config) : 
             CHwLayer<CHwLayerAdaptorVdpauAllwinner, CHwLayerConfigAllwinner>(config),
             m_config(config), m_layerId(-1), m_layerCreated(false),
             m_layerOpened(false), m_layerVideoStarted(false),
             m_alphaValue(PropertyKey::AlphaValue, 0), m_alphaEnable(PropertyKey::AlphaEnable, 0),
             m_colorKeyEnable(PropertyKey::ColorKeyEnable, 0), 
             m_scalerType(PropertyKey::ScalerType, (int)ScalerType::Type_Normal),
             m_colorSpace(PropertyKey::ColorSpace, (int)ColorSpace::BT709),
             m_interlaceMode(PropertyKey::InterlaceMode, (int)Interlace::IlaceOff), 
             m_frameId(0)
{
}

CHwLayerAllwinnerA10::~CHwLayerAllwinnerA10()
{
  hide();
  destroy();
};

bool CHwLayerAllwinnerA10::initialize(CHwLayerConfigAllwinner &config)
{
  m_config = config;
};

bool CHwLayerAllwinnerA10::create(HwLayerType type)
{
  bool status = true;
  m_layerType = type;
  if(type == HwLayerType::GUI)
  {
    if (ioctl(m_config.m_fbFd, FBIOGET_LAYER_HDL_0, &m_layerId) < 0)
    {
      CLog::Log(LOGERROR, "CHwLayerAllwinnerA10: get fb0 layer handle failed. (%d)", errno);
      status = false;
    }
    else
      m_layerCreated = true;
  }
  else if ( ! m_layerCreated)
  {
    unsigned long args[4];
    args[0] = m_config.m_screenId;
    if(m_scalerType.value == ScalerType::Type_Normal)
      args[1] = DISP_LAYER_WORK_MODE_NORMAL;
    else
      args[1] = DISP_LAYER_WORK_MODE_SCALER;
    args[2] = 0;
    args[3] = 0;
    m_layerId = ioctl(m_config.m_dispFd, DISP_CMD_LAYER_REQUEST, args);
    if (m_layerId <= 0)
    {
      m_layerId = -1;
      CLog::Log(LOGERROR, "CHwLayerAllwinnerA10: DISP_CMD_LAYER_REQUEST failed.\n");
      status = false;
    }
    else
      m_layerCreated = true;
  }
  else
  {
    status = false;
  }
  return status;
};

bool CHwLayerAllwinnerA10::destroy()
{
  bool status = true;
  
  if(m_layerType == HwLayerType::GUI)
  {
    m_layerId = -1;
  }
  else if (m_layerCreated)
  {
    unsigned long args[4];
    args[0] = m_config.m_screenId;
    args[1] = m_layerId;
    args[2] = 0;
    args[3] = 0;
    int error = ioctl(m_config.m_dispFd, DISP_CMD_LAYER_RELEASE, args);
    if (error < 0)
    {
      CLog::Log(LOGERROR, "CHwLayerAllwinnerA10: DISP_CMD_LAYER_RELEASE failed.\n");
      status = false;
    }
    m_layerId = -1;
    m_layerCreated = false;
  }
  else
  {
    // no valid layer set up
    status = false;
  }
  return status;

}

bool CHwLayerAllwinnerA10::configure(CHwLayerAdaptorVdpauAllwinner &frame, CRect &srcRect, CRect &dstRect)
{
  bool status = true;
  __disp_layer_info_t layer_info;
  unsigned long args[4];

  if(!m_layerCreated)
    return false;

  args[0] = m_config.m_screenId;
  args[1] = m_layerId;
  args[2] = (unsigned long)&layer_info;
  args[3] = 0;
  struct CHwLayerAdaptorVdpauAllwinner::cFrameConfig config;

  frame.getFrameConfig(config);

  int ret = ioctl(m_config.m_dispFd, DISP_CMD_LAYER_GET_PARA, args);
  if(ret < 0)
  {
    CLog::Log(LOGERROR, "CHwLayerAllwinnerA10: get para failed\n");
    status = false;
  }
  if(m_layerType == HwLayerType::Video)
    layer_info.pipe = 1;
  else
    layer_info.pipe = 0;

  if(m_scalerType.value == ScalerType::Type_Normal)
    layer_info.mode = DISP_LAYER_WORK_MODE_NORMAL;
  else
    layer_info.mode = DISP_LAYER_WORK_MODE_SCALER;
  
  layer_info.fb.format = DISP_FORMAT_YUV420;
  layer_info.fb.seq = DISP_SEQ_UVUV;
  switch (config.dataFormat) {
    case CHwLayerAdaptorVdpauAllwinner::YCBCR_FORMAT_YUYV:
      layer_info.fb.mode = DISP_MOD_INTERLEAVED;
      layer_info.fb.format = DISP_FORMAT_YUV422;
      layer_info.fb.seq = DISP_SEQ_YUYV;
      break;
    case CHwLayerAdaptorVdpauAllwinner::YCBCR_FORMAT_UYVY:
      layer_info.fb.mode = DISP_MOD_INTERLEAVED;
      layer_info.fb.format = DISP_FORMAT_YUV422;
      layer_info.fb.seq = DISP_SEQ_UYVY;
      break;
    case CHwLayerAdaptorVdpauAllwinner::YCBCR_FORMAT_NV12:
      layer_info.fb.mode = DISP_MOD_NON_MB_UV_COMBINED;
      break;
    case CHwLayerAdaptorVdpauAllwinner::YCBCR_FORMAT_YV12:
      layer_info.fb.mode = DISP_MOD_NON_MB_PLANAR;
      break;
    default:
    case CHwLayerAdaptorVdpauAllwinner::YCBCR_FORMAT_INTERNAL:
      layer_info.fb.mode = DISP_MOD_MB_UV_COMBINED;
      break;
  }
	
  layer_info.fb.br_swap = 0;
  layer_info.fb.addr[0] = (__u32)config.addrY;
  layer_info.fb.addr[1] = (__u32)config.addrU;
  layer_info.fb.addr[2] = (__u32)config.addrV;

  switch(m_colorSpace.value)
  {
    case ColorSpace::BT709:
      layer_info.fb.cs_mode = DISP_BT709;
      break;
    case ColorSpace::BT601:
      layer_info.fb.cs_mode = DISP_BT601;
      break;
    default:
      layer_info.fb.cs_mode = DISP_BT709;
      break;
  }
      
  layer_info.fb.size.width = config.fbSize.width;
  layer_info.fb.size.height = config.fbSize.height;
  layer_info.src_win.x = srcRect.x1;
  layer_info.src_win.y = srcRect.y1;
  layer_info.src_win.width = srcRect.x2 - srcRect.x1;
  layer_info.src_win.height = srcRect.y2 - srcRect.y1;
  layer_info.scn_win.x = dstRect.x1;
  layer_info.scn_win.y = dstRect.y1;
  layer_info.scn_win.width = dstRect.x2 - dstRect.x1;
  layer_info.scn_win.height = dstRect.y2 - dstRect.y1;
  layer_info.ck_enable = m_alphaEnable.value;

  if (layer_info.scn_win.y < 0)
  {
    int cutoff = -(layer_info.scn_win.y);
    layer_info.src_win.y += cutoff;
    layer_info.src_win.height -= cutoff;
    layer_info.scn_win.y = 0;
    layer_info.scn_win.height -= cutoff;
  }

  ret = ioctl(m_config.m_dispFd, DISP_CMD_LAYER_SET_PARA, args);
  if(ret < 0)
  {
    CLog::Log(LOGERROR, "CHwLayerAllwinnerA10: set para failed\n");
    status = false;
  }
  return status;
};

bool CHwLayerAllwinnerA10::hide()
{
  unsigned long args[4];
  bool status = true;
 
  if( ! m_layerCreated )
    return false;

  args[0] = m_config.m_screenId;
  args[1] = m_layerId;
  args[2] = 0;
  args[3] = 0;
  if(m_layerVideoStarted)
  {
    int error = ioctl(m_config.m_dispFd, DISP_CMD_VIDEO_STOP, args);
    if(error < 0)
    {
      CLog::Log(LOGERROR, "CHwLayerAllwinnerA10: video start failed, errno=%d\n", errno);
      status = false;
    }
    m_layerVideoStarted = false;
  }
  args[2] = 0;
  
  if(m_layerOpened)
  {
    int error = ioctl(m_config.m_dispFd, DISP_CMD_LAYER_CLOSE, args);
    if(error < 0)
    {
      CLog::Log(LOGERROR, "CHwLayerAllwinnerA10: layer open failed, errno=%d\n", errno);
      status = false;
    }
    m_layerOpened = false;
  }
  return status;
};

bool CHwLayerAllwinnerA10::show()
{
  unsigned long args[4];
  bool status = true;
  
  if( ! m_layerCreated )
    return -1;

  args[0] = m_config.m_screenId;
  args[1] = m_layerId;
  args[2] = 0;
  args[3] = 0;
  if(!m_layerOpened)
  {
    int error = ioctl(m_config.m_dispFd, DISP_CMD_LAYER_OPEN, args);
    if(error < 0)
    {
      CLog::Log(LOGERROR, "CHwLayerAllwinnerA10: layer open failed, errno=%d\n", errno);
      status = false;
    }
    m_layerOpened = true;
  }
  
  args[2] = 0;
  if(!m_layerVideoStarted)
  {
    int error = ioctl(m_config.m_dispFd, DISP_CMD_VIDEO_START, args);
    if(error < 0)
    {
      CLog::Log(LOGERROR, "CHwLayerAllwinnerA10: video start failed, errno=%d\n", errno);
      status = false;
    }
    m_layerVideoStarted = true;
  }
  
  return status;
};

bool CHwLayerAllwinnerA10::top()
{
  unsigned long args[4];
  int cmd;

  if( ! m_layerCreated )
    return false;

  cmd = DISP_CMD_LAYER_TOP;
  args[0] = m_config.m_screenId;
  args[1] = m_layerId;
  args[2] = 0;
  args[3] = 0;
  if (ioctl(m_config.m_dispFd, cmd, args))
  {
    CLog::Log(LOGERROR, "CHwLayerAllwinnerA10: DISP_CMD_LAYER_TOP video layer failed.\n");
    return false;
  }
  return true;
};

bool CHwLayerAllwinnerA10::back()
{
  unsigned long args[4];
  int cmd;

  if( ! m_layerCreated )
    return false;

  cmd = DISP_CMD_LAYER_BOTTOM;
  args[0] = m_config.m_screenId;
  args[1] = m_layerId;
  args[2] = 0;
  args[3] = 0;
  if (ioctl(m_config.m_dispFd, cmd, args))
  {
    CLog::Log(LOGERROR, "CHwLayerAllwinnerA10: DISP_CMD_LAYER_BOTTOM video layer failed.\n");
    return false;
  }
  return true;
};

bool CHwLayerAllwinnerA10::displayFrame(CHwLayerAdaptorVdpauAllwinner &frame, VDPAU::CVdpauRenderPicture *buffer, int top_field)
{
  bool status = true;
  int ret;

  if( ! m_layerCreated )
    return false;

  struct CHwLayerAdaptorVdpauAllwinner::cFrameConfig config;
  frame.getFrameConfig(config);
  
  __disp_video_fb_t fb_info;
  memset(&fb_info, 0, sizeof(fb_info));
	
  fb_info.id = buffer->frameId = m_frameId++;
  fb_info.addr[0] = (__u32)config.addrY;
  fb_info.addr[1] = (__u32)config.addrU;
  fb_info.addr[2] = (__u32)config.addrV;
  if(m_interlaceMode.value == CPropertyValue::IlaceOff)
    fb_info.interlace = 0;
  else
    fb_info.interlace = 1;
  fb_info.top_field_first = top_field;

  unsigned long args[4];
  args[0] =  m_config.m_screenId;
  args[1] =  m_layerId; 
  args[2] =  (unsigned long)(&fb_info); 
  args[3] =  0;

  int error = ioctl(m_config.m_dispFd, DISP_CMD_VIDEO_SET_FB, args);
  if(error < 0)
  {
    CLog::Log(LOGERROR, "CHwLayerAllwinnerA10: set video framebuffer failed\n");
    ret = false;
  }

  return status;
}

bool CHwLayerAllwinnerA10::syncFrame(VDPAU::CVdpauRenderPicture *pic)
{
  bool busy = false;
  int curDisplayedFrameId;
  bool status = getCurrentFrameId(curDisplayedFrameId);
  if(! status )
    CLog::Log(LOGERROR, "CHwLayerAllwinnerA10:%s error calling getCurrentFrameId", __FUNCTION__);

  if(status && curDisplayedFrameId != -1 && (pic->frameId >= curDisplayedFrameId))
  {
    busy = true;
  }
  return busy;
}

bool CHwLayerAllwinnerA10::getCurrentFrameId(int &frameId)
{
  bool status = true;
  unsigned long args[4];
  args[0] =  m_config.m_screenId;
  args[1] =  m_layerId; 
  args[2] =  0; 
  args[3] =  0;
  frameId = ioctl(m_config.m_dispFd, DISP_CMD_VIDEO_GET_FRAME_ID, args);
  if(frameId < 0)
  {
    CLog::Log(LOGERROR, "get frame id failed\n");
    status = false;
  }
  return status;
};

bool CHwLayerAllwinnerA10::setProperty(CPropertyValue &prop)
{
  bool status;
  switch(prop.key)
  {
    case PropertyKey::AlphaValue:
      m_alphaValue = prop;
      if(m_layerCreated)
        status = setAlphaValue(m_alphaValue.value);
      break;
    case PropertyKey::AlphaEnable:
      m_alphaEnable = prop;
      if(m_layerCreated)
        status = setAlphaEnable(m_alphaEnable.value);
      break;
    case PropertyKey::ColorKeyEnable:
      m_colorKeyEnable = prop;
      if(m_layerCreated)
        status = setColorKeyEnable(m_colorKeyEnable.value);
      break;
    case PropertyKey::ScalerType:
      m_scalerType = prop;
      if(m_layerCreated)
        status = setScalerType((ScalerType)m_scalerType.value);
      break;
    case PropertyKey::ColorSpace:
      m_colorSpace = prop;
      break;
    case PropertyKey::InterlaceMode:
      m_interlaceMode = prop;
      break;
    default:
      status = false;
      break;
  }
  return status;
};

bool CHwLayerAllwinnerA10::setAlphaValue(int alphaValue)
{
  unsigned long args[4];
  int cmd = DISP_CMD_LAYER_SET_ALPHA_VALUE;
  args[0] = m_config.m_screenId;
  args[1] = m_layerId;
  args[2] = alphaValue;
  if (ioctl(m_config.m_dispFd, cmd, &args) < 0)
  {
    CLog::Log(LOGERROR, "CHwLayerAllwinnerA10: DISP_CMD_LAYER_ALPHA_VALUE for layer(%d) failed.",
           m_layerId);
    return false;
  }
  return true;
};

bool CHwLayerAllwinnerA10::setAlphaEnable(int alphaEnable)
{
  unsigned long args[4];
  int cmd;
  if(alphaEnable == 0)
    cmd = DISP_CMD_LAYER_ALPHA_OFF;
  else
    cmd = DISP_CMD_LAYER_ALPHA_ON;
  args[0] = m_config.m_screenId;
  args[1] = m_layerId;
  if (ioctl(m_config.m_dispFd, cmd, &args) < 0)
  {
    CLog::Log(LOGERROR, "CHwLayerAllwinnerA10: DISP_CMD_LAYER_ALPHA_ON/OFF for layer(%d) failed.",
         m_layerId);
    return false;
  }
  return true;
}

bool CHwLayerAllwinnerA10::setColorKeyEnable(int ckEnable)
{
  unsigned long args[4];
  int cmd;
  if(ckEnable == 0)
    cmd = DISP_CMD_LAYER_CK_OFF;
  else
    cmd = DISP_CMD_LAYER_CK_ON;
  args[0] = m_config.m_screenId;
  args[1] = m_layerId;
  if (ioctl(m_config.m_dispFd, cmd, &args) < 0)
  {
    CLog::Log(LOGERROR, "CHwLayerAllwinnerA10: DISP_CMD_LAYER_CK_ON/OFF for layer(%d) failed.",
           m_layerId);
    return false;
  }
  return true;
};
bool CHwLayerAllwinnerA10::setScalerType(ScalerType scaler)
{
  unsigned long args[4];
  __disp_layer_info_t layer_info;

  args[0] = m_config.m_screenId;
  args[1] = m_layerId;
  args[2] = (unsigned long)(&layer_info);
  args[3] = 0;

  int ret = ioctl(m_config.m_dispFd, DISP_CMD_LAYER_GET_PARA, args);
  if(ret < 0)
  {
    CLog::Log(LOGERROR, "CHwLayerAllwinnerA10: get para failed\n");
    return false;
  }
  if(scaler == ScalerType::Type_Scale)
    layer_info.mode = DISP_LAYER_WORK_MODE_SCALER;
  else if(scaler == ScalerType::Type_Normal)
    layer_info.mode = DISP_LAYER_WORK_MODE_NORMAL;
  else
    return false;
  
  ret = ioctl(m_config.m_dispFd, DISP_CMD_LAYER_SET_PARA, args);
  if(ret < 0)
  {
    CLog::Log(LOGERROR, "CHwLayerAllwinnerA10: set para failed\n");
    return false;
  }

  return true;
};



