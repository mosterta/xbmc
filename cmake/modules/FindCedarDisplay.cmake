#.rst:
# FindCedarJpeg
# -------
# Finds the cedarJpeg library
#
# This will will define the following variables::
#
# CEDARDISPLAY_FOUND - system has cedarJpeg
# CEDARDISPLAY_INCLUDE_DIRS - the cedarJpeg include directory
# CEDARDISPLAY_LIBRARIES - the cedarJpeg libraries
# CEDARDISPLAY_DEFINITIONS - the cedarJpeg definitions
#
# and the following imported targets::
#
#   CEDARDISPLAY::CEDARDISPLAY   - The cedarJpeg library

if(PKG_CONFIG_FOUND)
  pkg_check_modules(PC_CEDARDISPLAY cedarDisplay QUIET)
endif()

find_path(CEDARDISPLAY_INCLUDE_DIR libcedarDisplay.h
                          PATHS ${PC_CEDARDISPLAY_INCLUDEDIR})

find_library(CEDARDISPLAY_LIBRARY NAMES CEDARDISPLAY cedarDisplay
                         PATHS ${PC_CEDARDISPLAY_LIBDIR})

set(CEDARDISPLAY_VERSION ${PC_CEDARDISPLAY_VERSION})

include(FindPackageHandleStandardArgs)
find_package_handle_standard_args(CEDARDISPLAY
                                  REQUIRED_VARS CEDARDISPLAY_LIBRARY CEDARDISPLAY_INCLUDE_DIR
                                  VERSION_VAR CEDARDISPLAY_VERSION)

if(CEDARDISPLAY_FOUND)
  set(CEDARDISPLAY_LIBRARIES ${CEDARDISPLAY_LIBRARY})
  set(CEDARDISPLAY_INCLUDE_DIRS ${CEDARDISPLAY_INCLUDE_DIR})
  set(CEDARDISPLAY_DEFINITIONS -DHAVE_LIBCEDARDISPLAY=1 -DUSE_ALLWINNER_DISP=1)

  if(NOT TARGET CEDARDISPLAY::CEDARDISPLAY)
    add_library(CEDARDISPLAY::CEDARDISPLAY UNKNOWN IMPORTED)
    set_target_properties(CEDARDISPLAY::CEDARDISPLAY PROPERTIES
                                   IMPORTED_LOCATION "${CEDARDISPLAY_LIBRARY}"
                                   INTERFACE_INCLUDE_DIRECTORIES "${CEDARDISPLAY_INCLUDE_DIR}"
                                   INTERFACE_COMPILE_DEFINITIONS HAVE_LIBCEDARDISPLAY=1)
  endif()
endif()

mark_as_advanced(CEDARDISPLAY_INCLUDE_DIR CEDARDISPLAY_LIBRARY)
