#.rst:
# FindVdpauSunxi
# -------
# Finds the vdpau_sunxi library
#
# This will will define the following variables::
#
# VDPAU_SUNXI_FOUND - system has vdpau_sunxi
# VDPAU_SUNXI_INCLUDE_DIRS - the vdpau_sunxi include directory
# VDPAU_SUNXI_LIBRARIES - the vdpau_sunxi libraries
# VDPAU_SUNXI_DEFINITIONS - the vdpau_sunxi definitions
#
# and the following imported targets::
#
#   VDPAU_SUNXI::VDPAU_SUNXI   - The vdpau_sunxi library

if(PKG_CONFIG_FOUND)
  pkg_check_modules(PC_VDPAU_SUNXI vdpau_sunxi QUIET)
endif()

#find_path(VDPAU_SUNXI_INCLUDE_DIR ve.h
#                          PATHS ${PC_VDPAU_SUNXI_INCLUDEDIR})
set(VDPAU_SUNXI_INCLUDE_DIR ${PC_VDPAU_SUNXI_INCLUDEDIR})

find_library(VDPAU_SUNXI_LIBRARY NAMES VDPAU_SUNXI vdpau_sunxi
                         PATHS ${PC_VDPAU_SUNXI_LIBDIR})

set(VDPAU_SUNXI_VERSION ${PC_VDPAU_SUNXI_VERSION})

include(FindPackageHandleStandardArgs)
find_package_handle_standard_args(VDPAU_SUNXI
                                  REQUIRED_VARS VDPAU_SUNXI_LIBRARY VDPAU_SUNXI_INCLUDE_DIR
                                  VERSION_VAR VDPAU_SUNXI_VERSION)

if(VDPAU_SUNXI_FOUND)
  set(VDPAU_SUNXI_LIBRARIES ${VDPAU_SUNXI_LIBRARY})
  set(VDPAU_SUNXI_INCLUDE_DIRS ${VDPAU_SUNXI_INCLUDE_DIR})
  set(VDPAU_SUNXI_DEFINITIONS -DHAVE_LIBVDPAU_SUNXI=1)

  if(NOT TARGET VDPAU_SUNXI::VDPAU_SUNXI)
    add_library(VDPAU_SUNXI::VDPAU_SUNXI UNKNOWN IMPORTED)
    set_target_properties(VDPAU_SUNXI::VDPAU_SUNXI PROPERTIES
                                   IMPORTED_LOCATION "${VDPAU_SUNXI_LIBRARY}"
                                   INTERFACE_COMPILE_DEFINITIONS HAVE_LIBVDPAU_SUNXI=1
                                   INTERFACE_INCLUDE_DIRECTORIES "${VDPAU_SUNXI_INCLUDE_DIR}")

  endif()
endif()

mark_as_advanced(VDPAU_SUNXI_INCLUDE_DIR VDPAU_SUNXI_LIBRARY)
