set(SOURCES GraphicContext.cpp
            OSScreenSaver.cpp
            Resolution.cpp
            WindowSystemFactory.cpp
            WinSystem.cpp)

set(HEADERS GraphicContext.h
            OSScreenSaver.h
            Resolution.h
            WinEvents.h
            WindowSystemFactory.h
            WinSystem.h
            XBMC_events.h
            VideoSync.h)

if(CORE_PLATFORM_NAME_LC STREQUAL rbpi)
  add_subdirectory(rpi)
endif()

if(CORE_PLATFORM_NAME_LC STREQUAL allwinner)
  add_subdirectory(allwinner)
endif()

core_add_library(windowing)
