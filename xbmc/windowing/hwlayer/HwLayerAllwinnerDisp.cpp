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

#include "HwLayerAllwinnerDisp.h"
#include "HwLayerAdaptorVdpau.h"
#include "utils/log.h"

#include <sys/ioctl.h>
#include <string.h>

#include "sync.h"

CHwLayerAllwinnerDisp2::CHwLayerAllwinnerDisp2(CHwLayerConfigAllwinner &config) : 
             CHwLayer<CHwLayerAdaptorVdpauAllwinner, CHwLayerConfigAllwinner>(config),
             m_config(config), m_layerId(-1), m_layerCreated(false),
             m_layerOpened(false), m_layerVideoStarted(false),
             m_alphaValue(PropertyKey::AlphaValue, 0), m_alphaEnable(PropertyKey::AlphaEnable, 0),
             m_colorKeyEnable(PropertyKey::ColorKeyEnable, 0), 
             m_scalerType(PropertyKey::ScalerType, (int)ScalerType::Type_Normal),
             m_colorSpace(PropertyKey::ColorSpace, (int)ColorSpace::BT709),
             m_interlaceMode(PropertyKey::InterlaceMode, (int)Interlace::IlaceOff),
             m_fenceBuffer(NULL)
{
}

CHwLayerAllwinnerDisp2::~CHwLayerAllwinnerDisp2()
{
  hide();
  destroy();
};

bool CHwLayerAllwinnerDisp2::initialize(CHwLayerConfigAllwinner &config)
{
  m_config = config;
  m_fenceBuffer = malloc(512);
  if(! m_fenceBuffer)
  {
    CLog::Log(LOGERROR, "CHwLayerAllwinnerDisp2: could not allocate buffer.\n");
    return false;
  }
};

