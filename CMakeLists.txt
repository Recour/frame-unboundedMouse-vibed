# SPDX-License-Identifier: MIT
cmake_minimum_required(VERSION 3.20)
project(mouselaser CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON) # lets clangd find openvr_driver.h via build/compile_commands.json
if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
  set(CMAKE_BUILD_TYPE RelWithDebInfo)
endif()

if(WIN32)
  set(MOUSELASER_PLATFORM_SOURCES src/mouse_reader_win.cpp)
  set(MOUSELASER_BIN_DIR win64)
  # SteamVR on Windows doesn't ship the driver header, so fetch the single file from the
  # public OpenVR SDK (BSD-3-Clause) at a pinned tag. Override with -DOPENVR_HEADERS=<dir>.
  set(OPENVR_SDK_TAG v2.5.1 CACHE STRING "OpenVR SDK tag to take openvr_driver.h from")
  set(OPENVR_HEADERS "" CACHE PATH "Directory containing openvr_driver.h (empty = download)")
  if(NOT OPENVR_HEADERS)
    set(OPENVR_HEADERS ${CMAKE_BINARY_DIR}/openvr-headers)
    if(NOT EXISTS ${OPENVR_HEADERS}/openvr_driver.h)
      file(DOWNLOAD
        https://raw.githubusercontent.com/ValveSoftware/openvr/${OPENVR_SDK_TAG}/headers/openvr_driver.h
        ${OPENVR_HEADERS}/openvr_driver.h
        STATUS openvr_dl_status)
      list(GET openvr_dl_status 0 openvr_dl_code)
      if(NOT openvr_dl_code EQUAL 0)
        file(REMOVE ${OPENVR_HEADERS}/openvr_driver.h)
        message(FATAL_ERROR "Could not download openvr_driver.h (${openvr_dl_status}). Pass -DOPENVR_HEADERS=<dir>.")
      endif()
    endif()
  endif()
else()
  set(MOUSELASER_PLATFORM_SOURCES src/mouse_reader_linux.cpp)
  set(MOUSELASER_BIN_DIR linuxarm64)
  # Header-only OpenVR driver API, as shipped with SteamVR's hellovr sample.
  set(OPENVR_HEADERS /opt/steamvr/tools/hellovr_vulkan_linux/src/openvr/headers
      CACHE PATH "Directory containing openvr_driver.h")
endif()

set(MOUSELASER_COMMON_SOURCES src/mouse_reader.cpp ${MOUSELASER_PLATFORM_SOURCES})

add_library(driver_mouselaser SHARED src/driver.cpp ${MOUSELASER_COMMON_SOURCES})
target_include_directories(driver_mouselaser PRIVATE ${OPENVR_HEADERS})
if(MSVC)
  target_compile_options(driver_mouselaser PRIVATE /W3 /utf-8)
  # Static CRT, so the DLL doesn't need the Visual C++ Redistributable installed.
  set_property(TARGET driver_mouselaser PROPERTY MSVC_RUNTIME_LIBRARY "MultiThreaded")
else()
  target_compile_options(driver_mouselaser PRIVATE -Wall -Wextra -fvisibility=hidden)
endif()
if(NOT WIN32)
  target_link_libraries(driver_mouselaser PRIVATE pthread)
endif()
set(MOUSELASER_OUT ${CMAKE_SOURCE_DIR}/driver/mouselaser/bin/${MOUSELASER_BIN_DIR})
set_target_properties(driver_mouselaser PROPERTIES
  PREFIX ""
  LIBRARY_OUTPUT_DIRECTORY ${MOUSELASER_OUT}
  RUNTIME_OUTPUT_DIRECTORY ${MOUSELASER_OUT})
# Multi-config generators (Visual Studio) would otherwise add a Debug/Release subfolder.
foreach(cfg DEBUG RELEASE RELWITHDEBINFO MINSIZEREL)
  set_target_properties(driver_mouselaser PROPERTIES
    LIBRARY_OUTPUT_DIRECTORY_${cfg} ${MOUSELASER_OUT}
    RUNTIME_OUTPUT_DIRECTORY_${cfg} ${MOUSELASER_OUT})
endforeach()

# Optional offline check of the wheel logic: cmake -DMOUSELASER_TESTS=ON ... && ./build/wheel_sim
option(MOUSELASER_TESTS "Build the offline wheel simulation" OFF)
if(MOUSELASER_TESTS)
  add_executable(wheel_sim tests/wheel_sim.cpp ${MOUSELASER_COMMON_SOURCES})
  target_include_directories(wheel_sim PRIVATE ${OPENVR_HEADERS} src)
  if(NOT WIN32)
    target_link_libraries(wheel_sim PRIVATE pthread)
  endif()
endif()
