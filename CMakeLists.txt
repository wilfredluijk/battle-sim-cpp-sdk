cmake_minimum_required(VERSION 3.20)
project(naval_sdk VERSION 0.1.0 LANGUAGES CXX)

include(GNUInstallDirs)
include(CMakePackageConfigHelpers)
if(CMAKE_SOURCE_DIR STREQUAL PROJECT_SOURCE_DIR)
    set(NAVAL_SDK_TOP_LEVEL ON)
else()
    set(NAVAL_SDK_TOP_LEVEL OFF)
endif()
option(NAVAL_SDK_BUILD_TESTS "Build the SDK tests" ${NAVAL_SDK_TOP_LEVEL})
option(NAVAL_SDK_BUILD_EXAMPLES "Build example bots and CLI" ${NAVAL_SDK_TOP_LEVEL})
option(NAVAL_SDK_SANITIZERS "Enable address and undefined behavior sanitizers" OFF)

find_package(Boost 1.74 REQUIRED CONFIG)
find_package(OpenSSL 1.1.1 REQUIRED)
find_package(nlohmann_json 3.10 REQUIRED)
find_package(Threads REQUIRED)

add_library(naval_sdk STATIC src/protocol.cpp src/helpers.cpp src/bot.cpp
    src/transport.cpp src/recording.cpp src/tactical.cpp src/cli.cpp)
add_library(naval_sdk::naval_sdk ALIAS naval_sdk)
set_target_properties(naval_sdk PROPERTIES POSITION_INDEPENDENT_CODE ON)
target_compile_features(naval_sdk PUBLIC cxx_std_17)
target_include_directories(naval_sdk PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>)
target_link_libraries(naval_sdk PUBLIC nlohmann_json::nlohmann_json
    PRIVATE Boost::headers OpenSSL::SSL OpenSSL::Crypto Threads::Threads)
target_compile_definitions(naval_sdk PRIVATE BOOST_ERROR_CODE_HEADER_ONLY)
if(WIN32)
    target_link_libraries(naval_sdk PRIVATE ws2_32 mswsock crypt32)
endif()
if(MSVC)
    target_compile_options(naval_sdk PRIVATE /W4 /permissive-)
else()
    target_compile_options(naval_sdk PRIVATE -Wall -Wextra -Wpedantic)
endif()
if(NAVAL_SDK_SANITIZERS AND NOT MSVC)
    target_compile_options(naval_sdk PUBLIC -fsanitize=address,undefined -fno-omit-frame-pointer)
    target_link_options(naval_sdk PUBLIC -fsanitize=address,undefined)
endif()

if(NAVAL_SDK_BUILD_EXAMPLES)
    foreach(example my_bot hunter_bot)
        add_executable(${example} examples/${example}.cpp)
        target_link_libraries(${example} PRIVATE naval_sdk::naval_sdk)
    endforeach()
    add_executable(naval-sdk examples/cli.cpp)
    target_link_libraries(naval-sdk PRIVATE naval_sdk::naval_sdk)
endif()
if(NAVAL_SDK_BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()

install(TARGETS naval_sdk EXPORT naval_sdkTargets ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR})
install(DIRECTORY include/ DESTINATION ${CMAKE_INSTALL_INCLUDEDIR})
install(EXPORT naval_sdkTargets NAMESPACE naval_sdk:: DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/naval_sdk)
configure_package_config_file(cmake/naval_sdkConfig.cmake.in
    ${CMAKE_CURRENT_BINARY_DIR}/naval_sdkConfig.cmake
    INSTALL_DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/naval_sdk)
write_basic_package_version_file(${CMAKE_CURRENT_BINARY_DIR}/naval_sdkConfigVersion.cmake
    VERSION ${PROJECT_VERSION} COMPATIBILITY SameMajorVersion)
install(FILES ${CMAKE_CURRENT_BINARY_DIR}/naval_sdkConfig.cmake
    ${CMAKE_CURRENT_BINARY_DIR}/naval_sdkConfigVersion.cmake
    DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/naval_sdk)
install(FILES LICENSE NOTICE DESTINATION ${CMAKE_INSTALL_DATADIR}/naval_sdk)
