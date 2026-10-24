# Locates libcmark-gfm and its extensions library.
# Sets cmark-gfm_FOUND plus imported targets cmark-gfm::cmark-gfm and cmark-gfm::extensions.

find_package(PkgConfig QUIET)
if(PkgConfig_FOUND)
  pkg_check_modules(PC_CMARK_GFM QUIET libcmark-gfm)
  pkg_check_modules(PC_CMARK_GFM_EXT QUIET libcmark-gfm-extensions)
endif()

set(_cmark_hints ${PC_CMARK_GFM_INCLUDE_DIRS} ${CMAKE_PREFIX_PATH} ENV HOMEBREW_PREFIX)

find_path(cmark-gfm_INCLUDE_DIR
  NAMES cmark-gfm.h cmark-gfm-extension_api.h
  HINTS ${_cmark_hints} ${PC_CMARK_GFM_LIBDIR}
  PATH_SUFFIXES cmark-gfm)

find_library(cmark-gfm_LIBRARY NAMES cmark-gfm libcmark-gfm
  HINTS ${PC_CMARK_GFM_LIBDIR} ${_cmark_hints})
find_library(cmark-gfm_EXTENSIONS_LIBRARY NAMES cmark-gfm-extensions libcmark-gfm-extensions
  HINTS ${PC_CMARK_GFM_EXT_LIBDIR} ${_cmark_hints})

include(FindPackageHandleStandardArgs)
find_package_handle_standard_args(cmark-gfm
  REQUIRED_VARS cmark-gfm_INCLUDE_DIR cmark-gfm_LIBRARY cmark-gfm_EXTENSIONS_LIBRARY)

if(cmark-gfm_FOUND AND NOT TARGET cmark-gfm::cmark-gfm)
  add_library(cmark-gfm::cmark-gfm UNKNOWN IMPORTED)
  set_target_properties(cmark-gfm::cmark-gfm PROPERTIES
    IMPORTED_LOCATION "${cmark-gfm_LIBRARY}"
    INTERFACE_INCLUDE_DIRECTORIES "${cmark-gfm_INCLUDE_DIR}")
  add_library(cmark-gfm::extensions UNKNOWN IMPORTED)
  set_target_properties(cmark-gfm::extensions PROPERTIES
    IMPORTED_LOCATION "${cmark-gfm_EXTENSIONS_LIBRARY}"
    INTERFACE_INCLUDE_DIRECTORIES "${cmark-gfm_INCLUDE_DIR}")
endif()

mark_as_advanced(cmark-gfm_INCLUDE_DIR cmark-gfm_LIBRARY cmark-gfm_EXTENSIONS_LIBRARY)
