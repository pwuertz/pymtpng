cmake_minimum_required(VERSION 3.16)
project(pymtpng CXX)

include(FetchContent)

if(WIN32)
  # Prevent pybind11 from sharing resources with other, potentially ABI incompatible modules
  # https://github.com/pybind/pybind11/issues/2898
  add_definitions(-DPYBIND11_COMPILER_TYPE="_${PROJECT_NAME}_abi")
endif()

# Fetch pybind11 library
FetchContent_Declare(
  pybind11
  GIT_REPOSITORY https://github.com/pybind/pybind11
  GIT_TAG        v2.11.1
  GIT_SHALLOW    True
)
FetchContent_MakeAvailable(pybind11)

# Fetch mtpng source
FetchContent_Declare(
  mtpng
  GIT_REPOSITORY https://github.com/brion/mtpng
  GIT_TAG        016c3fb07b6300bb8a08ee8790e80bc1c93b0c84
)
FetchContent_MakeAvailable(mtpng)

# Create target for mtpng library (built with cargo)
if(WIN32)
  set(MTPNG_STATIC_LIB "${mtpng_SOURCE_DIR}/target/release/mtpng.lib")
else()
  set(MTPNG_STATIC_LIB "${mtpng_SOURCE_DIR}/target/release/libmtpng.a")
endif()
add_custom_command(
  OUTPUT "${MTPNG_STATIC_LIB}"
  WORKING_DIRECTORY "${mtpng_SOURCE_DIR}"
  COMMAND cargo build --release --no-default-features --features capi
  COMMENT "Building mtpng library"
)
add_custom_target(mtpng_static_build DEPENDS "${MTPNG_STATIC_LIB}")
add_library(mtpng_static STATIC IMPORTED)
add_dependencies(mtpng_static mtpng_static_build)
set_target_properties(mtpng_static PROPERTIES
  IMPORTED_LOCATION "${MTPNG_STATIC_LIB}"
  INTERFACE_INCLUDE_DIRECTORIES "${mtpng_SOURCE_DIR}/c"
)
if(WIN32)
  target_link_libraries(mtpng_static INTERFACE userenv ntdll ws2_32 Bcrypt)
else()
  target_link_libraries(mtpng_static INTERFACE z)
endif()

# Create target for python library
pybind11_add_module(${PROJECT_NAME} "pymtpng.cpp" "mtpng.hpp")
target_link_libraries(${PROJECT_NAME} PRIVATE mtpng_static)
set_target_properties(${PROJECT_NAME} PROPERTIES
  CXX_STANDARD 20
  CXX_STANDARD_REQUIRED ON
)

if(CMAKE_CXX_COMPILER_ID MATCHES "GNU")
  target_compile_options(${PROJECT_NAME} PRIVATE -Wall -Wextra)
elseif(CMAKE_CXX_COMPILER_ID MATCHES "MSVC")
  target_compile_options(${PROJECT_NAME} PUBLIC "/Zc:__cplusplus")
endif()

install(TARGETS ${PROJECT_NAME} DESTINATION ".")
