cmake_minimum_required(VERSION 3.20)

project(gpumemd VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# Hardware-independent resource accounting lives in this library.
add_library(gpumemd_core STATIC src/core/resource_manager.cpp)
target_include_directories(gpumemd_core PUBLIC
    "${CMAKE_CURRENT_SOURCE_DIR}/include"
)
target_compile_features(gpumemd_core PUBLIC cxx_std_20)

find_package(Threads REQUIRED)
target_link_libraries(gpumemd_core PUBLIC Threads::Threads)

add_executable(gpumemd src/daemon/main.cpp)
target_link_libraries(gpumemd PRIVATE gpumemd_core)

if(CMAKE_CXX_COMPILER_ID MATCHES "Clang|GNU")
    target_compile_options(gpumemd PRIVATE -Wall -Wextra -Wpedantic)
endif()

include(CTest)
if(BUILD_TESTING)
    add_executable(resource_manager_test tests/resource_manager_test.cpp)
    target_link_libraries(resource_manager_test PRIVATE gpumemd_core)
    if(CMAKE_CXX_COMPILER_ID MATCHES "Clang|GNU")
        target_compile_options(resource_manager_test PRIVATE -Wall -Wextra -Wpedantic)
    endif()
    add_test(NAME resource_manager COMMAND resource_manager_test)
    set_tests_properties(resource_manager PROPERTIES TIMEOUT 10)

    foreach(scenario IN ITEMS help no_arguments unsupported_arguments)
        add_test(NAME cli_${scenario}
            COMMAND "${CMAKE_COMMAND}"
                "-DGPUMEMD=$<TARGET_FILE:gpumemd>"
                "-DSCENARIO=${scenario}"
                -P "${CMAKE_CURRENT_SOURCE_DIR}/tests/cli_smoke.cmake"
        )
        set_tests_properties(cli_${scenario} PROPERTIES TIMEOUT 5)
    endforeach()
endif()
