cmake_minimum_required(VERSION 3.10)

project(proc-load C)

set(CMAKE_C_STANDARD 99)
set(CMAKE_C_STANDARD_REQUIRED ON)

find_package(PkgConfig REQUIRED)
pkg_check_modules(JSONC REQUIRED json-c)
pkg_check_modules(DLT REQUIRED automotive-dlt)

include_directories(${JSONC_INCLUDE_DIRS} ${DLT_INCLUDE_DIRS})
link_directories(${JSONC_LIBRARY_DIRS} ${DLT_LIBRARY_DIRS})
add_definitions(${JSONC_CFLAGS_OTHER} ${DLT_CFLAGS_OTHER})

add_definitions("-fno-omit-frame-pointer -O1 -g")

# Add the gengetopt-generated file
add_executable(proc-load proc-load.c cmdline.c)

target_link_libraries(proc-load ${JSONC_LIBRARIES} ${DLT_LIBRARIES})
