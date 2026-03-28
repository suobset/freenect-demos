cmake_minimum_required(VERSION 3.16)
project(kinect_blog_demos LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

find_package(Threads REQUIRED)
find_package(PkgConfig QUIET)

set(FREENECT_FOUND FALSE)
set(LIBUSB_FOUND FALSE)

if(PkgConfig_FOUND)
  pkg_check_modules(FREENECT QUIET libfreenect)
  pkg_check_modules(LIBUSB QUIET libusb-1.0)
endif()

set(FREENECT_INCLUDE_HINTS
  /opt/homebrew/include
  /usr/local/include
  /opt/homebrew/include/libfreenect
  /usr/local/include/libfreenect
  /opt/homebrew/opt/libfreenect/include
  /usr/local/opt/libfreenect/include
  /opt/homebrew/opt/libfreenect/include/libfreenect
  /usr/local/opt/libfreenect/include/libfreenect
)
set(FREENECT_LIBRARY_HINTS
  /opt/homebrew/lib
  /usr/local/lib
  /opt/homebrew/opt/libfreenect/lib
  /usr/local/opt/libfreenect/lib
)
set(LIBUSB_INCLUDE_HINTS
  /opt/homebrew/include/libusb-1.0
  /usr/local/include/libusb-1.0
  /opt/homebrew/opt/libusb/include/libusb-1.0
  /usr/local/opt/libusb/include/libusb-1.0
)

if(FREENECT_FOUND)
  set(FREENECT_INCLUDE_PATHS ${FREENECT_INCLUDE_DIRS})
  set(FREENECT_LINK_LIBS ${FREENECT_LIBRARIES})
  set(FREENECT_LINK_DIRS ${FREENECT_LIBRARY_DIRS})
else()
  find_path(FREENECT_INCLUDE_DIR libfreenect.h HINTS ${FREENECT_INCLUDE_HINTS})
  find_library(FREENECT_LIBRARY NAMES freenect HINTS ${FREENECT_LIBRARY_HINTS})
  if(NOT FREENECT_INCLUDE_DIR OR NOT FREENECT_LIBRARY)
    message(FATAL_ERROR "Could not find libfreenect. Install it with Homebrew or expose it via pkg-config.")
  endif()
  set(FREENECT_INCLUDE_PATHS ${FREENECT_INCLUDE_DIR})
  set(FREENECT_LINK_LIBS ${FREENECT_LIBRARY})
  set(FREENECT_LINK_DIRS "")
endif()

if(LIBUSB_FOUND)
  set(LIBUSB_INCLUDE_PATHS ${LIBUSB_INCLUDE_DIRS})
else()
  find_path(LIBUSB_INCLUDE_DIR libusb.h HINTS ${LIBUSB_INCLUDE_HINTS})
  set(LIBUSB_INCLUDE_PATHS ${LIBUSB_INCLUDE_DIR})
endif()

add_library(blog_demo_common INTERFACE)
target_include_directories(blog_demo_common INTERFACE
  ${CMAKE_CURRENT_LIST_DIR}
  ${CMAKE_CURRENT_LIST_DIR}/..
  ${FREENECT_INCLUDE_PATHS}
  ${LIBUSB_INCLUDE_PATHS}
)
if(FREENECT_LINK_DIRS)
  target_link_directories(blog_demo_common INTERFACE ${FREENECT_LINK_DIRS})
endif()
target_link_libraries(blog_demo_common INTERFACE ${FREENECT_LINK_LIBS} Threads::Threads)

add_executable(kinect_depth_ascii kinect_depth_ascii.cpp)
target_link_libraries(kinect_depth_ascii PRIVATE blog_demo_common)

add_executable(kinect_pointcloud_snapshot kinect_pointcloud_snapshot.cpp)
target_link_libraries(kinect_pointcloud_snapshot PRIVATE blog_demo_common)

find_package(OpenGL QUIET)
find_package(GLUT QUIET)

if(APPLE)
  add_executable(kinect_cursor kinect_cursor.cpp)
  target_link_libraries(kinect_cursor PRIVATE blog_demo_common "-framework ApplicationServices")
endif()

if(APPLE AND OpenGL_FOUND AND GLUT_FOUND)
  add_executable(kinect_cursor_debug kinect_cursor_debug.cpp)
  target_link_libraries(kinect_cursor_debug
    PRIVATE
      blog_demo_common
      GLUT::GLUT
      OpenGL::GL
      "-framework ApplicationServices"
  )
endif()
