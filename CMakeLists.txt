cmake_minimum_required(VERSION 4.3)
project(DeepSeer
    VERSION 0.1.0
    LANGUAGES CXX
    DESCRIPTION "MITM proxy capable of traffic inspection and payload classification"
)

# --- Options ---
option(DEEPSEER_BUILD_TESTS  "Build test suite"                ON)
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
    src/Core/Buffer.cpp
    src/Event/EventLoop.cpp
    src/Http/Http1/Codec.cpp
    src/Log/ConsoleSink.cpp
    src/Log/FileSink.cpp
    src/Log/Logger.cpp
    src/Net/Address.cpp
    src/Net/Connection.cpp
    src/Net/Listener.cpp
    src/Net/Socket.cpp
    src/Proxy/ProxySession.cpp
    src/Server/Server.cpp
    src/Tls/CertCache.cpp
    src/Tls/CertGenerator.cpp
    src/Tls/TlsConnection.cpp
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
        OpenSSL::SSL
        OpenSSL::Crypto
        Threads::Threads
        llhttp_static
)

deepseer_set_compiler_options(${CORE_LIB})
deepseer_enable_sanitizers(${CORE_LIB})

# --- Enable testing early so subdirectories can register tests ---
if(DEEPSEER_BUILD_TESTS)
    enable_testing()
endif()

# --- Seer AI inference library ---
if(DEEPSEER_BUILD_SEER)
    add_subdirectory(seer)
    message(STATUS "Seer AI inference library: enabled")
else()
    message(STATUS "Seer AI inference library: disabled")
endif()

# --- Executable ---
add_executable(DeepSeer src/main.cpp)
target_link_libraries(DeepSeer PRIVATE ${CORE_LIB})
if(DEEPSEER_BUILD_SEER)
    target_link_libraries(DeepSeer PRIVATE Seer)
    target_compile_definitions(DeepSeer PRIVATE DEEPSEER_HAS_SEER)
endif()
deepseer_set_compiler_options(DeepSeer)
deepseer_enable_sanitizers(DeepSeer)

# --- Tests ---
if(DEEPSEER_BUILD_TESTS)
    add_subdirectory(test)
endif()
