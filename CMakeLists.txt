cmake_minimum_required(VERSION 3.25)

# VERSION.txt is the sole authored application-version source.
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/VERSION.txt")
file(STRINGS "${CMAKE_CURRENT_SOURCE_DIR}/VERSION.txt" NETARENA_VERSION LIMIT_COUNT 1)
if(NOT NETARENA_VERSION MATCHES "^([0-9]+)\.([0-9]+)\.([0-9]+)(-[A-Za-z0-9.-]+)?$")
    message(FATAL_ERROR "VERSION.txt must contain a semantic version")
endif()
set(NETARENA_NUMERIC_VERSION "${CMAKE_MATCH_1}.${CMAKE_MATCH_2}.${CMAKE_MATCH_3}")
project(NetArena VERSION ${NETARENA_NUMERIC_VERSION} LANGUAGES C CXX)

set(CMAKE_CXX_EXTENSIONS OFF)
include(CTest)
add_library(netarena_shared INTERFACE)
target_include_directories(netarena_shared INTERFACE "${CMAKE_CURRENT_SOURCE_DIR}")
target_compile_features(netarena_shared INTERFACE cxx_std_20)

function(netarena_warnings target)
    if(MSVC)
        target_compile_options(${target} PRIVATE /W4 /WX)
    else()
        target_compile_options(${target} PRIVATE -Wall -Wextra -Wpedantic -Werror)
    endif()
endfunction()

