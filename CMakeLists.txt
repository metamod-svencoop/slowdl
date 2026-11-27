cmake_minimum_required(VERSION 3.21)
option(FORMAT_VALIDATION_ONLY "Configure formatting without native dependencies" OFF)
if(FORMAT_VALIDATION_ONLY)
    project(SlowDLFormat LANGUAGES NONE)
    include(cmake/FormatValidation.cmake)
    return()
endif()
include(cmake/Metamod.cmake)
project(slowdl LANGUAGES C CXX)
if(NOT CMAKE_CONFIGURATION_TYPES AND NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Release CACHE STRING "Build configuration" FORCE)
endif()
include("${METAMOD_SOURCE_PATH}/cmake/SDK.cmake")
include(cmake/GoldSrcExt.cmake)
mmfg_configure_install_layout()

add_library(slowdl_hook_bridge STATIC
    src/bridge/slowdl_bridge.cpp
    src/bridge/slowdl_bridge_metamod.cpp
)
target_compile_features(slowdl_hook_bridge PUBLIC cxx_std_17)
set_target_properties(slowdl_hook_bridge PROPERTIES POSITION_INDEPENDENT_CODE ON)
target_include_directories(slowdl_hook_bridge PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/src")
target_link_libraries(slowdl_hook_bridge PUBLIC GoldSrcExt::Headers PRIVATE Metamod::SDK)
if(MSVC)
    target_compile_definitions(slowdl_hook_bridge PRIVATE WIN32 _CRT_SECURE_NO_WARNINGS
        $<$<CONFIG:Debug>:_DEBUG> $<$<NOT:$<CONFIG:Debug>>:NDEBUG>)
    target_compile_options(slowdl_hook_bridge PRIVATE /Zi)
elseif(CMAKE_SYSTEM_NAME STREQUAL "Linux")
    target_compile_definitions(slowdl_hook_bridge PRIVATE PLATFORM_POSIX LINUX _LINUX
        $<$<CONFIG:Debug>:_DEBUG> $<$<NOT:$<CONFIG:Debug>>:NDEBUG>)
    if(CMAKE_SIZEOF_VOID_P EQUAL 8)
        target_compile_options(slowdl_hook_bridge PRIVATE -m64 -fno-exceptions -fno-rtti -fvisibility=hidden
            -g $<$<NOT:$<CONFIG:Debug>>:-O2>)
    else()
        target_compile_options(slowdl_hook_bridge PRIVATE -m32 -fno-exceptions -fno-rtti -fvisibility=hidden
            -march=i686 -mtune=generic -msse -msse2 -g $<$<NOT:$<CONFIG:Debug>>:-O2>)
    endif()
endif()

add_library(slowdl SHARED
    src/dllapi.cpp
    src/engine_hook.cpp
    src/file_log.cpp
    src/h_export.cpp
    src/meta_api.cpp
    src/monitor.cpp
    src/slowdl.cpp
)
mmfg_configure_target(slowdl)
target_link_libraries(slowdl PRIVATE slowdl_hook_bridge Metamod::SDK)
if(WIN32)
    if(CMAKE_SIZEOF_VOID_P EQUAL 4)
        target_link_options(slowdl PRIVATE "/DEF:${CMAKE_CURRENT_SOURCE_DIR}/src/slowdl.def")
    endif()
else()
    if(CMAKE_SIZEOF_VOID_P EQUAL 4)
        target_compile_options(slowdl PRIVATE -mincoming-stack-boundary=2)
    endif()
endif()
mmfg_add_gamedata(slowdl scripts/manifests/slowdl.json)
# Keep Windows-only releases separate: the existing validator intentionally
# requires every declared platform in every selected snapshot.
add_custom_target(slowdl_windows)
mmfg_add_gamedata(slowdl_windows scripts/manifests/slowdl_windows.json)
add_dependencies(slowdl slowdl_windows)
mmfg_assign_source_groups(slowdl_hook_bridge)
mmfg_assign_source_groups(slowdl)
option(MMFG_SLOWDL_BUILD_TESTS "Build SlowDL behavior tests" ON)
if(MMFG_SLOWDL_BUILD_TESTS)
    enable_testing()
    add_executable(mmfg_test_slowdl tests/test_slowdl.cpp src/monitor.cpp src/file_log.cpp)
    target_include_directories(mmfg_test_slowdl PRIVATE src)
    target_compile_features(mmfg_test_slowdl PRIVATE cxx_std_17)
    add_test(NAME mmfg_test_slowdl COMMAND mmfg_test_slowdl)

    add_executable(mmfg_test_slowdl_bridge tests/test_slowdl_bridge.cpp)
    target_link_libraries(mmfg_test_slowdl_bridge PRIVATE slowdl_hook_bridge)
    target_compile_features(mmfg_test_slowdl_bridge PRIVATE cxx_std_17)
    if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
        target_compile_options(mmfg_test_slowdl_bridge PRIVATE -fno-exceptions -fno-rtti)
    endif()
    add_test(NAME mmfg_test_slowdl_bridge COMMAND mmfg_test_slowdl_bridge)
endif()
include(cmake/FormatValidation.cmake)
