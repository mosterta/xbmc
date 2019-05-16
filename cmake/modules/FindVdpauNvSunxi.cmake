#.rst:
# FindVdpauNvSunxiJpeg
# -------
# Finds the vdpau_nv_sunxi library
#
# This will will define the following variables::
#
# VDPAU_NV_SUNXI_FOUND - system has vdpau_nv_sunxi
# VDPAU_NV_SUNXI_INCLUDE_DIRS - the vdpau_nv_sunxi include directory
# VDPAU_NV_SUNXI_LIBRARIES - the vdpau_nv_sunxi libraries
# VDPAU_NV_SUNXI_DEFINITIONS - the vdpau_nv_sunxi definitions
#
# and the following imported targets::
#
#   VDPAU_NV_SUNXI::VDPAU_NV_SUNXI   - The vdpau_nv_sunxi library

if(PKG_CONFIG_FOUND)
  pkg_check_modules(PC_VDPAU_NV_SUNXI vdpau_nv_sunxi QUIET)
endif()

#find_path(VDPAU_NV_SUNXI_INCLUDE_DIR ve.h
#                          PATHS ${PC_VDPAU_NV_SUNXI_INCLUDEDIR})

find_library(VDPAU_NV_SUNXI_LIBRARY NAMES VDPAU_NV_SUNXI vdpau_nv_sunxi
                         PATHS ${PC_VDPAU_NV_SUNXI_LIBDIR})

set(VDPAU_NV_SUNXI_VERSION ${PC_VDPAU_NV_SUNXI_VERSION})

include(FindPackageHandleStandardArgs)
find_package_handle_standard_args(VDPAU_NV_SUNXI
                                  REQUIRED_VARS VDPAU_NV_SUNXI_LIBRARY VDPAU_NV_SUNXI_INCLUDE_DIR
                                  VERSION_VAR VDPAU_NV_SUNXI_VERSION)

if(VDPAU_NV_SUNXI_FOUND)
  set(VDPAU_NV_SUNXI_LIBRARIES ${VDPAU_NV_SUNXI_LIBRARY})
#  set(VDPAU_NV_SUNXI_INCLUDE_DIRS ${VDPAU_NV_SUNXI_INCLUDE_DIR})
  set(VDPAU_NV_SUNXI_DEFINITIONS -DHAVE_LIBVDPAU_NV_SUNXI=1)

  if(NOT TARGET VDPAU_NV_SUNXI::VDPAU_NV_SUNXI)
    add_library(VDPAU_NV_SUNXI::VDPAU_NV_SUNXI UNKNOWN IMPORTED)
    set_target_properties(VDPAU_NV_SUNXI::VDPAU_NV_SUNXI PROPERTIES
                                   IMPORTED_LOCATION "${VDPAU_NV_SUNXI_LIBRARY}"
                                   INTERFACE_COMPILE_DEFINITIONS HAVE_LIBVDPAU_NV_SUNXI=1)

#                                   INTERFACE_INCLUDE_DIRECTORIES "${VDPAU_NV_SUNXI_INCLUDE_DIR}"

  endif()
endif()

mark_as_advanced(VDPAU_NV_SUNXI_INCLUDE_DIR VDPAU_NV_SUNXI_LIBRARY)
