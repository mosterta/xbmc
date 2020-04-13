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

#include "VideoSyncFb.h"
//#include "guilib/GraphicContext.h"
//#include "windowing/WindowingFactory.h"
#include "utils/TimeUtils.h"
#include "utils/log.h"
#include "threads/Thread.h"
#include <sys/ioctl.h>
#include <linux/fb.h>
#include <string>
#include "ServiceBroker.h"
#include "windowing/WinSystem.h"
#include "windowing/GraphicContext.h"

using namespace std;

static int timeval_subtract (struct timeval *result, struct timeval *x, struct timeval *y)
{
  /* Perform the carry for the later subtraction by updating y. */
  if (x->tv_usec < y->tv_usec) {
    int nsec = (y->tv_usec - x->tv_usec) / 1000000 + 1;
    y->tv_usec -= 1000000 * nsec;
    y->tv_sec += nsec;
  }
  if (x->tv_usec - y->tv_usec > 1000000) {
    int nsec = (y->tv_usec - x->tv_usec) / 1000000;
    y->tv_usec += 1000000 * nsec;
    y->tv_sec -= nsec;
  }
  /* Compute the time remaining to wait.
  tv_usec is certainly positive. */
  result->tv_sec = x->tv_sec - y->tv_sec;
  result->tv_usec = x->tv_usec - y->tv_usec;

  /* Return 1 if result is negative. */
  return x->tv_sec < y->tv_sec;
}

CVideoSyncFb::CVideoSyncFb(void *clock, int fbNum) : CVideoSync(clock), m_fbNum(fbNum)
{
}

bool CVideoSyncFb::Setup(PUPDATECLOCK func)
{
  UpdateClock = func;
  m_abort = false;
  CServiceBroker::GetWinSystem()->Register(this);
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

void CVideoSyncFb::Run(CEvent& stopEvent)
{
  /* This shouldn't be very busy and timing is important so increase priority */
  CThread::GetCurrentThread()->SetPriority(CThread::GetCurrentThread()->GetPriority()+1);
  struct timeval curr;
  struct timeval last;
  struct timeval diff;
  
  gettimeofday(&last, NULL);
  int fps = CServiceBroker::GetWinSystem()->GetGfxContext().GetFPS();

  while (!stopEvent.Signaled() && !m_abort)
  {
    if(ioctl(m_fd_fb, FBIO_WAITFORVSYNC) < 0)
      CLog::Log(LOGERROR, "CVideoReferenceClock: ioctl FBIO_WAITFORVSYNC errno=%d", errno);

    gettimeofday(&curr, NULL);
    timeval_subtract(&diff, &curr, &last);
    diff.tv_usec += (1000000 / fps / 2);
    uint64_t now = CurrentHostCounter();
    UpdateClock(diff.tv_usec / (1000000 / fps), now, m_refClock);
    last = curr;
  }
}

void CVideoSyncFb::Cleanup()
{
  CLog::Log(LOGDEBUG, "CVideoReferenceClock: cleaning up Fb%d", m_fbNum);
  if(m_fd_fb >= 0)
     close(m_fd_fb);
  CServiceBroker::GetWinSystem()->Unregister(this);
}

float CVideoSyncFb::GetFps()
{
  m_fps = CServiceBroker::GetWinSystem()->GetGfxContext().GetFPS();
  CLog::Log(LOGDEBUG, "CVideoReferenceClock: fps: %.2f", m_fps);
  return m_fps;
}

void CVideoSyncFb::OnResetDisplay()
{
  m_abort = true;
}

