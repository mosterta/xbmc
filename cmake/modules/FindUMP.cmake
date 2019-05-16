#.rst:
# FindUMP
# -------
# Finds the UMP library
#
# This will will define the following variables::
#
# UMP_FOUND - system has libUMP
# UMP_INCLUDE_DIRS - the libUMP include directory
# UMP_LIBRARIES - the libUMP libraries
# UMP_DEFINITIONS - the libUMP definitions
#
# and the following imported targets::
#
#   UMP::UMP   - The libUMP library

if(PKG_CONFIG_FOUND)
  pkg_check_modules(PC_UMP libump QUIET)
endif()

find_path(UMP_INCLUDE_DIR ump/ump.h
                          PATHS ${PC_UMP_INCLUDEDIR})

find_library(UMP_LIBRARY NAMES UMP libUMP
                         PATHS ${PC_UMP_LIBDIR})

set(UMP_VERSION ${PC_UMP_VERSION})

include(FindPackageHandleStandardArgs)
find_package_handle_standard_args(UMP
                                  REQUIRED_VARS UMP_LIBRARY UMP_INCLUDE_DIR
                                  VERSION_VAR UMP_VERSION)

if(UMP_FOUND)
  set(UMP_LIBRARIES ${UMP_LIBRARY})
  set(UMP_INCLUDE_DIRS ${UMP_INCLUDE_DIR})
  set(UMP_DEFINITIONS -DHAVE_LIBUMP=1)

  if(NOT TARGET UMP::UMP)
    add_library(UMP::UMP UNKNOWN IMPORTED)
    set_target_properties(UMP::UMP PROPERTIES
                                   IMPORTED_LOCATION "${UMP_LIBRARY}"
                                   INTERFACE_INCLUDE_DIRECTORIES "${UMP_INCLUDE_DIR}"
                                   INTERFACE_COMPILE_DEFINITIONS HAVE_LIBUMP=1)
  endif()
endif()

mark_as_advanced(UMP_INCLUDE_DIR UMP_LIBRARY)
