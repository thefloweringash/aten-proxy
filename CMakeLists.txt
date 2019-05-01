cmake_minimum_required(VERSION 3.5)
project(aten-proxy)

set(CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake" ${CMAKE_MODULE_PATH})

set(CMAKE_CXX_STANDARD 11)
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -Wall -pedantic -Wextra")

find_package(PkgConfig)
find_package(Libev REQUIRED)

set(THREADS_PREFER_PTHREAD_FLAG ON)
find_package(Threads REQUIRED)

pkg_check_modules(libvncserver REQUIRED IMPORTED_TARGET libvncserver)

add_executable(aten-proxy
  main.cc
  keymap.cc
  connection.cc
)

target_link_libraries(aten-proxy
  PkgConfig::libvncserver
  Libev::Libev
  Threads::Threads
)
