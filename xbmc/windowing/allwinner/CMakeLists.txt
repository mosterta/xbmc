set(SOURCES GLContextEGL.cpp
            WinSystemSunxi.cpp
            VideoSyncFb.cpp)

set(HEADERS GLContextEGL.h
            WinSystemSunxi.h
            VideoSyncFb.h)

if(OPENGLES_FOUND)
  list(APPEND SOURCES WinSystemSunxiGLESContext.cpp)
  list(APPEND HEADERS WinSystemSunxiGLESContext.h)
endif()

add_subdirectory(hwlayer)

core_add_library(windowing_allwinner)
