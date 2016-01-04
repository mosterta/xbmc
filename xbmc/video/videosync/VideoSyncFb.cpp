/*
 *      Copyright (C) 2005-2014 Team XBMC
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

#include "system.h"

#if defined(USE_VIDEO_SYNC_FB)

#include "video/videosync/VideoSyncFb.h"
#include "guilib/GraphicContext.h"
#include "windowing/WindowingFactory.h"
#include "utils/TimeUtils.h"
#include "utils/log.h"
#include "threads/Thread.h"
#include <sys/ioctl.h>
#include <linux/fb.h>
#include <string>

using namespace std;

CVideoSyncFb::CVideoSyncFb(int fbNum) : CVideoSync(), IDispResource(), m_fbNum(fbNum)
{
}

bool CVideoSyncFb::Setup(PUPDATECLOCK func)
{
  UpdateClock = func;
  m_abort = false;
  g_Windowing.Register(this);
  string fbName = string("/dev/fb") + to_string(m_fbNum);
  m_fd_fb = open(fbName.c_str(), O_RDWR);
  if(m_fd_fb < 0)
  {
     CLog::Log(LOGERROR, "CVideoReferenceClock: setting up Framebuffer failed");
     return false;
  }
  CLog::Log(LOGDEBUG, "CVideoReferenceClock: setting up Framebuffer");
  return true;
}

void CVideoSyncFb::Run(volatile bool& stop)
{
  /* This shouldn't be very busy and timing is important so increase priority */
  CThread::GetCurrentThread()->SetPriority(CThread::GetCurrentThread()->GetPriority()+1);

  while (!stop && !m_abort)
  {
    ioctl(m_fd_fb, FBIO_WAITFORVSYNC);
    uint64_t now = CurrentHostCounter();
    UpdateClock(1, now);
  }
}

void CVideoSyncFb::Cleanup()
{
  CLog::Log(LOGDEBUG, "CVideoReferenceClock: cleaning up RPi");
  if(m_fd_fb >= 0)
     close(m_fd_fb);
  g_Windowing.Unregister(this);
}

float CVideoSyncFb::GetFps()
{
  m_fps = g_graphicsContext.GetFPS();
  CLog::Log(LOGDEBUG, "CVideoReferenceClock: fps: %.2f", m_fps);
  return m_fps;
}

void CVideoSyncFb::OnResetDevice()
{
  m_abort = true;
}

#endif
