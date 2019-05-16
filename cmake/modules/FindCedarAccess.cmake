#.rst:
# FindCedarJpeg
# -------
# Finds the cedarJpeg library
#
# This will will define the following variables::
#
# CEDARACCESS_FOUND - system has cedarJpeg
# CEDARACCESS_INCLUDE_DIRS - the cedarJpeg include directory
# CEDARACCESS_LIBRARIES - the cedarJpeg libraries
# CEDARACCESS_DEFINITIONS - the cedarJpeg definitions
#
# and the following imported targets::
#
#   CEDARACCESS::CEDARACCESS   - The cedarJpeg library

if(PKG_CONFIG_FOUND)
  pkg_check_modules(PC_CEDARACCESS cedar_access QUIET)
endif()

find_path(CEDARACCESS_INCLUDE_DIR ve.h
                          PATHS ${PC_CEDARACCESS_INCLUDEDIR})

find_library(CEDARACCESS_LIBRARY NAMES CEDARACCESS cedar_access
                         PATHS ${PC_CEDARACCESS_LIBDIR})

set(CEDARACCESS_VERSION ${PC_CEDARACCESS_VERSION})

include(FindPackageHandleStandardArgs)
find_package_handle_standard_args(CEDARACCESS
                                  REQUIRED_VARS CEDARACCESS_LIBRARY CEDARACCESS_INCLUDE_DIR
                                  VERSION_VAR CEDARACCESS_VERSION)

if(CEDARACCESS_FOUND)
  set(CEDARACCESS_LIBRARIES ${CEDARACCESS_LIBRARY})
  set(CEDARACCESS_INCLUDE_DIRS ${CEDARACCESS_INCLUDE_DIR})
  set(CEDARACCESS_DEFINITIONS -DHAVE_LIBCEDARACCESS=1)

  if(NOT TARGET CEDARACCESS::CEDARACCESS)
    add_library(CEDARACCESS::CEDARACCESS UNKNOWN IMPORTED)
    set_target_properties(CEDARACCESS::CEDARACCESS PROPERTIES
                                   IMPORTED_LOCATION "${CEDARACCESS_LIBRARY}"
                                   INTERFACE_INCLUDE_DIRECTORIES "${CEDARACCESS_INCLUDE_DIR}"
                                   INTERFACE_COMPILE_DEFINITIONS HAVE_LIBCEDARACCESS=1)
  endif()
endif()

mark_as_advanced(CEDARACCESS_INCLUDE_DIR CEDARACCESS_LIBRARY)
