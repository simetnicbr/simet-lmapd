# So CMake can tell whether or not it can process this file
cmake_minimum_required(VERSION 3.0 FATAL_ERROR)

include(GNUInstallDirs)

set(LMAPD_CONFIG_DIR "${CMAKE_INSTALL_FULL_SYSCONFDIR}/lmapd/config" CACHE PATH "Default config directory/files colon-separated list")
set(LMAPD_CAPABILITY_DIR "${CMAKE_INSTALL_FULL_SYSCONFDIR}/lmapd/capabilities" CACHE PATH "Default capabilities directory")
set(LMAPD_QUEUE_DIR  "${CMAKE_INSTALL_FULL_LOCALSTATEDIR}/spool/lmapd/queue" CACHE PATH "Default queue directory")
set(LMAPD_RUN_DIR    "${CMAKE_INSTALL_FULL_RUNSTATEDIR}" CACHE PATH "Default pidfile directory")

configure_file(
  "${PROJECT_SOURCE_DIR}/src/lmap.h.in"
  "${PROJECT_BINARY_DIR}/src/lmap.h"
)
configure_file(
  "${PROJECT_SOURCE_DIR}/src/lmapd.h.in"
  "${PROJECT_BINARY_DIR}/src/lmapd.h"
)

include_directories(${PROJECT_BINARY_DIR}/src
	${LIBEVENT_INCLUDE_DIRS}
	${LIBXML2_INCLUDE_DIRS}
	${LIBJSONC_INCLUDE_DIRS})
	
link_directories(${LIBEVENT_LIBRARY_DIRS}
	${LIBXML2_LIBRARY_DIRS}
	${LIBJSONC_LIBRARY_DIRS})

add_library(lmap data.c pidfile.c utils.c workspace.c runner.c signals.c csv.c lmap-io.c xml-io.c json-io.c)

add_executable(lmapd lmapd.c)
target_link_libraries(lmapd
	lmap
	${LIBEVENT_LIBRARIES}
	${LIBXML2_LIBRARIES}
	${LIBJSONC_LIBRARIES})

add_executable(lmapctl lmapctl.c)
target_link_libraries(lmapctl
	lmap
	${LIBEVENT_LIBRARIES}
	${LIBXML2_LIBRARIES}
	${LIBJSONC_LIBRARIES})

if(BUILD_SHARED_LIBS)
	install(TARGETS lmap LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR})
endif(BUILD_SHARED_LIBS)
install(TARGETS lmapd DESTINATION ${CMAKE_INSTALL_BINDIR})
install(TARGETS lmapctl DESTINATION ${CMAKE_INSTALL_BINDIR})
