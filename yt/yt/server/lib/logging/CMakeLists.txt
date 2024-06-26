
# This file was generated by the build system used internally in Yandex and called "ya"
# (https://github.com/yandex/yatool).
#
# Configuration files of ya build system are usually named ya.make. If ya.make file is present
# at the root of the repository, then this repository supports ya build. 
# 
# If the repository supports both CMake and ya build configurations, please modify both of them.
#
# If only CMake build configuration is supported then modify only CMake files and note that only
# simple modifications are allowed like adding source-files to targets or adding simple properties
# like target_include_directories. These modifications will be ported to original ya.make files
# by maintainers. Any complex modifications which can't be easily ported back to the ya build 
# system may be rejected.

find_package(linux-headers-generic REQUIRED)
add_subdirectory(unittests)
add_library(server-lib-logging)


target_compile_options(server-lib-logging PRIVATE
  -Wdeprecated-this-capture
)

target_link_libraries(server-lib-logging PUBLIC
  linux-headers-generic::linux-headers-generic
  contrib-libs-cxxsupp
  yutil
  cpp-yt-threading
  yt-yt-client
  yt-yt-core
  yt-library-program
)

target_sources(server-lib-logging PRIVATE
  ${PROJECT_SOURCE_DIR}/yt/yt/server/lib/logging/category_registry.cpp
  ${PROJECT_SOURCE_DIR}/yt/yt/server/lib/logging/program_describe_structured_logs_mixin.cpp
)






