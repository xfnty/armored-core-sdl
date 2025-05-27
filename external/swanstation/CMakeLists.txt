add_library(swanstation SHARED IMPORTED GLOBAL)
set_target_properties(
    swanstation PROPERTIES
    IMPORTED_IMPLIB "${CMAKE_CURRENT_SOURCE_DIR}/lib/swanstation_libretro.lib"
    IMPORTED_LOCATION "${CMAKE_CURRENT_SOURCE_DIR}/bin/swanstation_libretro.dll"
    INTERFACE_INCLUDE_DIRECTORIES "${CMAKE_CURRENT_SOURCE_DIR}/include"
)
install(
    IMPORTED_RUNTIME_ARTIFACTS swanstation
    RUNTIME DESTINATION "${CMAKE_INSTALL_PREFIX}/$<CONFIGURATION>"
)
