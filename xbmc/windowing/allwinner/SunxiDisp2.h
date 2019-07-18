#pragma once

#include "windowing/allwinner/SunxiDispBase.h"

class SunxiDisp2 : public SunxiDispBase
{
  public:
    SunxiDisp2(int fd, int id);
    int GetWidth() override;
    int GetHeight() override;
    int GetRefreshRate() override;
};

