#----------------------------------------------------------------
# Generated CMake target import file.
#----------------------------------------------------------------

# Commands may need to know the format version.
set(CMAKE_IMPORT_FILE_VERSION 1)

# Import target "yyjson::yyjson" for configuration ""
set_property(TARGET yyjson::yyjson APPEND PROPERTY IMPORTED_CONFIGURATIONS NOCONFIG)
set_target_properties(yyjson::yyjson PROPERTIES
  IMPORTED_LINK_INTERFACE_LANGUAGES_NOCONFIG "C"
  IMPORTED_LOCATION_NOCONFIG "${_IMPORT_PREFIX}/lib/libyyjson.a"
  )

list(APPEND _cmake_import_check_targets yyjson::yyjson )
list(APPEND _cmake_import_check_files_for_yyjson::yyjson "${_IMPORT_PREFIX}/lib/libyyjson.a" )

# Commands beyond this point should not need to know the version.
set(CMAKE_IMPORT_FILE_VERSION)
