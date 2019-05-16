set (SOURCES HwLayerAdaptorVdpau.cpp
             HwLayerAllwinnerA10.cpp
             HwLayerAllwinnerDisp.cpp
             HwLayerCommonAllwinner.cpp
             HwLayerManagerAllwinner.cpp 
             HwLayerManagerAllwinnerDisp2.cpp
             HwLayerManagerAllwinnerDisp.cpp )

set (HEADERS HwLayerAdaptorVdpau.h
             HwLayerAllwinnerA10.h
             HwLayerAllwinnerDefs.h
             HwLayerAllwinnerDisp.h
             HwLayerFactory.h
             HwLayer.h
             HwLayerManagerAllwinnerDisp2.h
             HwLayerManagerAllwinnerDisp.h
             HwLayerManagerAllwinner.h
             HwLayerManagerAllwinnerItf.h
             HwLayerManager.h 
             sunxi_display2.h
             sync.h )

core_add_library(windowing_hwlayer)

