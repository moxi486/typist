# Locates libcmark-gfm and its extensions library.
# Sets CmarkGfm_FOUND plus imported targets cmark-gfm::cmark-gfm and cmark-gfm::extensions.

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

find_library(cmark-gfm_LIBRARY NAMES cmark-gfm libcmark-gfm cmark-gfm_static libcmark-gfm_static
  HINTS ${PC_CMARK_GFM_LIBDIR} ${_cmark_hints})
find_library(cmark-gfm_EXTENSIONS_LIBRARY NAMES cmark-gfm-extensions libcmark-gfm-extensions cmark-gfm-extensions_static libcmark-gfm-extensions_static
  HINTS ${PC_CMARK_GFM_EXT_LIBDIR} ${_cmark_hints})

include(FindPackageHandleStandardArgs)
find_package_handle_standard_args(CmarkGfm
  REQUIRED_VARS cmark-gfm_INCLUDE_DIR cmark-gfm_LIBRARY cmark-gfm_EXTENSIONS_LIBRARY)

if(CmarkGfm_FOUND AND NOT TARGET cmark-gfm::cmark-gfm)
  add_library(cmark-gfm::cmark-gfm UNKNOWN IMPORTED)
  set_target_properties(cmark-gfm::cmark-gfm PROPERTIES
    IMPORTED_LOCATION "${cmark-gfm_LIBRARY}"
    INTERFACE_INCLUDE_DIRECTORIES "${cmark-gfm_INCLUDE_DIR}")
  add_library(cmark-gfm::extensions UNKNOWN IMPORTED)
  set_target_properties(cmark-gfm::extensions PROPERTIES
    IMPORTED_LOCATION "${cmark-gfm_EXTENSIONS_LIBRARY}"
    INTERFACE_INCLUDE_DIRECTORIES "${cmark-gfm_INCLUDE_DIR}"
    INTERFACE_LINK_LIBRARIES cmark-gfm::cmark-gfm)
endif()

if(WIN32 AND cmark-gfm_LIBRARY)
  # Import libraries live in lib/ while the DLLs live in bin/, so tests need that
  # directory on PATH.
  get_filename_component(_cmark_lib_dir "${cmark-gfm_LIBRARY}" DIRECTORY)
  set(cmark-gfm_BIN_DIR "${_cmark_lib_dir}/../bin" CACHE INTERNAL "cmark-gfm runtime directory")
endif()

mark_as_advanced(cmark-gfm_INCLUDE_DIR cmark-gfm_LIBRARY cmark-gfm_EXTENSIONS_LIBRARY)
