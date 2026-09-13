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

add_library(gpumemd_protocol STATIC src/protocol/protocol.cpp)
target_include_directories(gpumemd_protocol PUBLIC
    "${CMAKE_CURRENT_SOURCE_DIR}/include"
)
target_compile_features(gpumemd_protocol PUBLIC cxx_std_20)

add_library(gpumemd_server STATIC src/ipc/unix_socket_server.cpp)
target_include_directories(gpumemd_server PUBLIC
    "${CMAKE_CURRENT_SOURCE_DIR}/include"
)
target_compile_features(gpumemd_server PUBLIC cxx_std_20)
target_link_libraries(gpumemd_server PUBLIC gpumemd_core gpumemd_protocol Threads::Threads)

add_executable(gpumemd src/daemon/main.cpp)
target_link_libraries(gpumemd PRIVATE gpumemd_server)

add_executable(gpumemctl src/client/main.cpp)
target_link_libraries(gpumemctl PRIVATE gpumemd_protocol)

if(CMAKE_CXX_COMPILER_ID MATCHES "Clang|GNU")
    target_compile_options(gpumemd PRIVATE -Wall -Wextra -Wpedantic)
    target_compile_options(gpumemctl PRIVATE -Wall -Wextra -Wpedantic)
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

    add_executable(protocol_test tests/protocol_test.cpp)
    target_link_libraries(protocol_test PRIVATE gpumemd_protocol)
    if(CMAKE_CXX_COMPILER_ID MATCHES "Clang|GNU")
        target_compile_options(protocol_test PRIVATE -Wall -Wextra -Wpedantic)
    endif()
    add_test(NAME protocol COMMAND protocol_test)
    set_tests_properties(protocol PROPERTIES TIMEOUT 10)

    add_executable(server_test tests/server_test.cpp)
    target_link_libraries(server_test PRIVATE gpumemd_server)
    if(CMAKE_CXX_COMPILER_ID MATCHES "Clang|GNU")
        target_compile_options(server_test PRIVATE -Wall -Wextra -Wpedantic)
    endif()
    add_test(NAME server COMMAND server_test)
    set_tests_properties(server PROPERTIES TIMEOUT 10)

    foreach(scenario IN ITEMS help no_arguments unsupported_arguments)
        add_test(NAME cli_${scenario}
            COMMAND "${CMAKE_COMMAND}"
                "-DGPUMEMD=$<TARGET_FILE:gpumemd>"
                "-DSCENARIO=${scenario}"
                -P "${CMAKE_CURRENT_SOURCE_DIR}/tests/cli_smoke.cmake"
        )
        set_tests_properties(cli_${scenario} PROPERTIES TIMEOUT 5)
    endforeach()

    add_test(NAME cli_client_help
        COMMAND "${CMAKE_COMMAND}"
            "-DGPUMEMCTL=$<TARGET_FILE:gpumemctl>"
            -DSCENARIO=client_help
            -P "${CMAKE_CURRENT_SOURCE_DIR}/tests/cli_smoke.cmake"
    )
    set_tests_properties(cli_client_help PROPERTIES TIMEOUT 5)
endif()
