set (SOURCES HwLayerAdaptorVdpau.cpp
             HwLayerAllwinnerA10.cpp
             HwLayerManagerAllwinner.cpp )

set (HEADERS HwLayerAdaptorVdpau.h
             HwLayerAllwinnerA10.h
             HwLayer.h
             HwLayerManagerAllwinner.h
             HwLayerManager.h )

core_add_library(windowing_hwlayer)

