cmake_minimum_required(VERSION 3.20)
project(GLanglight LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# ── Sources ────────────────────────────────────────────────────────────────
set(SOURCES
    src/main.cpp
    src/tray.cpp
    src/lang_detector.cpp
    src/logi_led.cpp
    resources/app.rc           # compiled by CMake's RC compiler
)

# ── WIN32 GUI executable (no console window) ──────────────────────────────
add_executable(GLanglight WIN32 ${SOURCES})

# ── Include paths ─────────────────────────────────────────────────────────
target_include_directories(GLanglight PRIVATE
    ${CMAKE_SOURCE_DIR}/include
    ${CMAKE_SOURCE_DIR}/src
)

# ── Link the Logitech LED SDK (x64 only) ──────────────────────────────────
target_link_libraries(GLanglight PRIVATE
    ${CMAKE_SOURCE_DIR}/lib/x64/LogitechLEDLib.lib
    # Windows system libraries needed for tray / shell
    Shell32
    User32
    Gdi32
)

# ── Copy LogitechLEDLib.dll next to the binary after build ────────────────
# The DLL is provided by Logitech GHub; we don't ship it, but we note its
# expected location as a comment. If you have a local copy, uncomment:
# add_custom_command(TARGET GLanglight POST_BUILD
#     COMMAND ${CMAKE_COMMAND} -E copy_if_different
#         "${CMAKE_SOURCE_DIR}/lib/x64/LogitechLEDLib.dll"
#         "$<TARGET_FILE_DIR:GLanglight>")

# ── RC resource search path (so the compiler finds app.ico) ──────────────
set_source_files_properties(resources/app.rc PROPERTIES
    COMPILE_FLAGS "/I\"${CMAKE_SOURCE_DIR}/resources\""
)

# ── MSVC-specific hardening flags ─────────────────────────────────────────
if(MSVC)
    target_compile_options(GLanglight PRIVATE
        /W4           # high warning level
        /WX-          # warnings not errors (SDK headers have W4 warnings)
        /permissive-  # strict standard conformance
    )
    # Use Unicode Win32 APIs
    target_compile_definitions(GLanglight PRIVATE UNICODE _UNICODE)
endif()
