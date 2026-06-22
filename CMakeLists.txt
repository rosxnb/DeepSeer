cmake_minimum_required(VERSION 4.3)
project(DeepSeer
    VERSION 0.1.0
    LANGUAGES CXX
    DESCRIPTION "DeepSeer -- a proxy capable of Layer 7 payload reconstruction"
)

# --- Options ---
option(DEEPSEER_BUILD_TESTS  "Build test suite"                 ON)
option(DEEPSEER_BUILD_SEER   "Build Seer AI inference library"  ON)
option(DEEPSEER_ENABLE_ASAN  "Enable AddressSanitizer + UBSan" OFF)
option(DEEPSEER_ENABLE_TSAN  "Enable ThreadSanitizer"          OFF)


# --- Platform Detection ---
if(APPLE)
    add_compile_definitions(PLATFORM_MACOS)
    message(STATUS "Platform: macOS")
elseif(UNIX)
    add_compile_definitions(PLATFORM_LINUX)
    message(STATUS "Platform: Linux")
elseif(WIN32)
    add_compile_definitions(PLATFORM_WINDOWS)
    message(STATUS "Platform: Windows")
endif()

# --- Output directories ---
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin")
set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/lib")
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/lib")

# --- CMake modules ---
list(APPEND CMAKE_MODULE_PATH "${CMAKE_SOURCE_DIR}/cmake")
include(CompilerOptions)
include(Dependencies)

# --- Symlink compile_commands.json to build/ for clangd ---
if(CMAKE_EXPORT_COMPILE_COMMANDS)
    file(CREATE_LINK
        "${CMAKE_BINARY_DIR}/compile_commands.json"
        "${CMAKE_SOURCE_DIR}/build/compile_commands.json"
        SYMBOLIC
    )
endif()

# --- Enable testing early so subdirectories can register tests ---
if(DEEPSEER_BUILD_TESTS)
    enable_testing()
endif()

# --- Network Libarary ---
add_subdirectory(DeepSeerLib)

# --- Seer AI inference library ---
if(DEEPSEER_BUILD_SEER)
    add_subdirectory(SeerLib)
    message(STATUS "Seer AI inference library: enabled")
else()
    message(STATUS "Seer AI inference library: disabled")
endif()

# --- Executable ---
add_executable(DeepSeer Source/main.cpp)
target_link_libraries(DeepSeer PRIVATE DeepSeerLib)
if(DEEPSEER_BUILD_SEER)
    target_link_libraries(DeepSeer PRIVATE SeerLib)
    target_compile_definitions(DeepSeer PRIVATE DEEPSEER_HAS_SEER)
endif()
deepseer_set_compiler_options(DeepSeer)
deepseer_enable_sanitizers(DeepSeer)
