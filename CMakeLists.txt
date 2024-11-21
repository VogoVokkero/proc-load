cmake_minimum_required(VERSION 3.10)

project(proc-load C)

set(CMAKE_C_STANDARD 99)
set(CMAKE_C_STANDARD_REQUIRED ON)

find_package(PkgConfig REQUIRED)
pkg_check_modules(CJSON REQUIRED libcjson)
pkg_check_modules(DLT REQUIRED automotive-dlt)

include_directories(${CJSON_INCLUDE_DIRS} ${DLT_INCLUDE_DIRS})
link_directories(${CJSON_LIBRARY_DIRS} ${DLT_LIBRARY_DIRS})
add_definitions(${CJSON_CFLAGS_OTHER} ${DLT_CFLAGS_OTHER})

# Add the gengetopt-generated file
add_executable(proc-load proc-load.c cmdline.c)

target_link_libraries(proc-load ${CJSON_LIBRARIES} ${DLT_LIBRARIES})
