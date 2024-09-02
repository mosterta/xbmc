set(SOURCES RecursiveMutex.cpp)
set(HEADERS RecursiveMutex.h)

if(NOT CORE_SYSTEM_NAME STREQUAL linux AND NOT CORE_SYSTEM_NAME STREQUAL android AND NOT CORE_SYSTEM_NAME STREQUAL allwinner)
  list(APPEND SOURCES ThreadImplPosix.cpp)
  list(APPEND HEADERS ThreadImplPosix.h)
endif()

core_add_library(platform_posix_threads)
