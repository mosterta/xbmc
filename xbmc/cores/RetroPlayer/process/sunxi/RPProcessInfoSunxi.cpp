/*
 *  Copyright (C) 2017-2018 Team Kodi
 *  This file is part of Kodi - https://kodi.tv
 *
 *  SPDX-License-Identifier: GPL-2.0-or-later
 *  See LICENSES/README.md for more information.
 */

#include "RPProcessInfoSunxi.h"

using namespace KODI;
using namespace RETRO;

CRPProcessInfoSunxi::CRPProcessInfoSunxi() :
  CRPProcessInfo("Sunxi")
{
}

std::unique_ptr<CRPProcessInfo> CRPProcessInfoSunxi::Create()
{
  return std::unique_ptr<CRPProcessInfoSunxi>();
}

void CRPProcessInfoSunxi::Register()
{
  CRPProcessInfo::RegisterProcessControl(CRPProcessInfoSunxi::Create);
}
