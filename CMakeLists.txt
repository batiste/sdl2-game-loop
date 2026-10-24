cmake_minimum_required(VERSION 3.24)
project(sdl-game-loop LANGUAGES C)

set(CMAKE_C_STANDARD 17)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
  set(CMAKE_BUILD_TYPE Debug CACHE STRING "Build type" FORCE)
endif()

option(ENABLE_SANITIZERS "Build with AddressSanitizer and UBSan" OFF)

find_package(SDL3 REQUIRED CONFIG)
find_package(SDL3_image REQUIRED CONFIG)
find_package(SDL3_ttf REQUIRED CONFIG)
find_package(SDL3_mixer REQUIRED CONFIG)
find_package(PkgConfig REQUIRED)
pkg_check_modules(MXML REQUIRED IMPORTED_TARGET mxml4)

add_executable(game
  src/main.c
  src/engine.c
  src/sprite.c
  src/tmx.c
)

target_link_libraries(game PRIVATE
  SDL3::SDL3
  SDL3_image::SDL3_image
  SDL3_ttf::SDL3_ttf
  SDL3_mixer::SDL3_mixer
  PkgConfig::MXML
)

if(CMAKE_C_COMPILER_ID MATCHES "Clang|GNU")
  target_compile_options(game PRIVATE -Wall -Wextra -Wpedantic -Wshadow -Wconversion -Wno-sign-conversion)
  if(ENABLE_SANITIZERS)
    target_compile_options(game PRIVATE -fsanitize=address,undefined -fno-omit-frame-pointer)
    target_link_options(game PRIVATE -fsanitize=address,undefined)
  endif()
elseif(MSVC)
  target_compile_options(game PRIVATE /W4)
endif()

# Assets are looked up next to the executable (SDL_GetBasePath()).
add_custom_command(TARGET game POST_BUILD
  COMMAND ${CMAKE_COMMAND} -E copy_directory_if_different
          ${CMAKE_SOURCE_DIR}/assets $<TARGET_FILE_DIR:game>/assets
  COMMENT "Copying assets"
)
