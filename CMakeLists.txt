cmake_minimum_required(VERSION 3.25)

# VERSION.txt is the sole authored application-version source.
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/VERSION.txt")
file(STRINGS "${CMAKE_CURRENT_SOURCE_DIR}/VERSION.txt" NETARENA_VERSION LIMIT_COUNT 1)
if(NOT NETARENA_VERSION MATCHES "^([0-9]+)\.([0-9]+)\.([0-9]+)(-[A-Za-z0-9.-]+)?$")
    message(FATAL_ERROR "VERSION.txt must contain a semantic version")
endif()
set(NETARENA_NUMERIC_VERSION "${CMAKE_MATCH_1}.${CMAKE_MATCH_2}.${CMAKE_MATCH_3}")
project(NetArena VERSION ${NETARENA_NUMERIC_VERSION} LANGUAGES CXX)

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
        shared/contracts/transport.hpp)
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
