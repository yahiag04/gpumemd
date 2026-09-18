cmake_minimum_required(VERSION 3.20)

project(gpumemd VERSION 0.11.0 LANGUAGES CXX)

set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

include(CheckLanguage)
check_language(CUDA)

# Hardware-independent resource accounting lives in this library.
add_library(gpumemd_core STATIC
    src/backend/mock_backend.cpp
    src/core/model_registry.cpp
    src/core/model_loader.cpp
    src/core/memory_allocator.cpp
    src/core/model_residency.cpp
    src/core/resource_manager.cpp
)
target_include_directories(gpumemd_core PUBLIC
    "${CMAKE_CURRENT_SOURCE_DIR}/include"
)
target_compile_features(gpumemd_core PUBLIC cxx_std_20)

find_package(Threads REQUIRED)
target_link_libraries(gpumemd_core PUBLIC Threads::Threads)

if(APPLE)
    enable_language(OBJCXX)
    add_library(gpumemd_metal STATIC src/backend/metal_backend.mm)
    target_include_directories(gpumemd_metal PUBLIC
        "${CMAKE_CURRENT_SOURCE_DIR}/include"
    )
    target_compile_features(gpumemd_metal PUBLIC cxx_std_20)
    target_link_libraries(gpumemd_metal PUBLIC "-framework Metal" objc)
    set_source_files_properties(src/backend/metal_backend.mm PROPERTIES
        COMPILE_OPTIONS "-fobjc-arc"
    )
endif()

if(CMAKE_CUDA_COMPILER)
    enable_language(CUDA)
    find_package(CUDAToolkit REQUIRED)
    add_library(gpumemd_cuda STATIC
        src/backend/cuda_backend.cu
        src/backend/cuda_ipc_client.cu
    )
    target_include_directories(gpumemd_cuda PUBLIC
        "${CMAKE_CURRENT_SOURCE_DIR}/include"
    )
    set_target_properties(gpumemd_cuda PROPERTIES
        CUDA_STANDARD 20
        CUDA_STANDARD_REQUIRED YES
    )
    target_link_libraries(gpumemd_cuda PUBLIC CUDA::cudart)
endif()

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
if(APPLE)
    target_compile_definitions(gpumemd PRIVATE GPUMEMD_HAS_METAL=1)
    target_link_libraries(gpumemd PRIVATE gpumemd_metal)
endif()
if(CMAKE_CUDA_COMPILER)
    target_compile_definitions(gpumemd PRIVATE GPUMEMD_HAS_CUDA=1)
    target_link_libraries(gpumemd PRIVATE gpumemd_cuda)
endif()

add_executable(gpumemctl src/client/main.cpp)
target_link_libraries(gpumemctl PRIVATE gpumemd_protocol)

add_executable(gpumemd-bench src/benchmark/main.cpp)
target_link_libraries(gpumemd-bench PRIVATE gpumemd_core)
if(APPLE)
    target_compile_definitions(gpumemd-bench PRIVATE GPUMEMD_HAS_METAL=1)
    target_link_libraries(gpumemd-bench PRIVATE gpumemd_metal)
endif()
if(CMAKE_CUDA_COMPILER)
    target_compile_definitions(gpumemd-bench PRIVATE GPUMEMD_HAS_CUDA=1)
    target_link_libraries(gpumemd-bench PRIVATE gpumemd_cuda)
endif()

