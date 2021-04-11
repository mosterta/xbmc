/*
 *  Copyright (C) 2005-2018 Team Kodi
 *  This file is part of Kodi - https://kodi.tv
 *
 *  SPDX-License-Identifier: GPL-2.0-or-later
 *  See LICENSES/README.md for more information.
 */

#include "ProcessInfoSunxi.h"
#include "utils/log.h"

#if not defined (GLsizei)
typedef int GLsizei;
#endif

#include <libcedarDisplay.h>

extern "C" {
#include "libavutil/imgutils.h"
}

// Override for platform ports

CProcessInfo* CProcessInfoSunxi::Create()
{
  return new CProcessInfoSunxi();
}

CProcessInfoSunxi::CProcessInfoSunxi()
{
  m_interopState.Init(0,0,0);
  m_bufferPool = std::make_shared<CVideoBufferPoolSunxi>(m_interopState.GetInterop());
  m_videoBufferManager.RegisterPool(m_bufferPool);
}

void CProcessInfoSunxi::Register()
{
  CProcessInfo::RegisterProcessControl("allwinner", CProcessInfoSunxi::Create);
}

EINTERLACEMETHOD CProcessInfoSunxi::GetFallbackDeintMethod()
{
  return EINTERLACEMETHOD::VS_INTERLACEMETHOD_RENDER_BOB;
}

bool CProcessInfoSunxi::AllowDTSHDDecode()
{
  return true;
}

void CProcessInfoSunxi::SetSwDeinterlacingMethods()
{
  // first populate with the defaults from base implementation
  CProcessInfo::SetSwDeinterlacingMethods();

  std::list<EINTERLACEMETHOD> methods;
  {
    // get the current methods
    CSingleLock lock(m_videoCodecSection);
    methods = m_deintMethods;
  }
  // add bob and blend deinterlacer for osx
  methods.push_back(EINTERLACEMETHOD::VS_INTERLACEMETHOD_RENDER_BLEND);
  methods.push_back(EINTERLACEMETHOD::VS_INTERLACEMETHOD_RENDER_WEAVE);
  methods.push_back(EINTERLACEMETHOD::VS_INTERLACEMETHOD_RENDER_BOB);

  UpdateDeinterlacingMethods(methods);
  SetDeinterlacingMethodDefault(EINTERLACEMETHOD::VS_INTERLACEMETHOD_RENDER_BOB);
}

std::shared_ptr<CVideoBufferPoolSunxi> CProcessInfoSunxi::GetBufferPool()
{
  return m_bufferPool;
}
