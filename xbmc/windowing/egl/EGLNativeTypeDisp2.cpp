#include "EGLNativeTypeDisp2.h"
#include <sys/ioctl.h>

typedef unsigned long u32;

#include "sunxi_display2.h"

int CEGLNativeTypeDisp2::GetWidth()
{
  unsigned long       args[4];
  args[0] = m_screenid;
  args[1] = 0;
  args[2] = 0;
  args[3] = 0;
  return ioctl(m_hdisp, DISP_GET_SCN_WIDTH, args);
}

int CEGLNativeTypeDisp2::GetHeight()
{
  unsigned long       args[4];
  args[0] = m_screenid;
  args[1] = 0;
  args[2] = 0;
  args[3] = 0;

  return ioctl(m_hdisp, DISP_GET_SCN_HEIGHT, args);
}

int CEGLNativeTypeDisp2::GetRefreshRate()
{
  disp_output output;
  int refreshRate = 50;
  
  unsigned long       args[4];
  args[0] = m_screenid;
  args[1] = (unsigned long)&output;
  args[2] = 0;
  args[3] = 0;

  int error = ioctl(m_hdisp, DISP_GET_OUTPUT, args);
  if (!error)
  {
    if(output.type == DISP_OUTPUT_TYPE_HDMI || output.type == DISP_OUTPUT_TYPE_TV)
    {
      switch(output.mode) 
      {
        case DISP_TV_MOD_NTSC:
        case DISP_TV_MOD_480I:
        case DISP_TV_MOD_480P:
          refreshRate = 60;
          break;
        case DISP_TV_MOD_PAL:
        case DISP_TV_MOD_576I:
        case DISP_TV_MOD_576P:
          refreshRate = 50;
          break;
        case DISP_TV_MOD_720P_50HZ:
        case DISP_TV_MOD_1080P_50HZ:
        case DISP_TV_MOD_1080I_50HZ:
          refreshRate = 50;
          break;
        case DISP_TV_MOD_720P_60HZ:
        case DISP_TV_MOD_1080P_60HZ:
        case DISP_TV_MOD_1080I_60HZ:
          refreshRate = 60;
          break;
        case DISP_TV_MOD_1080P_30HZ:
        case DISP_TV_MOD_3840_2160P_30HZ:
          refreshRate = 30;
          break;
        case DISP_TV_MOD_1080P_25HZ:
        case DISP_TV_MOD_3840_2160P_25HZ:
          refreshRate = 25;
          break;
        case DISP_TV_MOD_1080P_24HZ:
        case DISP_TV_MOD_3840_2160P_24HZ:
          refreshRate = 24;
          break;
      }
    }
  }
  
  return refreshRate;
}
