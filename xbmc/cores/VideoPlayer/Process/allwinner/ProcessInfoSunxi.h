/*
 *  Copyright (C) 2005-2018 Team Kodi
 *  This file is part of Kodi - https://kodi.tv
 *
 *  SPDX-License-Identifier: GPL-2.0-or-later
 *  See LICENSES/README.md for more information.
 */

#pragma once

#include "cores/IPlayer.h"
#include "cores/VideoPlayer/Process/ProcessInfo.h"
#include "cores/VideoPlayer/Buffers/VideoBufferSunxi.h"

#include "cores/VideoPlayer/VideoRenderers/HwDecRender/VdpauCedar.h"
#include "vdpau/vdpau.h"
#include <memory>

class CProcessInfoSunxi : public CProcessInfo
{
public:
  CProcessInfoSunxi();
  static CProcessInfo* Create();
  static void Register();
  EINTERLACEMETHOD GetFallbackDeintMethod() override;
  bool AllowDTSHDDecode() override;
  void SetSwDeinterlacingMethods();
  std::shared_ptr<CVideoBufferPoolSunxi> GetBufferPool();

protected:
  VDPAU::CInteropStateCedar m_interopState;
  std::shared_ptr<CVideoBufferPoolSunxi> m_bufferPool;
};
