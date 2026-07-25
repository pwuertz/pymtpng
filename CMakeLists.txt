cmake_minimum_required(VERSION 3.24)

if(NOT SKBUILD)
  message(FATAL_ERROR "Build should be driven by scikit-build-core")
endif()
set(MODULE_NAME "_${SKBUILD_PROJECT_NAME}")
set(MODULE_VERSION ${SKBUILD_PROJECT_VERSION})
project(${MODULE_NAME} VERSION ${MODULE_VERSION} LANGUAGES CXX)

find_package(
  Python 3.12 REQUIRED
  COMPONENTS Interpreter Development.Module
  OPTIONAL_COMPONENTS Development.SABIModule
)
find_package(nanobind CONFIG REQUIRED)

include(FetchContent)
FetchContent_Declare(
  mtpng
  GIT_REPOSITORY https://github.com/bvibber/mtpng
  GIT_TAG        0.3.4
)
FetchContent_MakeAvailable(mtpng)

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
  VERBATIM
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

nanobind_add_module(
  ${MODULE_NAME}
  STABLE_ABI
  NB_STATIC
  "src/pymtpng.cpp"
  "src/mtpng.hpp"
)
nanobind_add_stub(
  ${MODULE_NAME}_stub
  MODULE ${MODULE_NAME}
  OUTPUT ${MODULE_NAME}.pyi
  PYTHON_PATH $<TARGET_FILE_DIR:${MODULE_NAME}>
  DEPENDS ${MODULE_NAME}
  MARKER_FILE py.typed
)
target_link_libraries(${MODULE_NAME} PRIVATE mtpng_static)
set_target_properties(${MODULE_NAME} PROPERTIES
  CXX_STANDARD 20
  CXX_STANDARD_REQUIRED ON
)
target_compile_definitions(${MODULE_NAME} PRIVATE MODULE_NAME=${MODULE_NAME})
target_compile_definitions(${MODULE_NAME} PRIVATE MODULE_VERSION=${MODULE_VERSION})

if(CMAKE_CXX_COMPILER_ID MATCHES "GNU")
  target_compile_options(${MODULE_NAME} PRIVATE -Wall -Wextra)
elseif(CMAKE_CXX_COMPILER_ID MATCHES "MSVC")
  target_compile_options(${MODULE_NAME} PUBLIC "/Zc:__cplusplus")
endif()

install(
  TARGETS
    ${MODULE_NAME}
  LIBRARY
  DESTINATION ${SKBUILD_PROJECT_NAME}
)
install(
  FILES
    ${CMAKE_CURRENT_SOURCE_DIR}/src/__init__.py
    ${${PROJECT_NAME}_BINARY_DIR}/py.typed
    ${${PROJECT_NAME}_BINARY_DIR}/${MODULE_NAME}.pyi
  DESTINATION ${SKBUILD_PROJECT_NAME}
)