bool CHwLayerAllwinnerDisp2::create(HwLayerType type)
{
  bool status = true;
  int zOrder = 0;
  unsigned long args[4] = { m_config.m_screenId, (unsigned long)(&m_layerConfig), 1, 0 };
    
  
  m_layerType = type;
  if ( ! m_layerCreated)
  {
    memset(&m_layerConfig, 0, sizeof(m_layerConfig));
    if(type == HwLayerType::GUI)
    {
      m_layerId = 0;
      m_channelId = 1;  
      
      m_layerConfig.channel = m_channelId;
      m_layerConfig.layer_id = m_layerId;
      
      if (ioctl(m_config.m_dispFd, DISP_LAYER_GET_CONFIG, args))
      {
        m_layerId = -1;
        CLog::Log(LOGERROR, "CHwLayerAllwinnerDisp2: DISP_LAYER_GET_CONFIG failed.\n");
        status = false;
      }
      zOrder = 2;
    }
    else
    {
      m_layerId = 1;
      m_channelId = 1;
      zOrder = 1;
      m_layerConfig.info.mode = LAYER_MODE_BUFFER;
      m_layerConfig.enable = 0;
      m_layerConfig.channel = m_channelId;
      m_layerConfig.layer_id = m_layerId;
      m_layerConfig.info.alpha_mode = 1;
      m_layerConfig.info.alpha_value = 255;
    }
    
    m_layerConfig.info.zorder = zOrder;

    if (ioctl(m_config.m_dispFd, DISP_LAYER_SET_CONFIG, args))
    {
      m_layerId = -1;
      CLog::Log(LOGERROR, "CHwLayerAllwinnerDisp2: DISP_LAYER_SET_CONFIG failed.\n");
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

bool CHwLayerAllwinnerDisp2::destroy()
{
  bool status = true;
  
  if (m_layerCreated)
  {
    unsigned long args[4] = { m_config.m_screenId, (unsigned long)(&m_layerConfig), 1, 0 };

    m_layerConfig.enable = 0;

    if (ioctl(m_config.m_dispFd, DISP_LAYER_SET_CONFIG, args))
    {
      m_layerId = -1;
      CLog::Log(LOGERROR, "CHwLayerAllwinnerDisp2: DISP_LAYER_SET_CONFIG failed.\n");
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

bool CHwLayerAllwinnerDisp2::configure(CHwLayerAdaptorVdpauAllwinner &frame, CRect &srcRect, CRect &dstRect)
{
  bool status = true;
  unsigned long args[4];

  if(!m_layerCreated)
    return false;

  args[0] = m_config.m_screenId;
  args[1] = (unsigned long)&m_layerConfig;
  args[2] = 1;
  args[3] = 0;
  struct CHwLayerAdaptorVdpauAllwinner::cFrameConfig config;

  frame.getFrameConfig(config);
  
  switch (config.dataFormat) {
    case CHwLayerAdaptorVdpauAllwinner::YCBCR_FORMAT_YUYV:
      m_layerConfig.info.fb.format = DISP_FORMAT_YUV422_I_YUYV;
      break;
    case CHwLayerAdaptorVdpauAllwinner::YCBCR_FORMAT_UYVY:
      m_layerConfig.info.fb.format = DISP_FORMAT_YUV422_I_UYVY;
      break;
    case CHwLayerAdaptorVdpauAllwinner::YCBCR_FORMAT_NV12:
      m_layerConfig.info.fb.format = DISP_FORMAT_YUV420_SP_UVUV;
      break;
    case CHwLayerAdaptorVdpauAllwinner::YCBCR_FORMAT_YV12:
      m_layerConfig.info.fb.format = DISP_FORMAT_YUV422_P;
      break;
    default:
    case CHwLayerAdaptorVdpauAllwinner::YCBCR_FORMAT_INTERNAL:
      m_layerConfig.info.fb.format = DISP_FORMAT_YUV420_P;
      break;
  }
	
  m_layerConfig.info.fb.addr[0] = (__u32)config.addrY;
  m_layerConfig.info.fb.addr[1] = (__u32)config.addrU;
  m_layerConfig.info.fb.addr[2] = (__u32)config.addrV;

  switch(m_colorSpace.value)
  {
    case ColorSpace::BT709:
      m_layerConfig.info.fb.color_space = DISP_BT709;
      break;
    case ColorSpace::BT601:
      m_layerConfig.info.fb.color_space = DISP_BT601;
      break;
    default:
      m_layerConfig.info.fb.color_space = DISP_BT709;
      break;
  }
      
  m_layerConfig.info.fb.size[0].width = config.fbSize.width;
  m_layerConfig.info.fb.size[0].height = config.fbSize.height;
  m_layerConfig.info.fb.size[1].width = config.fbSize.width/2;
  m_layerConfig.info.fb.size[1].height = config.fbSize.height/2;
  m_layerConfig.info.fb.size[2].width = config.fbSize.width/2;
  m_layerConfig.info.fb.size[2].height = config.fbSize.height/2;
  m_layerConfig.info.fb.align[0] = 32;
  m_layerConfig.info.fb.align[1] = 16;  
  m_layerConfig.info.fb.align[2] = 16;
  m_layerConfig.info.fb.crop.x = (long long)srcRect.x1 << 32;
  m_layerConfig.info.fb.crop.y = (long long)srcRect.y1 << 32;
  m_layerConfig.info.fb.crop.width = (long long)(srcRect.x2 - srcRect.x1) << 32;
  m_layerConfig.info.fb.crop.height = (long long)(srcRect.y2 - srcRect.y1) << 32;
  m_layerConfig.info.screen_win.x = srcRect.x1;
  m_layerConfig.info.screen_win.y = srcRect.y1;
  m_layerConfig.info.screen_win.width = (srcRect.x2 - srcRect.x1);
  m_layerConfig.info.screen_win.height = (srcRect.y2 - srcRect.y1);
  m_layerConfig.info.fb.flags = DISP_BF_NORMAL;
  m_layerConfig.info.b_trd_out = 0;
  
  int ret = ioctl(m_config.m_dispFd, DISP_LAYER_SET_CONFIG, args);
  if(ret < 0)
  {
    CLog::Log(LOGERROR, "CHwLayerAllwinnerDisp2: set layer config failed\n");
    status = false;
  }
  return status;
};

bool CHwLayerAllwinnerDisp2::hide()
{
  bool status = true;
 
  if( ! m_layerCreated )
    return false;

  if(m_layerOpened)
  {
    unsigned long args[4] = { 0, (unsigned long)(&m_layerConfig), 1, 0 };

    m_layerConfig.enable = 0;

    if (ioctl(m_config.m_dispFd, DISP_LAYER_SET_CONFIG, args))
    {
      m_layerId = -1;
      CLog::Log(LOGERROR, "CHwLayerAllwinnerDisp2: DISP_LAYER_SET_CONFIG failed.\n");
      status = false;
    }
    m_layerOpened = false;
  }
  return status;
};

bool CHwLayerAllwinnerDisp2::show()
{
  bool status = true;
  
  if( ! m_layerCreated )
    return -1;

  if(!m_layerOpened)
  {
    unsigned long args[4] = { 0, (unsigned long)(&m_layerConfig), 1, 0 };

    m_layerConfig.enable = 1;

    if (ioctl(m_config.m_dispFd, DISP_LAYER_SET_CONFIG, args))
    {
      m_layerId = -1;
      CLog::Log(LOGERROR, "CHwLayerAllwinnerDisp2: DISP_LAYER_SET_CONFIG failed.\n");
      status = false;
    }
    m_layerOpened = true;
  }
  
  return status;
};

bool CHwLayerAllwinnerDisp2::top()
{
  int cmd;

  if( ! m_layerCreated )
    return false;

  unsigned long args[4] = { 0, (unsigned long)(&m_layerConfig), 1, 0 };

  m_layerConfig.info.zorder += 2;
  if (m_layerConfig.info.zorder > 2)
     m_layerConfig.info.zorder = 2;

  if (ioctl(m_config.m_dispFd, DISP_LAYER_SET_CONFIG, args))
  {
    m_layerId = -1;
    CLog::Log(LOGERROR, "CHwLayerAllwinnerDisp2: DISP_LAYER_SET_CONFIG failed.\n");
    return false;
  }
  return true;
};

bool CHwLayerAllwinnerDisp2::back()
{
  int cmd;

  if( ! m_layerCreated )
    return false;

  unsigned long args[4] = { 0, (unsigned long)(&m_layerConfig), 1, 0 };

  m_layerConfig.info.zorder -= 1;
  if (m_layerConfig.info.zorder < 1)
     m_layerConfig.info.zorder = 1;

  if (ioctl(m_config.m_dispFd, DISP_LAYER_SET_CONFIG, args))
  {
    m_layerId = -1;
    CLog::Log(LOGERROR, "CHwLayerAllwinnerDisp2: DISP_LAYER_SET_CONFIG failed.\n");
    return false;
  }
  return true;
};

//This is a structure copied from source file, there is no public header defined for it
typedef struct {
  int                 outaquirefencefd;
  int                 rotate;
  disp_capture_info   capturedata;
}WriteBack_t;
typedef struct
{
  int                 forceflip;
  int                 layer_num[2];
  disp_layer_config   layer_info[2][16];
  int                 firstdisplay;
  int*                aquireFenceFd;
  int                 aquireFenceCnt;
  int                 firstDispFenceCnt;
  int*                returnfenceFd;
  bool                needWB[2]; //[0] is HDMI, [1] is miracast
  unsigned int        ehancemode[2]; //0 is close,1 is whole,2 is half mode
  unsigned int        androidfrmnum;
  WriteBack_t         *WriteBackdata;
}setup_dispc_data_t;

bool CHwLayerAllwinnerDisp2::displayFrame(CHwLayerAdaptorVdpauAllwinner &frame, VDPAU::CVdpauRenderPicture *buffer, int top_field)
{
  bool status = true;
  int ret;
  setup_dispc_data_t hwc;
  int dispFenceFd[2];
  
  if( ! m_layerCreated )
    return false;

  struct CHwLayerAdaptorVdpauAllwinner::cFrameConfig config;
  frame.getFrameConfig(config);
  
  memset(&hwc, 0, sizeof(hwc));
  hwc.layer_num[1] = m_layerId;
  hwc.returnfenceFd = dispFenceFd;
  hwc.layer_info[m_config.m_screenId][0] = m_layerConfig;
  hwc.layer_info[m_config.m_screenId][0].info.fb.addr[0] = (__u32)config.addrY;
  hwc.layer_info[m_config.m_screenId][0].info.fb.addr[1] = (__u32)config.addrU;
  hwc.layer_info[m_config.m_screenId][0].info.fb.addr[2] = (__u32)config.addrV;
  if(m_interlaceMode.value == CPropertyValue::IlaceOff)
    hwc.layer_info[m_config.m_screenId][0].info.fb.scan = DISP_SCAN_PROGRESSIVE;
  else
  {
    if (top_field)
      hwc.layer_info[m_config.m_screenId][0].info.fb.scan = DISP_SCAN_INTERLACED_EVEN_FLD_FIRST;
    else
      hwc.layer_info[m_config.m_screenId][0].info.fb.scan = DISP_SCAN_INTERLACED_ODD_FLD_FIRST;
  }

  unsigned long args[4];
  args[0] =  m_config.m_screenId;
  args[1] =  (unsigned long)(&hwc); 
  args[2] =  0;
  args[3] =  0;

  int error = ioctl(m_config.m_dispFd, DISP_HWC_COMMIT, args);
  if(error < 0)
  {
    CLog::Log(LOGERROR, "CHwLayerAllwinnerDisp2: set video framebuffer failed\n");
    ret = false;
  }
  buffer->frameId = dispFenceFd[0];
  return status;
}

bool CHwLayerAllwinnerDisp2::syncFrame(VDPAU::CVdpauRenderPicture *pic)
{
  bool busy = false;
  struct sync_fence_info_data *fenceInfo = (struct sync_fence_info_data *)m_fenceBuffer;
  fenceInfo->len = 512;
  
  int status = ioctl(pic->frameId, SYNC_IOC_FENCE_INFO, m_fenceBuffer);
  if(status >= 0)
  {
    if(fenceInfo->status == 0) //fence still active
    {
      busy = true;
    }
    else
    {
      close (pic->frameId);
      pic->frameId = -1;
    }
  }
  return busy;
}

bool CHwLayerAllwinnerDisp2::setProperty(CPropertyValue &prop)
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

bool CHwLayerAllwinnerDisp2::setAlphaValue(int alphaValue)
{
  unsigned long args[4] = { 0, (unsigned long)(&m_layerConfig), 1, 0 };

  m_layerConfig.info.alpha_value = alphaValue;

  if (ioctl(m_config.m_dispFd, DISP_LAYER_SET_CONFIG, args))
  {
    m_layerId = -1;
    CLog::Log(LOGERROR, "CHwLayerAllwinnerDisp2: DISP_LAYER_SET_CONFIG failed.\n");
    return false;
  }
  return true;
};

bool CHwLayerAllwinnerDisp2::setAlphaEnable(int alphaEnable)
{
  unsigned long args[4] = { 0, (unsigned long)(&m_layerConfig), 1, 0 };

  m_layerConfig.info.alpha_mode = alphaEnable;

  if (ioctl(m_config.m_dispFd, DISP_LAYER_SET_CONFIG, args))
  {
    m_layerId = -1;
    CLog::Log(LOGERROR, "CHwLayerAllwinnerDisp2: DISP_LAYER_SET_CONFIG failed.\n");
    return false;
  }
  return true;
}

bool CHwLayerAllwinnerDisp2::setColorKeyEnable(int ckEnable)
{
  return true;
};
bool CHwLayerAllwinnerDisp2::setScalerType(ScalerType scaler)
{
  return true;
};



