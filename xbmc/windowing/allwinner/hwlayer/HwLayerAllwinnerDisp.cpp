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
#include "windowing/GraphicContext.h"

#include <sys/ioctl.h>
#include <string.h>
#include <linux/fb.h>

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
  memset(&m_layerConfig, 0, sizeof(m_layerConfig));
}

CHwLayerAllwinnerDisp2::~CHwLayerAllwinnerDisp2()
{
  destroy();
  if(m_fenceBuffer)
    free(m_fenceBuffer);
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
      
      if (ioctl(m_config.m_dispFd, DISP_LAYER_GET_CONFIG, args) < 0)
      {
        m_layerId = -1;
        CLog::Log(LOGERROR, "CHwLayerAllwinnerDisp2: DISP_LAYER_GET_CONFIG failed.\n");
        status = false;
      }
      zOrder = 2;
    }
    else
    {
      m_layerId = 0;
      m_channelId = 0;
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

    if(m_layerType != HwLayerType::GUI)
    {
      m_layerConfig.enable = 0;
  
      if (ioctl(m_config.m_dispFd, DISP_LAYER_SET_CONFIG, args))
      {
        m_layerId = -1;
        CLog::Log(LOGERROR, "CHwLayerAllwinnerDisp2: DISP_LAYER_SET_CONFIG failed.\n");
        status = false;
      }
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

bool CHwLayerAllwinnerDisp2::calcCroppedValues(cdRect_t *src, cdRect_t *scn, int screen_width)
{
  if (scn->y < 0)
  {
    int scn_clip = -scn->y;
    int src_clip = scn_clip * src->height / scn->height;
    scn->y = 0;
    scn->height -= scn_clip;
    src->y += src_clip;
    src->height -= src_clip;
  }
  if (scn->x < 0)
  {
    int scn_clip = -scn->x;
    int src_clip = scn_clip * src->width / scn->width;
    scn->x = 0;
    scn->width -= scn_clip;
    src->x += src_clip;
    src->width -= src_clip;
  }
  if (scn->x + scn->width > screen_width)
  {
    int scn_clip = scn->x + scn->width - screen_width;
    int src_clip = scn_clip * src->width / scn->width;
    scn->width -= scn_clip;
    src->width -= src_clip;
  }
}

bool CHwLayerAllwinnerDisp2::configure(CHwLayerAdaptorVdpauAllwinner &frame, CRect &srcRect, CRect &dstRect)
{
  CLog::Log(LOGDEBUG, " (CHwLayerAllwinnerDisp2) %s", __FUNCTION__);
  bool status = true;
  unsigned long args[4];
  __u32 addr0, addr1, addr2;

  if(!m_layerCreated)
    return false;

  args[0] = m_config.m_screenId;
  args[1] = (unsigned long)&m_layerConfig;
  args[2] = 1;
  args[3] = 0;
  struct CHwLayerAdaptorVdpauAllwinner::cFrameConfig config;

  if(! frame.getFrameConfig(config))
  {
    CLog::Log(LOGERROR, "CHwLayerAllwinnerDisp2:%s Failure in getting frame configuration!", __FUNCTION__);
    return false;
  }
  
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
      m_layerConfig.info.fb.format = DISP_FORMAT_YUV420_P;
      break;
    default:
    case CHwLayerAdaptorVdpauAllwinner::YCBCR_FORMAT_INTERNAL:
      m_layerConfig.info.fb.format = DISP_FORMAT_YUV420_SP_VUVU;
      break;
  }

  addr0 = (__u32)config.addrY;
  addr1 = (__u32)config.addrU;
  addr2 = (__u32)config.addrV;

  m_layerConfig.info.fb.addr[0] = addr0 ;
  m_layerConfig.info.fb.addr[1] = addr1 ;
  m_layerConfig.info.fb.addr[2] = addr2 ;

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

  cdRect_t src = { (int32_t)srcRect.x1, (int32_t)srcRect.y1, 
    (uint32_t)(srcRect.x2 - srcRect.x1), (uint32_t)(srcRect.y2 - srcRect.y1) };
  cdRect_t scn = { (int32_t)dstRect.x1, (int32_t)dstRect.y1, 
    (uint32_t)(dstRect.x2 - dstRect.x1), (uint32_t)(dstRect.y2 - dstRect.y1) };

  calcCroppedValues(&src, &scn, CServiceBroker::GetWinSystem()->GetGfxContext().GetWidth());

  m_layerConfig.info.fb.size[0].width = config.fbSize.width;
  m_layerConfig.info.fb.size[0].height = config.fbSize.height;
  m_layerConfig.info.fb.size[1].width = config.fbSize.width/2;
  m_layerConfig.info.fb.size[1].height = config.fbSize.height/2;
  m_layerConfig.info.fb.size[2].width = config.fbSize.width/2;
  m_layerConfig.info.fb.size[2].height = config.fbSize.height/2;
  m_layerConfig.info.fb.align[0] = config.alignY;
  m_layerConfig.info.fb.align[1] = config.alignU;
  m_layerConfig.info.fb.align[2] = config.alignV;
  m_layerConfig.info.fb.crop.x = ((long long)(src.x)) << 32;
  m_layerConfig.info.fb.crop.y = ((long long)(src.y)) << 32;
  m_layerConfig.info.fb.crop.width = ((long long)(src.width)) << 32;
  m_layerConfig.info.fb.crop.height = ((long long)(src.height)) << 32;
  m_layerConfig.info.screen_win.x = scn.x;
  m_layerConfig.info.screen_win.y = scn.y;
  m_layerConfig.info.screen_win.width = (scn.width);  
  m_layerConfig.info.screen_win.height = (scn.height);
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
    int                 layer_num[2];
    int			layer_active[2];
    disp_layer_config   layer_info[2][16];
    int*                aquireFenceFd;
    int                 aquireFenceCnt;
    int*                returnfenceFd;
    bool                needWB[2];
    unsigned int        ehancemode[2]; //0 is close,1 is whole,2 is half mode
    unsigned int        androidfrmnum;
    WriteBack_t         *WriteBackdata;
}setup_dispc_data_t;

bool CHwLayerAllwinnerDisp2::displayFrame(CHwLayerAdaptorVdpauAllwinner &frame, int &fence, int top_field)
{
  CLog::Log(LOGDEBUG, " (CHwLayerAllwinnerDisp2) %s", __FUNCTION__);
  bool status = true;
  int ret;
  setup_dispc_data_t hwc;
  int dispFenceFd[2];
  
  if( ! m_layerCreated || fence >= 0)
    return false;

  unsigned long args[4];
  args[0] =  m_config.m_screenId;
  args[1] =  (unsigned long)(&m_layerConfig);
  args[2] =  1;
  args[3] =  0;
  if (ioctl(m_config.m_dispFd, DISP_LAYER_GET_CONFIG, args) < 0)
  {
     m_layerId = -1;
     CLog::Log(LOGERROR, "CHwLayerAllwinnerDisp2: DISP_LAYER_GET_CONFIG failed.\n");
     status = false;
  }

  struct CHwLayerAdaptorVdpauAllwinner::cFrameConfig config;
  frame.getFrameConfig(config);

  memset(&hwc, 0, sizeof(hwc));
  hwc.layer_num[0] = 1;
  hwc.layer_active[0] = 1;
  hwc.ehancemode[0] = 1;
  hwc.returnfenceFd = dispFenceFd;
  m_layerConfig.info.fb.size[0].width = config.fbSize.width;
  m_layerConfig.info.fb.size[0].height = config.fbSize.height;
  m_layerConfig.info.fb.size[1].width = config.fbSize.width/2;
  m_layerConfig.info.fb.size[1].height = config.fbSize.height/2;
  m_layerConfig.info.fb.size[2].width = config.fbSize.width/2;
  m_layerConfig.info.fb.size[2].height = config.fbSize.height/2;
  m_layerConfig.info.fb.align[0] = config.alignY;
  m_layerConfig.info.fb.align[1] = config.alignU;
  m_layerConfig.info.fb.align[2] = config.alignV;
  m_layerConfig.info.fb.addr[0] = (__u32)config.addrY ;
  m_layerConfig.info.fb.addr[1] = (__u32)config.addrU ;
  m_layerConfig.info.fb.addr[2] = (__u32)config.addrV ;
  if(m_interlaceMode.value == CPropertyValue::IlaceOff)
    m_layerConfig.info.fb.scan = DISP_SCAN_PROGRESSIVE;
  else
  {
    if (top_field)
      m_layerConfig.info.fb.scan = DISP_SCAN_INTERLACED_ODD_FLD_FIRST;
    else
      m_layerConfig.info.fb.scan = DISP_SCAN_INTERLACED_EVEN_FLD_FIRST;
  }
  hwc.layer_info[0][0] = m_layerConfig;

  args[0] = m_config.m_screenId;
  args[1] = (unsigned long)&m_layerConfig;
  args[2] = 1;
  args[3] = 0;

  int error = ioctl(m_config.m_dispFd, DISP_LAYER_SET_CONFIG, args);
  if(error < 0)
  {
    CLog::Log(LOGERROR, "CHwLayerAllwinnerDisp2: set layer config failed\n");
    status = false;
  }

  args[0] =  m_config.m_screenId;
  args[1] =  (unsigned long)(&hwc); 
  args[2] =  0;
  args[3] =  0;

  error = ioctl(m_config.m_dispFd, DISP_HWC_COMMIT, args);
  if(error < 0)
  {
    CLog::Log(LOGERROR, "CHwLayerAllwinnerDisp2: set video framebuffer failed\n");
    ret = false;
  }
  if(fence >= 0)
  {
    close(fence);
  }

  fence = dispFenceFd[0];

  return status;
}

bool CHwLayerAllwinnerDisp2::getSyncFenceValue(int fence, HwLayerSyncValue &value)
{
  bool retval = false;
  struct sync_fence_info_data *fenceInfo = (struct sync_fence_info_data *)m_fenceBuffer;
  fenceInfo->len = 512;
 
  value = HwLayerSyncValue::HWLayerFenceUnsignaled;
 
  if (fence < 0)
     return false;

  int status = ioctl(fence, SYNC_IOC_FENCE_INFO, fenceInfo);
  if(status >= 0)
  {
    if(fenceInfo->status == 1) //fence still active
    {
      value = HwLayerSyncValue::HWLayerFenceSignaled;
      retval = true;
    }
  }
  else
  {
     CLog::Log(LOGERROR, "CHwLayerAllwinnerDisp2: syncFrame: returned error:%d errno:%d fence:%d\n", status, errno, fence);
  }
  return retval;
}
bool CHwLayerAllwinnerDisp2::destroySyncFence(int &fence)
{
  if(fence != -1)
    close(fence);
fence = -1;
  return true;
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



