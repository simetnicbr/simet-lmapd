# So cmake can tell whether or not it can process this file
cmake_minimum_required(VERSION 3.0.0 FATAL_ERROR)

project(simet-lmapd VERSION 0.15.3 LANGUAGES C)

option(BUILD_SHARED_LIBS "Build the shared library" OFF)
option(BUILD_TESTS "Build test programs (requires JSON and XML support)" ON)
option(BUILD_JSON  "Build with JSON support (requires json-c)" ON)
option(BUILD_XML   "Build with XML support (requires libxml2)" ON)

# Get some extra flexibility so that our defaults are less awkward
include(GNUInstallDirs)

# experimental code coverage stuff...
#set(CMAKE_CXX_FLAGS "-g -O0 -Wall -fprofile-arcs -ftest-coverage")
#set(CMAKE_C_FLAGS "-g -O0 -Wall -W -fprofile-arcs -ftest-coverage")
#set(CMAKE_EXE_LINKER_FLAGS "-fprofile-arcs -ftest-coverage")

find_package(Threads)
find_package(PkgConfig)
pkg_check_modules(LIBEVENT REQUIRED libevent)

if(BUILD_XML)
    pkg_check_modules(LIBXML2 REQUIRED libxml-2.0)
    add_definitions(-DWITH_XML)
endif(BUILD_XML)
if(BUILD_JSON)
    pkg_check_modules(LIBJSONC REQUIRED json-c)
    add_definitions(-DWITH_JSON)
endif(BUILD_JSON)

if(CMAKE_COMPILER_IS_GNUCC)
    add_definitions(-Wall)
endif(CMAKE_COMPILER_IS_GNUCC)

add_subdirectory(doc)
add_subdirectory(src)

# testing related definitions
if(BUILD_TESTS AND BUILD_XML AND BUILD_JSON)
    add_subdirectory(test)
    enable_testing()
    add_test(NAME lmap COMMAND check-lmap WORKING_DIRECTORY ${CMAKE_SOURCE_DIR})
    add_test(NAME lmapd COMMAND check-lmapd WORKING_DIRECTORY ${CMAKE_SOURCE_DIR})
endif(BUILD_TESTS AND BUILD_XML AND BUILD_JSON)

# source packages 'make package_source' or 'make dist'
set(CPACK_PACKAGE_VERSION_MAJOR ${PROJECT_VERSION_MAJOR})
set(CPACK_PACKAGE_VERSION_MINOR ${PROJECT_VERSION_MINOR})
set(CPACK_PACKAGE_VERSION_PATCH ${PROJECT_VERSION_PATCH})
set(CPACK_SOURCE_GENERATOR "TGZ")
set(CPACK_SOURCE_PACKAGE_FILE_NAME
  "${CMAKE_PROJECT_NAME}-${CPACK_PACKAGE_VERSION_MAJOR}.${CPACK_PACKAGE_VERSION_MINOR}.${CPACK_PACKAGE_VERSION_PATCH}")
set(CPACK_SOURCE_IGNORE_FILES
  "/build/;/.git/;~$;${CPACK_SOURCE_IGNORE_FILES}")
include(CPack)
add_custom_target(dist COMMAND ${CMAKE_MAKE_PROGRAM} package_source)
