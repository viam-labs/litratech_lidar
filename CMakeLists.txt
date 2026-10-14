cmake_minimum_required(VERSION 3.16)

project(litratech_lidar LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

set(LDCP_DIR "${CMAKE_SOURCE_DIR}/third_party/ldcp_sdk")
if(NOT EXISTS "${LDCP_DIR}/src/device.cpp")
  message(FATAL_ERROR
    "Missing ${LDCP_DIR}. Initialize the submodule with: git submodule update --init --recursive")
endif()

add_library(ldcp_sdk STATIC
  "${LDCP_DIR}/src/bootloader.cpp"
  "${LDCP_DIR}/src/device.cpp"
  "${LDCP_DIR}/src/device_base.cpp"
  "${LDCP_DIR}/src/device_info.cpp"
  "${LDCP_DIR}/src/location.cpp"
  "${LDCP_DIR}/src/session.cpp"
  "${LDCP_DIR}/src/transport.cpp"
)
target_include_directories(ldcp_sdk
  PUBLIC "${LDCP_DIR}/include"
  PRIVATE "${LDCP_DIR}/third_party/Asio/asio-1.18.0/include"
  PRIVATE "${LDCP_DIR}/third_party/RapidJSON/rapidjson-1.1.0/include"
)
target_compile_definitions(ldcp_sdk PRIVATE ASIO_STANDALONE ASIO_DISABLE_VISIBILITY)
target_compile_options(ldcp_sdk PRIVATE
  -include "${CMAKE_SOURCE_DIR}/src/ldcp_posix_compat.h")
target_compile_features(ldcp_sdk PUBLIC cxx_std_17)
if(UNIX AND NOT APPLE)
  target_link_libraries(ldcp_sdk PUBLIC pthread)
endif()

add_library(scan_points STATIC src/scan_points.cpp)
target_link_libraries(scan_points PUBLIC ldcp_sdk)
target_include_directories(scan_points PUBLIC "${CMAKE_SOURCE_DIR}/src")

add_executable(scan_points_test src/scan_points_test.cpp)
target_link_libraries(scan_points_test PRIVATE scan_points)

option(LITRATECH_BUILD_MODULE "Build the Viam camera module (requires the Viam C++ SDK)" ON)
if(LITRATECH_BUILD_MODULE)
  include(FetchContent)
  # Declared before the SDK so this pin wins. xtensor 0.25 does not compile
  # with the Clang version on Viam's macOS cloud builders.
  FetchContent_Declare(
    xtensor
    GIT_REPOSITORY https://github.com/xtensor-stack/xtensor.git
    GIT_TAG 0.26.0
  )
  FetchContent_Declare(
    viam-cpp-sdk
    GIT_REPOSITORY https://github.com/viamrobotics/viam-cpp-sdk.git
    GIT_TAG releases/v0.41.1
  )
  FetchContent_MakeAvailable(viam-cpp-sdk)

  add_executable(litratech-lidar src/main.cpp src/litratech_camera.cpp)
  target_link_libraries(litratech-lidar PRIVATE viam-cpp-sdk::viamsdk scan_points)
endif()
