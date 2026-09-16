# Verify public CLI behavior without a third-party test dependency.
if(SCENARIO STREQUAL "client_help")
    set(program "${GPUMEMCTL}")
    set(arguments --help)
    set(expected_result 0)
    set(expected_outputs
        "Usage: gpumemctl"
        "register NAME SIZE METADATA"
        "unregister NAME"
        "retain NAME"
        "release_model NAME"
        "models"
    )
    set(expected_lines
        "  load NAME"
        "  unload NAME"
        "  residency"
    )
elseif(SCENARIO STREQUAL "help")
    set(program "${GPUMEMD}")
    set(arguments --help)
    set(expected_result 0)
    set(expected_output "Usage: gpumemd --memory SIZE")
elseif(SCENARIO STREQUAL "no_arguments")
    set(arguments)
    set(expected_result 2)
    set(expected_output "invalid or missing memory/socket option")
elseif(SCENARIO STREQUAL "unsupported_arguments")
    set(arguments --unsupported)
    set(expected_result 2)
    set(expected_output "expected --memory SIZE and --socket PATH")
else()
    message(FATAL_ERROR "Unknown CLI test scenario: ${SCENARIO}")
endif()

if(NOT program)
    set(program "${GPUMEMD}")
endif()

execute_process(
    COMMAND "${program}" ${arguments}
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

if(expected_outputs)
    foreach(expected_output IN LISTS expected_outputs)
        string(FIND "${actual_output}" "${expected_output}" match_position)
        if(match_position EQUAL -1)
            message(FATAL_ERROR
                "${SCENARIO}: missing '${expected_output}' in '${actual_output}'")
        endif()
    endforeach()
else()
    string(FIND "${actual_output}" "${expected_output}" match_position)
    if(match_position EQUAL -1)
        message(FATAL_ERROR
            "${SCENARIO}: missing '${expected_output}' in '${actual_output}'")
    endif()
endif()

if(expected_lines)
    string(REPLACE "\n" ";" actual_lines "${actual_output}")
    foreach(expected_line IN LISTS expected_lines)
        list(FIND actual_lines "${expected_line}" match_position)
        if(match_position EQUAL -1)
            message(FATAL_ERROR
                "${SCENARIO}: missing complete line '${expected_line}' in '${actual_output}'")
        endif()
    endforeach()
endif()
