find_path(LIBEV_INCLUDE_DIR NAMES ev.h)
find_library(LIBEV_LIBRARY NAMES ev)

include(FindPackageHandleStandardArgs)
find_package_handle_standard_args(
  Libev
  REQUIRED_VARS LIBEV_LIBRARY LIBEV_INCLUDE_DIR)

if(LIBEV_FOUND)
  add_library(Libev::Libev SHARED IMPORTED)
  set_target_properties(Libev::Libev
    PROPERTIES
    INTERFACE_INCLUDE_DIRECTORIES ${LIBEV_INCLUDE_DIR}
    IMPORTED_LOCATION             ${LIBEV_LIBRARY}
  )
endif()
