include(FetchContent)
if(NOT TARGET GoldSrcExt::Headers)
    if(NOT DEFINED GOLDSRCEXT_SOURCE_PATH)
        set(GOLDSRCEXT_SOURCE_PATH "$ENV{GOLDSRCEXT_SOURCE_PATH}" CACHE PATH "Local GoldSrcExt headers")
    endif()
    set(GOLDSRCEXT_BUILD_TESTS OFF CACHE BOOL "Build GoldSrcExt standalone unit tests" FORCE)
    if(GOLDSRCEXT_SOURCE_PATH)
        get_filename_component(GOLDSRCEXT_SOURCE_PATH "${GOLDSRCEXT_SOURCE_PATH}" ABSOLUTE BASE_DIR "${CMAKE_CURRENT_SOURCE_DIR}")
        add_subdirectory("${GOLDSRCEXT_SOURCE_PATH}" "${CMAKE_BINARY_DIR}/vendor/goldsrcext" EXCLUDE_FROM_ALL)
    else()
        include("${CMAKE_CURRENT_LIST_DIR}/Revisions.cmake")
        FetchContent_Declare(mmfg_goldsrcext
            GIT_REPOSITORY https://github.com/HLND2T/GoldSrcExt.git
            GIT_TAG "${MMFG_GOLDSRCEXT_REVISION}"
            GIT_SUBMODULES "" GIT_SUBMODULES_RECURSE FALSE)
        FetchContent_MakeAvailable(mmfg_goldsrcext)
        set(GOLDSRCEXT_SOURCE_PATH "${mmfg_goldsrcext_SOURCE_DIR}")
    endif()
endif()
