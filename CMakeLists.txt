cmake_minimum_required(VERSION 3.10.0)

set(CMAKE_C_STANDARD 11)
project("EmulatorFrontend" LANGUAGES C)

add_subdirectory("external")

file(GLOB_RECURSE SOURCES "src/*.c")
list(FILTER SOURCES EXCLUDE REGEX "\\.ignore")
add_executable(${PROJECT_NAME} ${SOURCES})

target_include_directories(
    ${PROJECT_NAME} PRIVATE
    "src"
)

target_link_libraries(
    ${PROJECT_NAME} PRIVATE
    SDL3::SDL3 klib swanstation_libretro libretro
)

set_target_properties(
    ${PROJECT_NAME} PROPERTIES
    OUTPUT_NAME "Emulator"
    WIN32_EXECUTABLE "$<$<CONFIG:Release>:TRUE>"
)

target_compile_options(
    ${PROJECT_NAME} PRIVATE
    /wd4244
)

install(TARGETS ${PROJECT_NAME} swanstation_libretro SDL3-shared RUNTIME DESTINATION "${CMAKE_INSTALL_PREFIX}/$<CONFIGURATION>")
install(DIRECTORY "data" DESTINATION "${CMAKE_INSTALL_PREFIX}/$<CONFIGURATION>")
