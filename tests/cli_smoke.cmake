# Verify public CLI behavior without a third-party test dependency.
if(SCENARIO STREQUAL "help")
    set(arguments --help)
    set(expected_result 0)
    set(expected_output "Usage: gpumemd --help")
elseif(SCENARIO STREQUAL "no_arguments")
    set(arguments)
    set(expected_result 1)
    set(expected_output "broker startup is not implemented")
elseif(SCENARIO STREQUAL "unsupported_arguments")
    set(arguments --memory 16GB)
    set(expected_result 2)
    set(expected_output "unsupported arguments")
else()
    message(FATAL_ERROR "Unknown CLI test scenario: ${SCENARIO}")
endif()

execute_process(
    COMMAND "${GPUMEMD}" ${arguments}
    RESULT_VARIABLE actual_result
    OUTPUT_VARIABLE stdout
    ERROR_VARIABLE stderr
    TIMEOUT 3
)

if(NOT "${actual_result}" STREQUAL "${expected_result}")
    message(FATAL_ERROR
        "${SCENARIO}: expected exit ${expected_result}, got ${actual_result}\n"
        "stdout: ${stdout}\nstderr: ${stderr}")
endif()

if(expected_result EQUAL 0)
    set(actual_output "${stdout}")
    if(NOT stderr STREQUAL "")
        message(FATAL_ERROR "Successful help wrote to stderr: ${stderr}")
    endif()
else()
    set(actual_output "${stderr}")
    if(NOT stdout STREQUAL "")
        message(FATAL_ERROR "Failed startup wrote to stdout: ${stdout}")
    endif()
endif()

string(FIND "${actual_output}" "${expected_output}" match_position)
if(match_position EQUAL -1)
    message(FATAL_ERROR
        "${SCENARIO}: missing '${expected_output}' in '${actual_output}'")
endif()
