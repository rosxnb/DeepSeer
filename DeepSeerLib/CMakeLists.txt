# DeepSeerLib - Networking library for DeepSeer

set(NET_LIB DeepSeerLib)

add_library(${NET_LIB} STATIC
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
    target_sources(${NET_LIB} PRIVATE src/Event/KqueueLoop.cpp)
elseif(UNIX)
    # TODO: src/Event/EpollLoop.cpp
elseif(WIN32)
    # TODO: src/Event/IocpLoop.cpp
endif()

target_include_directories(${NET_LIB}
    PUBLIC  "${CMAKE_CURRENT_SOURCE_DIR}/include"
)

target_link_libraries(${NET_LIB}
    PUBLIC
        OpenSSL::SSL
        OpenSSL::Crypto
        Threads::Threads
        llhttp_static
)

deepseer_set_compiler_options(${NET_LIB})
deepseer_enable_sanitizers(${NET_LIB})

if(DEEPSEER_BUILD_TESTS)
    add_subdirectory(test)
endif()
