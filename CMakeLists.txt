cmake_minimum_required(VERSION 3.16)
project(pymtpng CXX)

find_package(
  Python 3.9 REQUIRED
  COMPONENTS Interpreter Development.Module
  OPTIONAL_COMPONENTS Development.SABIModule
)

# Use LTO for building this module
set(CMAKE_INTERPROCEDURAL_OPTIMIZATION TRUE)

# Fetch nanobind library
include(FetchContent)
FetchContent_Declare(
  nanobind
  GIT_REPOSITORY https://github.com/wjakob/nanobind
  GIT_TAG        c5454462e35f29310df05b412b5c48997d634bdd
)
FetchContent_MakeAvailable(nanobind)

# Fetch mtpng source
FetchContent_Declare(
  mtpng
  GIT_REPOSITORY https://github.com/bvibber/mtpng
  GIT_TAG        40f389daf89603d493e22597fdbe3d5b592a49a6
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
nanobind_add_module(${PROJECT_NAME}
  STABLE_ABI
  "pymtpng.cpp"
  "mtpng.hpp"
)
nanobind_add_stub(
  ${PROJECT_NAME}_stub
  MODULE ${PROJECT_NAME}
  OUTPUT ${PROJECT_NAME}.pyi
  PYTHON_PATH $<TARGET_FILE_DIR:${PROJECT_NAME}>
  DEPENDS ${PROJECT_NAME}
  MARKER_FILE py.typed
)
target_link_libraries(${PROJECT_NAME} PRIVATE mtpng_static)
set_target_properties(${PROJECT_NAME} PROPERTIES
  CXX_STANDARD 20
  CXX_STANDARD_REQUIRED ON
)
if(DEFINED SKBUILD_PROJECT_VERSION)
  target_compile_definitions(${PROJECT_NAME} PRIVATE VERSION_INFO=${SKBUILD_PROJECT_VERSION})
endif()

if(CMAKE_CXX_COMPILER_ID MATCHES "GNU")
  target_compile_options(${PROJECT_NAME} PRIVATE -Wall -Wextra)
elseif(CMAKE_CXX_COMPILER_ID MATCHES "MSVC")
  target_compile_options(${PROJECT_NAME} PUBLIC "/Zc:__cplusplus")
endif()

install(TARGETS ${PROJECT_NAME}
  DESTINATION "${PROJECT_NAME}"
)
install(FILES
  ${CMAKE_CURRENT_SOURCE_DIR}/${PROJECT_NAME}/__init__.py
  ${${PROJECT_NAME}_BINARY_DIR}/py.typed
  ${${PROJECT_NAME}_BINARY_DIR}/${PROJECT_NAME}.pyi
  DESTINATION "${PROJECT_NAME}"
)
