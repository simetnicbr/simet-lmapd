# So cmake can tell whether or not it can process this file
cmake_minimum_required(VERSION 3.0.0 FATAL_ERROR)

project(simet-lmapd VERSION 0.15.6 LANGUAGES C)

option(BUILD_SHARED_LIBS "Build the shared library" OFF)
option(BUILD_TESTS "Build test programs (requires JSON and XML support)" ON)
option(BUILD_JSON  "Build with JSON support (requires json-c)" ON)
option(BUILD_XML   "Build with XML support (requires libxml2)" ON)

# Get some extra flexibility so that our defaults are less awkward
include(GNUInstallDirs)

# Get better libc where available
add_definitions(-D_GNU_SOURCE)

# platform-dependent functionality
include(CheckSymbolExists)
set(CMAKE_REQUIRED_DEFINITIONS "-D_GNU_SOURCE")
check_symbol_exists("closefrom" unistd.h HAVE_CLOSEFROM)
if(${HAVE_CLOSEFROM})
	add_definitions(-DHAVE_CLOSEFROM)
endif()
check_symbol_exists("close_range" unistd.h HAVE_CLOSE_RANGE)
if(${HAVE_CLOSE_RANGE})
	add_definitions(-DHAVE_CLOSE_RANGE)
endif()
check_symbol_exists("timegm" time.h HAVE_TIMEGM)
if(${HAVE_TIMEGM})
	add_definitions(-DHAVE_TIMEGM)
endif()

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
