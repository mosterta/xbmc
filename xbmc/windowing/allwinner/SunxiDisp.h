#pragma once

#include "windowing/allwinner/SunxiDispBase.h"

class SunxiDisp : public SunxiDispBase
{
  public:
    SunxiDisp(int fd, int id);
    int GetWidth() override;
    int GetHeight() override;
    int GetRefreshRate() override;
};

