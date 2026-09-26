cmake_minimum_required(VERSION 3.20)
project(th10_ios LANGUAGES C CXX OBJC OBJCXX)
if(NOT CMAKE_SYSTEM_NAME STREQUAL "iOS")
  message(FATAL_ERROR "Configure with the iOS toolchain; see ios/build_ios.sh")
endif()
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_OBJCXX_STANDARD 17)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
get_filename_component(TH10_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/.." ABSOLUTE)
set(TH10_DEPENDENCIES "${TH10_ROOT}/.local/dependencies" CACHE PATH "Pinned SDL source dependencies")
set(TH10_ASSETS "${TH10_ROOT}/.local/assets" CACHE PATH "Private prepared resources")
set(TH10_ICON_CATALOG "${TH10_ROOT}/.local/app-icon/Assets.xcassets" CACHE PATH "Private extracted game icon catalog")
set(TH10_BUILD_NUMBER "1" CACHE STRING "Monotonic build number")
set(TH10_BUNDLE_ID "org.th10.native" CACHE STRING "Application identifier")
option(TH10_DIAGNOSTICS "Enable scripted simulator smoke inputs" OFF)
set(BUILD_SHARED_LIBS OFF CACHE BOOL "" FORCE)
set(SDL_SHARED OFF CACHE BOOL "" FORCE)
set(SDL_STATIC ON CACHE BOOL "" FORCE)
set(SDL_TESTS OFF CACHE BOOL "" FORCE)
set(SDL_TEST_LIBRARY OFF CACHE BOOL "" FORCE)
set(SDL_EXAMPLES OFF CACHE BOOL "" FORCE)
set(SDL_GPU OFF CACHE BOOL "" FORCE)
set(SDL_METAL OFF CACHE BOOL "" FORCE)
set(SDL_VULKAN OFF CACHE BOOL "" FORCE)
set(SDL_RENDER ON CACHE BOOL "" FORCE)
set(SDL_OPENGLES ON CACHE BOOL "" FORCE)
set(SDL_HIDAPI OFF CACHE BOOL "" FORCE)
set(SDL_INSTALL OFF CACHE BOOL "" FORCE)
add_subdirectory("${TH10_DEPENDENCIES}/SDL" "${CMAKE_BINARY_DIR}/SDL")
set(SDLTTF_VENDORED ON CACHE BOOL "" FORCE)
set(SDLTTF_HARFBUZZ OFF CACHE BOOL "" FORCE)
set(SDLTTF_PLUTOSVG OFF CACHE BOOL "" FORCE)
set(SDLTTF_SAMPLES OFF CACHE BOOL "" FORCE)
set(SDLTTF_INSTALL OFF CACHE BOOL "" FORCE)
add_subdirectory("${TH10_DEPENDENCIES}/SDL_ttf" "${CMAKE_BINARY_DIR}/SDL_ttf")
file(GLOB GAME_SOURCES CONFIGURE_DEPENDS "${TH10_ROOT}/th10_web/cpp/game/*.cpp")
list(FILTER GAME_SOURCES EXCLUDE REGEX "/(LegacyBridge|LegacyCallbacks|Exports|Freestanding)\.cpp$")
file(GLOB PLATFORM_SOURCES CONFIGURE_DEPENDS "${TH10_ROOT}/th10_web/cpp/platform/*.cpp")
file(GLOB HOST_SOURCES CONFIGURE_DEPENDS "${TH10_ROOT}/th10_web/cpp/sdl/*.cpp")
add_executable(th10 MACOSX_BUNDLE main.mm Diagnostics.mm Preferences.mm ${GAME_SOURCES} ${PLATFORM_SOURCES} ${HOST_SOURCES}
  "${TH10_ROOT}/portable/sdl/Renderer.cpp"
  "${TH10_ROOT}/th10_web/cpp/rebuild/third_party/softfloat.c")
if(NOT EXISTS "${TH10_ICON_CATALOG}/AppIcon.appiconset/Contents.json")
  message(FATAL_ERROR "Prepare the private original game icon first; see ios/ICON.md")
endif()
target_sources(th10 PRIVATE "${TH10_ICON_CATALOG}")
set_source_files_properties("${TH10_ICON_CATALOG}" PROPERTIES MACOSX_PACKAGE_LOCATION Resources)
target_include_directories(th10 PRIVATE "${TH10_ROOT}/portable/sdl" "${TH10_ROOT}/th10_web/cpp/sdl")
target_compile_definitions(th10 PRIVATE TH_SDL3=1 TH_NATIVE_PLATFORM=1 TH10_IOS=1 SOFTFLOAT_FAST_INT64 INLINE_LEVEL=5)
target_compile_definitions(th10 PRIVATE TH10_DIAGNOSTICS=$<BOOL:${TH10_DIAGNOSTICS}>)
target_compile_options(th10 PRIVATE -fno-strict-aliasing -fno-fast-math -ffp-contract=off -fsigned-char
  -Wno-unknown-attributes -Wno-deprecated-declarations
  $<$<COMPILE_LANGUAGE:CXX>:-fno-exceptions;-fno-rtti>)
set_source_files_properties(main.mm Diagnostics.mm Preferences.mm PROPERTIES COMPILE_FLAGS "-fobjc-arc")
target_link_libraries(th10 PRIVATE SDL3::SDL3 SDL3_ttf::SDL3_ttf
  "-framework UIKit" "-framework Foundation" "-framework AVFoundation"
  "-framework QuartzCore" "-framework OpenGLES")
target_link_options(th10 PRIVATE "LINKER:-dead_strip")
configure_file(Info.plist.in "${CMAKE_BINARY_DIR}/TH10-Info.plist" @ONLY)
set_target_properties(th10 PROPERTIES
  MACOSX_BUNDLE_INFO_PLIST "${CMAKE_BINARY_DIR}/TH10-Info.plist"
  XCODE_ATTRIBUTE_PRODUCT_BUNDLE_IDENTIFIER "${TH10_BUNDLE_ID}"
  XCODE_ATTRIBUTE_TARGETED_DEVICE_FAMILY "1,2"
  XCODE_ATTRIBUTE_ASSETCATALOG_COMPILER_APPICON_NAME "AppIcon"
  XCODE_ATTRIBUTE_IPHONEOS_DEPLOYMENT_TARGET "14.0"
  XCODE_ATTRIBUTE_CODE_SIGNING_ALLOWED "NO"
  XCODE_ATTRIBUTE_ENABLE_BITCODE "NO"
  XCODE_ATTRIBUTE_DEBUG_INFORMATION_FORMAT "dwarf-with-dsym")
add_custom_command(TARGET th10 POST_BUILD
  COMMAND ${CMAKE_COMMAND} -E copy_directory "${CMAKE_CURRENT_SOURCE_DIR}/licenses" "$<TARGET_BUNDLE_DIR:th10>/licenses"
  COMMAND ${CMAKE_COMMAND} -E copy "${TH10_ROOT}/portable/sdl/third_party/miniaudio.LICENSE" "$<TARGET_BUNDLE_DIR:th10>/licenses/miniaudio.txt"
  COMMAND ${CMAKE_COMMAND} -E copy "${TH10_ROOT}/th10_web/assets/vendor/th10-rebuilt.LICENSE" "$<TARGET_BUNDLE_DIR:th10>/licenses/TH10-SoftFloat.txt")
if(EXISTS "${TH10_ASSETS}/game/th10c.dat")
  add_custom_command(TARGET th10 POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_directory "${TH10_ASSETS}" "$<TARGET_BUNDLE_DIR:th10>/assets")
else()
  message(FATAL_ERROR "Prepare private game resources first; see ios/ASSETS.md")
endif()
