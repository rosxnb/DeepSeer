cmake_minimum_required(VERSION 4.3)
project(DeepSeer
    VERSION 0.1.0
    LANGUAGES CXX
    DESCRIPTION "MITM proxy capable of traffic inspection and payload classification"
)

# --- Options ---
option(DEEPSEER_BUILD_TESTS  "Build test suite"                ON)
option(DEEPSEER_ENABLE_ASAN  "Enable AddressSanitizer + UBSan" OFF)
option(DEEPSEER_ENABLE_TSAN  "Enable ThreadSanitizer"          OFF)

# --- CMake modules ---
list(APPEND CMAKE_MODULE_PATH "${CMAKE_SOURCE_DIR}/cmake")
include(CompilerOptions)
include(Dependencies)

# --- Symlink compile_commands.json to build/ for clangd ---
# Whichever preset you configure last wins. clangd reads from build/.
if(CMAKE_EXPORT_COMPILE_COMMANDS)
    file(CREATE_LINK
        "${CMAKE_BINARY_DIR}/compile_commands.json"
        "${CMAKE_SOURCE_DIR}/build/compile_commands.json"
        SYMBOLIC
    )
endif()

# --- Core library ---
set(CORE_LIB DeepSeerLib)
add_library(${CORE_LIB} STATIC
    src/Config/Config.cpp
)

# Platform-specific event loop
if(APPLE OR CMAKE_SYSTEM_NAME MATCHES "BSD")
    target_sources(${CORE_LIB} PRIVATE src/Event/KqueueLoop.cpp)
elseif(UNIX)
    # TODO: src/Event/EpollLoop.cpp
elseif(WIN32)
    # TODO: src/Event/IocpLoop.cpp
endif()

target_include_directories(${CORE_LIB}
    PUBLIC  "${CMAKE_SOURCE_DIR}/include"
)

target_link_libraries(${CORE_LIB}
    PUBLIC
        Threads::Threads
)

deepseer_set_compiler_options(${CORE_LIB})
deepseer_enable_sanitizers(${CORE_LIB})

# --- Executable ---
add_executable(DeepSeer src/main.cpp)
target_link_libraries(DeepSeer PRIVATE ${CORE_LIB})
deepseer_set_compiler_options(DeepSeer)
deepseer_enable_sanitizers(DeepSeer)

# --- Tests ---
if(DEEPSEER_BUILD_TESTS)
    enable_testing()
    add_subdirectory(test)
endif()
