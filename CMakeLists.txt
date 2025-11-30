# =====================================================================
#               DMOD Virtual File System
# =====================================================================
cmake_minimum_required(VERSION 3.10)

# ======================================================================
#               DMOD VFS
# ======================================================================
project(dmvfs 
    VERSION 1.0
    DESCRIPTION "DMOD Virtual File System"
    LANGUAGES C CXX)


# ======================================================================
#               Fetch DMOD repository
# ======================================================================
include(FetchContent)

# Only fetch and build dmod if it's not already available as a target
if(NOT TARGET dmod_inc)
    FetchContent_Declare(
        dmod
        GIT_REPOSITORY https://github.com/choco-technologies/dmod.git
        GIT_TAG        develop
    )

    # ======================================================================
    #               DMOD Configuration
    # ======================================================================
    set(DMOD_MODE "DMOD_SYSTEM" CACHE STRING "DMOD build mode")
    set(DMOD_BUILD_TESTS OFF CACHE BOOL "Build tests")
    set(DMOD_BUILD_EXAMPLES OFF CACHE BOOL "Build examples")
    set(DMOD_BUILD_TOOLS OFF CACHE BOOL "Build tools")
    set(DMOD_BUILD_TEMPLATES OFF CACHE BOOL "Build templates")
    set(DMOD_EXTERNAL_REGISTRATION ON CACHE BOOL "Use external registration")
    
    # Pass coverage flags to DMOD if enabled
    if(ENABLE_COVERAGE)
        set(DMOD_ENABLE_COVERAGE ON CACHE BOOL "Enable DMOD coverage")
    endif()

    FetchContent_MakeAvailable(dmod)
    set(DMOD_DIR ${dmod_SOURCE_DIR} CACHE PATH "DMOD source directory" FORCE)
else()
    message(STATUS "dmod target already exists, skipping FetchContent")
endif()

# ======================================================================
#               Fetch DMOD File System Interface
# ======================================================================
FetchContent_Declare(
    dmfsi
    GIT_REPOSITORY
    https://github.com/choco-technologies/dmfsi.git
    GIT_TAG        master
)

# ======================================================================
#               DMOD File System Interface Configuration
# ======================================================================
set(DMOD_DIR ${dmod_SOURCE_DIR})
FetchContent_MakeAvailable(dmfsi)

# ======================================================================
#               DMOD VFS Library definition
# ======================================================================
# Option to include DMOD SAL implementations in the library
option(DMVFS_INCLUDE_DMOD_SAL "Include DMOD SAL file operation implementations" ON)

set(DMVFS_SOURCES
    src/dmvfs.c
)

if(DMVFS_INCLUDE_DMOD_SAL)
    list(APPEND DMVFS_SOURCES src/dmod_sal.c)
endif()

add_library(dmvfs STATIC ${DMVFS_SOURCES})

target_include_directories(dmvfs PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/inc>
)

target_link_libraries(dmvfs PUBLIC
    dmod
    dmfsi
)

# Define version string for the library
target_compile_definitions(dmvfs PRIVATE
    DMVFS_VERSION="${PROJECT_VERSION}"
)

# ======================================================================
#               Tests
# ======================================================================
option(DMVFS_BUILD_TESTS "Build tests" OFF)

if(DMVFS_BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()