cmake_minimum_required(VERSION 3.20)
project(stellar_tool_gameplay LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

set(CPS_SDK_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../CPS-SDK" CACHE PATH "Path to the CPS SDK")
if(NOT EXISTS "${CPS_SDK_DIR}/CMakeLists.txt")
  message(FATAL_ERROR "CPS SDK not found at ${CPS_SDK_DIR}; set CPS_SDK_DIR to the SDK folder")
endif()

add_subdirectory("${CPS_SDK_DIR}" cps_sdk EXCLUDE_FROM_ALL)
cps_add_plugin(stellar_tool_gameplay
  MANIFEST plugin.toml
  SOURCES src/plugin.cpp
  ASSETS "${CPS_SDK_DIR}/LICENSE" "LICENSES/CPS-SDK-MIT.txt"
         "${CPS_SDK_DIR}/THIRD_PARTY_NOTICES.md" "LICENSES/THIRD_PARTY_NOTICES.md"
  LOCALE locale)
target_link_libraries(stellar_tool_gameplay PRIVATE cps_sdk_ue)
if(MSVC)
  target_compile_options(stellar_tool_gameplay PRIVATE /EHa)
endif()
