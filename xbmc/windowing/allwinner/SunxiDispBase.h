#pragma once

class SunxiDispBase
{
  public:
    SunxiDispBase(int fd, int id) : m_screenid(id), m_hdisp(fd) {};
    virtual int GetWidth() = 0;
    virtual int GetHeight() = 0;
    virtual int GetRefreshRate() = 0;
  protected:
    int m_screenid;
    int m_hdisp;
};
