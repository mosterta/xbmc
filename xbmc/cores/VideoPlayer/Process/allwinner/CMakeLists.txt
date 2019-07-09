if(CORE_PLATFORM_NAME_LC STREQUAL allwinner)
  set(SOURCES ProcessInfoSunxi.cpp)

  set(HEADERS ProcessInfoSunxi.h)

  core_add_library(processSunxi)
endif()