file(MAKE_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/generated")
file(WRITE "${CMAKE_CURRENT_BINARY_DIR}/generated/version.hpp"
    "#pragma once\n#include <string_view>\nnamespace netarena { inline constexpr std::string_view version = \"${NETARENA_VERSION}\"; }\n")
add_executable(netarena_foundation_demo apps/foundation_demo.cpp)
target_link_libraries(netarena_foundation_demo PRIVATE netarena_shared)
target_include_directories(netarena_foundation_demo PRIVATE "${CMAKE_CURRENT_BINARY_DIR}/generated")
netarena_warnings(netarena_foundation_demo)

if(BUILD_TESTING)
    foreach(test_name IN ITEMS foundation contracts)
        add_executable(${test_name}_test tests/unit/${test_name}_test.cpp)
        target_link_libraries(${test_name}_test PRIVATE netarena_shared)
        netarena_warnings(${test_name}_test)
        add_test(NAME ${test_name} COMMAND ${test_name}_test)
    endforeach()
    add_test(NAME foundation_demo COMMAND netarena_foundation_demo)
    set_tests_properties(foundation_demo PROPERTIES
        PASS_REGULAR_EXPRESSION "foundations demonstration.*no network server")

    # Every public header is compiled as the only include in its translation unit.
    set(public_headers
        shared/foundation/result.hpp shared/foundation/types.hpp
        shared/foundation/clock.hpp shared/contracts/messages.hpp
        shared/contracts/transport.hpp shared/protocol/protocol_codec.hpp
        adapters/transport/enet_transport.hpp client/core/interop_client.hpp)
    set(header_sources)
    foreach(header IN LISTS public_headers)
        string(REPLACE "/" "_" unit_name "${header}")
        set(unit "${CMAKE_CURRENT_BINARY_DIR}/generated/${unit_name}.cpp")
        file(WRITE "${unit}" "#include \"${header}\"\n")
        list(APPEND header_sources "${unit}")
    endforeach()
    add_library(public_header_checks OBJECT ${header_sources})
    target_link_libraries(public_header_checks PRIVATE netarena_shared)
    netarena_warnings(public_header_checks)
endif()

# WP-02: transports and protocols stay independent of Godot.
include(cmake/Dependencies.cmake)
add_library(netarena_protocol STATIC
    shared/protocol/protocol_codec.cpp
    shared/protocol/codec_envelope.cpp)
target_link_libraries(netarena_protocol PUBLIC netarena_shared PRIVATE netarena_json)
set_target_properties(netarena_protocol PROPERTIES POSITION_INDEPENDENT_CODE ON)
netarena_warnings(netarena_protocol)
add_library(netarena_transport STATIC adapters/transport/enet_transport.cpp)
target_link_libraries(netarena_transport PUBLIC netarena_shared PRIVATE enet)
set_target_properties(netarena_transport PROPERTIES POSITION_INDEPENDENT_CODE ON)
netarena_warnings(netarena_transport)
add_library(netarena_interop_client STATIC client/core/interop_client.cpp)
target_link_libraries(netarena_interop_client PUBLIC netarena_shared netarena_protocol)
set_target_properties(netarena_interop_client PROPERTIES POSITION_INDEPENDENT_CODE ON)
netarena_warnings(netarena_interop_client)
add_executable(netarena_interop_server apps/interop_server.cpp)
target_link_libraries(netarena_interop_server PRIVATE netarena_protocol netarena_transport)
netarena_warnings(netarena_interop_server)
if(BUILD_TESTING)
    add_executable(protocol_codec_test tests/unit/protocol_codec_test.cpp)
    target_link_libraries(protocol_codec_test PRIVATE netarena_protocol netarena_json)
    netarena_warnings(protocol_codec_test)
    add_test(NAME protocol_codec COMMAND protocol_codec_test)
    add_executable(transport_test tests/integration/transport_test.cpp)
    target_link_libraries(transport_test PRIVATE netarena_transport enet)
    target_include_directories(transport_test PRIVATE "${PROJECT_SOURCE_DIR}/tests/unit")
    netarena_warnings(transport_test)
    add_test(NAME transport COMMAND transport_test)
    set_tests_properties(transport PROPERTIES TIMEOUT 25)
    add_executable(interop_client_test tests/unit/interop_client_test.cpp)
    target_link_libraries(interop_client_test PRIVATE netarena_interop_client)
    netarena_warnings(interop_client_test)
    add_test(NAME interop_client COMMAND interop_client_test)
    add_executable(client_loopback_test tests/integration/client_loopback_test.cpp)
    target_link_libraries(client_loopback_test PRIVATE netarena_interop_client netarena_transport)
    netarena_warnings(client_loopback_test)
    add_test(NAME client_loopback COMMAND client_loopback_test)
    set_tests_properties(client_loopback PROPERTIES TIMEOUT 20)
endif()

option(NETARENA_BUILD_GODOT "Build the Godot 4.5 GDExtension" OFF)
set(GODOT_EXECUTABLE "" CACHE FILEPATH "Godot executable for actual interop acceptance")
if(NETARENA_BUILD_GODOT)
    netarena_require("${NETARENA_DEPS_DIR}/godot-cpp/CMakeLists.txt")
    set(GODOTCPP_BUILD_PROFILE "${PROJECT_SOURCE_DIR}/cmake/godot-build-profile.json" CACHE PATH "" FORCE)
    set(GODOTCPP_DISABLE_EXCEPTIONS OFF CACHE BOOL "" FORCE)
    set(GODOTCPP_SYSTEM_HEADERS ON CACHE BOOL "" FORCE)
    set(GODOTCPP_ENABLE_TESTING OFF CACHE BOOL "" FORCE)
    add_subdirectory("${NETARENA_DEPS_DIR}/godot-cpp" "${CMAKE_BINARY_DIR}/third_party/godot-cpp" EXCLUDE_FROM_ALL)
    add_library(netarena_godot SHARED
        client/godot/native/netarena_bridge.cpp
        client/godot/native/runtime_composition.cpp
        client/godot/native/register_types.cpp)
    target_link_libraries(netarena_godot PRIVATE godot::cpp netarena_interop_client netarena_transport)
    set_target_properties(netarena_godot PROPERTIES
        LIBRARY_OUTPUT_DIRECTORY "${PROJECT_SOURCE_DIR}/client/godot/bin"
        RUNTIME_OUTPUT_DIRECTORY "${PROJECT_SOURCE_DIR}/client/godot/bin"
        OUTPUT_NAME netarena_godot
        CXX_VISIBILITY_PRESET hidden VISIBILITY_INLINES_HIDDEN YES)
    netarena_warnings(netarena_godot)
    if(BUILD_TESTING AND GODOT_EXECUTABLE)
        find_package(Python3 3.12 REQUIRED COMPONENTS Interpreter)
        add_test(NAME godot_interop COMMAND "${Python3_EXECUTABLE}"
            "${PROJECT_SOURCE_DIR}/tests/integration/run_godot_interop.py"
            --server $<TARGET_FILE:netarena_interop_server>
            --godot "${GODOT_EXECUTABLE}" --project "${PROJECT_SOURCE_DIR}/client/godot")
        set_tests_properties(godot_interop PROPERTIES TIMEOUT 100 RUN_SERIAL TRUE)
    elseif(BUILD_TESTING)
        message(STATUS "Godot runtime test NOT configured: set GODOT_EXECUTABLE to run actual engine acceptance")
    endif()
endif()
