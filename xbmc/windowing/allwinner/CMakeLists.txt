set(SOURCES GLContextEGL.cpp
            WinSystemSunxi.cpp
            VideoSyncFb.cpp
            SunxiDisp.cpp
            SunxiDisp2.cpp)

set(HEADERS GLContextEGL.h
            WinSystemSunxi.h
            VideoSyncFb.h
            SunxiDisp.h
            SunxiDisp2.h
            SunxiDispBase.h)

if(OPENGLES_FOUND)
  list(APPEND SOURCES WinSystemSunxiGLESContext.cpp)
  list(APPEND HEADERS WinSystemSunxiGLESContext.h)
endif()

add_subdirectory(hwlayer)

core_add_library(windowing_allwinner)
