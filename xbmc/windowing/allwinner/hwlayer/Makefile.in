INCLUDES=-I.

SRCS = HwLayerManagerAllwinner.cpp
SRCS+= HwLayerManagerAllwinnerDisp.cpp
SRCS+= HwLayerManagerAllwinnerDisp2.cpp
SRCS+= HwLayerAllwinnerA10.cpp
SRCS+= HwLayerAdaptorVdpau.cpp
SRCS+= HwLayerCommonAllwinner.cpp
SRCS+= HwLayerAllwinnerDisp.cpp

LIB = windowing_hwlayer.a

include ../../../Makefile.include
-include $(patsubst %.cpp,%.P,$(patsubst %.c,%.P,$(SRCS)))
