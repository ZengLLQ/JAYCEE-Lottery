cmake_minimum_required(VERSION 3.20)

project(JAYCEELottery VERSION 2.1.0 LANGUAGES CXX RC)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

add_executable(JAYCEELottery WIN32
    src/main.cpp
    resources/app.rc
)

target_compile_definitions(JAYCEELottery PRIVATE
    UNICODE
    _UNICODE
    WINVER=0x0A00
    _WIN32_WINNT=0x0A00
    WIN32_LEAN_AND_MEAN
    NOMINMAX
)

target_link_libraries(JAYCEELottery PRIVATE
    d2d1
    dwrite
    dwmapi
    bcrypt
    ole32
    shell32
    comdlg32
    winmm
    uuid
)

if(MSVC)
    set_property(TARGET JAYCEELottery PROPERTY
        MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
    target_compile_options(JAYCEELottery PRIVATE /W4 /permissive- /utf-8)
else()
    target_compile_options(JAYCEELottery PRIVATE -Wall -Wextra -Wpedantic -O2)
    target_link_options(JAYCEELottery PRIVATE
        -municode
        -static
        -static-libgcc
        -static-libstdc++
        $<$<CONFIG:Release>:-s>
    )
endif()

set_target_properties(JAYCEELottery PROPERTIES
    OUTPUT_NAME "JAYCEE Lottery"
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin"
)

file(TO_CMAKE_PATH "${CMAKE_BINARY_DIR}/bin/JAYCEE Lottery.exe" APP_PAYLOAD)
file(TO_CMAKE_PATH "${CMAKE_SOURCE_DIR}/docs/QUICK_START.md" README_PAYLOAD)
file(TO_CMAKE_PATH "${CMAKE_SOURCE_DIR}/assets/participants-template.csv" CSV_PAYLOAD)
file(TO_CMAKE_PATH "${CMAKE_SOURCE_DIR}/resources/jaycee.ico" ICON_PAYLOAD)
file(TO_CMAKE_PATH "${CMAKE_SOURCE_DIR}/installer/installer.manifest" INSTALLER_MANIFEST)
set(INSTALLER_RESOURCE "${CMAKE_BINARY_DIR}/installer_payload.rc")
configure_file(installer/installer.rc.in "${INSTALLER_RESOURCE}" @ONLY)

add_custom_command(TARGET JAYCEELottery POST_BUILD
    COMMAND "${CMAKE_COMMAND}" -E touch "${INSTALLER_RESOURCE}"
    COMMENT "Refreshing installer payload resources"
)

add_executable(JAYCEELotteryInstaller WIN32
    installer/installer.cpp
    "${INSTALLER_RESOURCE}"
)
add_dependencies(JAYCEELotteryInstaller JAYCEELottery)

target_compile_definitions(JAYCEELotteryInstaller PRIVATE
    UNICODE
    _UNICODE
    WINVER=0x0A00
    _WIN32_WINNT=0x0A00
    WIN32_LEAN_AND_MEAN
    NOMINMAX
)

target_link_libraries(JAYCEELotteryInstaller PRIVATE
    shell32
    ole32
    uuid
    advapi32
    dwmapi
)

if(MSVC)
    set_property(TARGET JAYCEELotteryInstaller PROPERTY
        MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
    target_compile_options(JAYCEELotteryInstaller PRIVATE /W4 /permissive- /utf-8)
else()
    target_compile_options(JAYCEELotteryInstaller PRIVATE -Wall -Wextra -Wpedantic -O2)
    target_link_options(JAYCEELotteryInstaller PRIVATE
        -municode
        -static
        -static-libgcc
        -static-libstdc++
        $<$<CONFIG:Release>:-s>
    )
endif()

set_target_properties(JAYCEELotteryInstaller PROPERTIES
    OUTPUT_NAME "JAYCEE-Lottery-Setup"
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin"
)
