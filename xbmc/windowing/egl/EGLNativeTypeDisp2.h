#include "EGLNativeTypeA10.h"

class CEGLNativeTypeDisp2 : public CEGLNativeTypeA10
{
public:
  CEGLNativeTypeDisp2(): CEGLNativeTypeA10() {};

protected:
  virtual int GetWidth();
  virtual int GetHeight();
  virtual int GetRefreshRate();

};
