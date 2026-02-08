cmake_minimum_required(VERSION 3.10)

# Set the project name
project(fs_tester VERSION 1.0 DESCRIPTION "DMOD File System tester application" LANGUAGES C)

# Specify the C++ standard
set(CMAKE_CXX_STANDARD 11)
set(CMAKE_CXX_STANDARD_REQUIRED True)

# ======================================================================
#               Fetch DMLIST Library
# ======================================================================
include(FetchContent)

# Only fetch dmlist if it's not already available as a target
if(NOT TARGET dmlist)
    FetchContent_Declare(
        dmlist
        GIT_REPOSITORY https://github.com/choco-technologies/dmlist.git
        GIT_TAG        master
    )
    
    # Configure dmlist options
    set(DMLIST_BUILD_TESTS OFF CACHE BOOL "Build dmlist tests")
    
    FetchContent_MakeAvailable(dmlist)
endif()

# Add the executable
add_executable(${PROJECT_NAME} main.c)

# Include directories
include_directories(${PROJECT_SOURCE_DIR})

# Link libraries (if any)
target_link_libraries(${PROJECT_NAME} dmod dmvfs dmlist)

target_link_options(${PROJECT_NAME} PRIVATE -L ${DMOD_DIR}/scripts)
target_link_options(${PROJECT_NAME} PRIVATE -T ${CMAKE_CURRENT_SOURCE_DIR}/main.ld)

# Add mount point selection test
add_executable(mount_point_test test_mount_point_selection.c)
target_link_libraries(mount_point_test dmod dmvfs)
target_link_options(mount_point_test PRIVATE -L ${DMOD_DIR}/scripts)
target_link_options(mount_point_test PRIVATE -T ${CMAKE_CURRENT_SOURCE_DIR}/main.ld)