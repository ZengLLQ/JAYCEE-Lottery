foreach(required IN ITEMS INSTALLER APP OUTPUT_DIR)
    if(NOT DEFINED ${required} OR "${${required}}" STREQUAL "")
        message(FATAL_ERROR "${required} is required")
    endif()
endforeach()

file(REMOVE_RECURSE "${OUTPUT_DIR}")
file(MAKE_DIRECTORY "${OUTPUT_DIR}")

execute_process(
    COMMAND "${INSTALLER}" --extract-test "${OUTPUT_DIR}"
    RESULT_VARIABLE extract_result
)
if(NOT extract_result EQUAL 0)
    message(FATAL_ERROR "Installer extraction failed with exit code ${extract_result}")
endif()

set(extracted_app "${OUTPUT_DIR}/JAYCEE Lottery.exe")
set(extracted_readme "${OUTPUT_DIR}/README.md")
set(extracted_template "${OUTPUT_DIR}/Templates/participants-template.csv")
foreach(payload IN ITEMS "${extracted_app}" "${extracted_readme}" "${extracted_template}")
    if(NOT EXISTS "${payload}")
        message(FATAL_ERROR "Missing installer payload: ${payload}")
    endif()
endforeach()

file(SHA256 "${APP}" source_hash)
file(SHA256 "${extracted_app}" extracted_hash)
if(NOT source_hash STREQUAL extracted_hash)
    message(FATAL_ERROR "Installer application payload does not match the release executable")
endif()
