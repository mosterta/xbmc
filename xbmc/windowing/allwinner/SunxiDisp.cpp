#include "windowing/allwinner/SunxiDisp.h"
#include <sys/ioctl.h>
#include "drv_display_sun4i.h"
#include "utils/log.h"

SunxiDisp::SunxiDisp(int fd, int id) : SunxiDispBase(fd, id)
{
}

int SunxiDisp::GetWidth()
{
  unsigned long       args[4];
  args[0] = m_screenid;
  args[1] = 0;
  args[2] = 0;
  args[3] = 0;
  return ioctl(m_hdisp, DISP_CMD_SCN_GET_WIDTH , args);
}

int SunxiDisp::GetHeight()
{
  unsigned long       args[4];
  args[0] = m_screenid;
  args[1] = 0;
  args[2] = 0;
  args[3] = 0;

  return ioctl(m_hdisp, DISP_CMD_SCN_GET_HEIGHT, args);
}

int SunxiDisp::GetRefreshRate()
{
  unsigned long       args[4];
  unsigned int        i;
  int _refreshRate;

  args[0] = m_screenid;
  args[1] = 0;
  args[2] = 0;
  args[3] = 0;
  i = ioctl(m_hdisp, DISP_CMD_HDMI_GET_MODE, args);
  switch(i)
  {
    case DISP_TV_MOD_720P_50HZ:
    case DISP_TV_MOD_1080I_50HZ:
    case DISP_TV_MOD_1080P_50HZ:
      _refreshRate = 50.0;
      break;
    case DISP_TV_MOD_720P_60HZ:
    case DISP_TV_MOD_1080I_60HZ:
    case DISP_TV_MOD_1080P_60HZ:
      _refreshRate = 60.0;
      break;
    case DISP_TV_MOD_1080P_24HZ:
      _refreshRate = 24.0;
      break;
    default:
      CLog::Log(LOGERROR, "A10: display mode %d is unknown. Assume refreh rate 60Hz\n", i);
      _refreshRate = 60.0;
      break;
  }
  return _refreshRate;
}