if(CMAKE_CXX_COMPILER_ID MATCHES "Clang|GNU")
    target_compile_options(gpumemd PRIVATE -Wall -Wextra -Wpedantic)
    target_compile_options(gpumemctl PRIVATE -Wall -Wextra -Wpedantic)
    target_compile_options(gpumemd-bench PRIVATE -Wall -Wextra -Wpedantic)
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

    add_executable(model_registry_test tests/model_registry_test.cpp)
    target_link_libraries(model_registry_test PRIVATE gpumemd_core)
    if(CMAKE_CXX_COMPILER_ID MATCHES "Clang|GNU")
        target_compile_options(model_registry_test PRIVATE -Wall -Wextra -Wpedantic)
    endif()
    add_test(NAME model_registry COMMAND model_registry_test)
    set_tests_properties(model_registry PROPERTIES TIMEOUT 10)

    add_executable(model_residency_test tests/model_residency_test.cpp)
    target_link_libraries(model_residency_test PRIVATE gpumemd_core)
    if(CMAKE_CXX_COMPILER_ID MATCHES "Clang|GNU")
        target_compile_options(model_residency_test PRIVATE -Wall -Wextra -Wpedantic)
    endif()
    add_test(NAME model_residency COMMAND model_residency_test)
    set_tests_properties(model_residency PROPERTIES TIMEOUT 10)

    add_executable(mock_backend_test tests/mock_backend_test.cpp)
    target_link_libraries(mock_backend_test PRIVATE gpumemd_core)
    if(CMAKE_CXX_COMPILER_ID MATCHES "Clang|GNU")
        target_compile_options(mock_backend_test PRIVATE -Wall -Wextra -Wpedantic)
    endif()
    add_test(NAME mock_backend COMMAND mock_backend_test)
    set_tests_properties(mock_backend PROPERTIES TIMEOUT 10)

    add_executable(model_file_test tests/model_file_test.cpp)
    target_link_libraries(model_file_test PRIVATE gpumemd_core)
    if(CMAKE_CXX_COMPILER_ID MATCHES "Clang|GNU")
        target_compile_options(model_file_test PRIVATE -Wall -Wextra -Wpedantic)
    endif()
    add_test(NAME model_file COMMAND model_file_test)
    set_tests_properties(model_file PROPERTIES TIMEOUT 10)

    add_executable(memory_allocator_test tests/memory_allocator_test.cpp)
    target_link_libraries(memory_allocator_test PRIVATE gpumemd_core)
    if(CMAKE_CXX_COMPILER_ID MATCHES "Clang|GNU")
        target_compile_options(memory_allocator_test PRIVATE -Wall -Wextra -Wpedantic)
    endif()
    add_test(NAME memory_allocator COMMAND memory_allocator_test)
    set_tests_properties(memory_allocator PROPERTIES TIMEOUT 10)

    if(APPLE)
        add_executable(metal_backend_test tests/metal_backend_test.cpp)
        target_link_libraries(metal_backend_test PRIVATE gpumemd_metal)
        if(CMAKE_CXX_COMPILER_ID MATCHES "Clang|GNU")
            target_compile_options(metal_backend_test PRIVATE -Wall -Wextra -Wpedantic)
        endif()
        add_test(NAME metal_backend COMMAND metal_backend_test)
        set_tests_properties(metal_backend PROPERTIES TIMEOUT 10)
    endif()

    if(CMAKE_CUDA_COMPILER)
        add_executable(cuda_backend_test tests/cuda_backend_test.cpp)
        target_link_libraries(cuda_backend_test PRIVATE gpumemd_cuda)
        if(CMAKE_CXX_COMPILER_ID MATCHES "Clang|GNU")
            target_compile_options(cuda_backend_test PRIVATE -Wall -Wextra -Wpedantic)
        endif()
        add_test(NAME cuda_backend COMMAND cuda_backend_test)
        set_tests_properties(cuda_backend PROPERTIES TIMEOUT 10)

        add_executable(cuda_ipc_test tests/cuda_ipc_test.cu)
        target_link_libraries(cuda_ipc_test PRIVATE gpumemd_cuda)
        set_target_properties(cuda_ipc_test PROPERTIES CUDA_STANDARD 20
                              CUDA_STANDARD_REQUIRED YES)
        add_test(NAME cuda_ipc COMMAND cuda_ipc_test)
        set_tests_properties(cuda_ipc PROPERTIES TIMEOUT 10)
    endif()

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

    add_executable(cli_framing_test tests/cli_framing_test.cpp)
    target_link_libraries(cli_framing_test PRIVATE Threads::Threads)
    if(CMAKE_CXX_COMPILER_ID MATCHES "Clang|GNU")
        target_compile_options(cli_framing_test PRIVATE -Wall -Wextra -Wpedantic)
    endif()
    add_test(NAME cli_framing
        COMMAND cli_framing_test $<TARGET_FILE:gpumemctl>)
    set_tests_properties(cli_framing PROPERTIES TIMEOUT 10)

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
