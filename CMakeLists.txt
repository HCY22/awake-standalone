cmake_minimum_required(VERSION 3.20)
project(Awake VERSION 1.0.0 LANGUAGES CXX RC)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
  set(CMAKE_BUILD_TYPE MinSizeRel)
endif()

add_executable(Awake WIN32
  src/main.cpp
  src/settings.cpp
  src/log.cpp
  src/strings.cpp
  src/awake.rc
)

target_include_directories(Awake PRIVATE src assets)
target_compile_definitions(Awake PRIVATE UNICODE _UNICODE WIN32_LEAN_AND_MEAN NOMINMAX _WIN32_WINNT=0x0A00 WINVER=0x0A00)
target_link_libraries(Awake PRIVATE comctl32 shell32 user32 wtsapi32 advapi32 ole32 uuid)

if(MSVC)
  # Static CRT: a single Awake.exe with no runtime to install.
  set_property(TARGET Awake PROPERTY MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
  target_compile_options(Awake PRIVATE /utf-8 /W4 /permissive- /GS /Gy /Gw)
  # The manifest is embedded from awake.rc.
  target_link_options(Awake PRIVATE /MANIFEST:NO /OPT:REF /OPT:ICF)
else()
  target_compile_options(Awake PRIVATE -municode -Wall -Wextra)
  target_link_options(Awake PRIVATE -municode -static -s)
endif()

option(AWAKE_BUILD_TESTS "Build unit tests" OFF)
if(AWAKE_BUILD_TESTS)
  add_executable(awake_tests tests/unit_tests.cpp src/settings.cpp src/log.cpp src/strings.cpp)
  target_include_directories(awake_tests PRIVATE src)
  target_compile_definitions(awake_tests PRIVATE UNICODE _UNICODE WIN32_LEAN_AND_MEAN NOMINMAX _WIN32_WINNT=0x0A00 WINVER=0x0A00)
  target_link_libraries(awake_tests PRIVATE shell32 ole32 uuid)
  if(MSVC)
    set_property(TARGET awake_tests PROPERTY MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
    target_compile_options(awake_tests PRIVATE /utf-8)
  else()
    target_compile_options(awake_tests PRIVATE -municode)
    target_link_options(awake_tests PRIVATE -municode -static)
  endif()
endif()
