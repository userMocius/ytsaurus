if (NOT YT_BUILD_BRANCH)
  set(YT_BUILD_BRANCH "local")
endif()

if (NOT YT_BUILD_NUMBER)
  set(YT_BUILD_NUMBER 0)
endif()

if (NOT YT_BUILD_VCS_NUMBER)
  set(YT_BUILD_VCS_NUMBER "local")
endif()

if (NOT YT_BUILD_GIT_DEPTH)
  set(YT_BUILD_GIT_DEPTH 0)
endif()

if(CMAKE_BUILD_TYPE STREQUAL "Debug")
  set(YT_BUILD_TYPE "debug")
endif()
if(YT_USE_ASAN)
  set(YT_BUILD_TYPE "asan")
endif()
if(YT_USE_TSAN)
  set(YT_BUILD_TYPE "tsan")
endif()
if(YT_USE_MSAN)
  set(YT_BUILD_TYPE "msan")
endif()
if(_is_clang)
  set(YT_BUILD_TYPE "clang")
endif()

# Set the YT build version
set(YT_VERSION_MAJOR 19)
set(YT_VERSION_MINOR 2)
set(YT_VERSION_PATCH ${YT_BUILD_GIT_DEPTH})
set(YT_ABI_VERSION "${YT_VERSION_MAJOR}.${YT_VERSION_MINOR}")

# Clone some version attributes from YT to YP
set(YP_BUILD_BRANCH "${YT_BUILD_BRANCH}")
set(YP_BUILD_NUMBER "${YT_BUILD_NUMBER}")
set(YP_BUILD_VCS_NUMBER "${YT_BUILD_VCS_NUMBER}")
set(YP_BUILD_GIT_DEPTH "${YT_BUILD_GIT_DEPTH}")
set(YP_BUILD_TYPE "${YT_BUILD_TYPE}")

# Set the YP build version
set(YP_VERSION_MAJOR 0)
set(YP_VERSION_MINOR 1)
set(YP_VERSION_PATCH ${YP_BUILD_GIT_DEPTH})

# Construct the full YT version
set(YT_VERSION "${YT_VERSION_MAJOR}.${YT_VERSION_MINOR}.${YT_VERSION_PATCH}")
set(YT_VERSION "${YT_VERSION}-${YT_BUILD_BRANCH}")
if(YT_BUILD_TYPE)
  set(YT_VERSION "${YT_VERSION}-${YT_BUILD_TYPE}")
endif()
if(YT_BUILD_VCS_NUMBER)
  set(YT_VERSION "${YT_VERSION}~${YT_BUILD_VCS_NUMBER}")
endif()

# Construct the full YP version
set(YP_VERSION "${YP_VERSION_MAJOR}.${YP_VERSION_MINOR}.${YP_VERSION_PATCH}")
set(YP_VERSION "${YP_VERSION}-${YP_BUILD_BRANCH}")
if(YP_BUILD_TYPE)
  set(YP_VERSION "${YP_VERSION}-${YP_BUILD_TYPE}")
endif()
set(YP_VERSION "${YP_VERSION}~${YP_BUILD_VCS_NUMBER}")

# Underscore is forbidden in the version
string(REPLACE "_" "-" YT_VERSION ${YT_VERSION})
string(REPLACE "_" "-" YP_VERSION ${YP_VERSION})
# Slash is forbidden in the version
string(REPLACE "/" "-" YT_VERSION ${YT_VERSION})
string(REPLACE "/" "-" YP_VERSION ${YP_VERSION})
# Dot is forbidden in proper keywords
string(REPLACE "." "_" YT_ABI_VERSION_U ${YT_ABI_VERSION})

# Get the build name and hostname
find_program(_HOSTNAME NAMES hostname)
find_program(_UNAME NAMES uname)
find_program(_DATE NAMES date)

if(_HOSTNAME)
  set(_HOSTNAME ${_HOSTNAME} CACHE INTERNAL "")
  execute_process(
    COMMAND ${_HOSTNAME}
    OUTPUT_VARIABLE YT_BUILD_HOST
    OUTPUT_STRIP_TRAILING_WHITESPACE)
  string(REGEX REPLACE "[/\\\\+<> #]" "-" YT_BUILD_HOST "${YT_BUILD_HOST}")
else()
  set(YT_BUILD_HOST "unknown")
endif()

if(_UNAME)
  set(_UNAME ${_UNAME} CACHE INTERNAL "")
  execute_process(
    COMMAND ${_UNAME} -a
    OUTPUT_VARIABLE YT_BUILD_MACHINE
    OUTPUT_STRIP_TRAILING_WHITESPACE)
else()
  set(YT_BUILD_MACHINE "unknown")
endif()

if(_DATE)
  set(_DATE ${_DATE} CACHE INTERNAL "")
  execute_process(
    COMMAND ${_DATE}
    OUTPUT_VARIABLE YT_BUILD_TIME
    OUTPUT_STRIP_TRAILING_WHITESPACE)
else()
  set(YT_BUILD_TIME "unknown")
endif()

# Clone some version attributes from YT to YP
set(YP_BUILD_HOST "${YT_BUILD_HOST}")
set(YP_BUILD_MACHINE "${YT_BUILD_MACHINE}")
set(YP_BUILD_TIME "${YT_BUILD_TIME}")
