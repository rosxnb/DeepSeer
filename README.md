# DeepSeer

DeepSeer is a MITM proxy with deep packet inspection (DPI) capabilities. It intercepts HTTP traffic, inspects requests/responses, and forwards them transparently.

## Prerequisites

- C++23 compiler (Clang 18+ or GCC 15+)
- CMake 4.3+
- Ninja
- OpenSSL 3.0+

Dependencies fetched automatically via CMake FetchContent:
- [llhttp](https://github.com/nodejs/llhttp) (HTTP/1.1 parser)
- [GoogleTest](https://github.com/google/googletest) (testing)

## Build

```bash
# Configure and build (debug)
cmake --preset debug
cmake --build --preset debug

# Release build
cmake --preset release
cmake --build --preset release
```

### Build presets

| Preset           | Description                                |
|------------------|--------------------------------------------|
| `debug`          | Debug build with tests enabled             |
| `release`        | Optimized release build, no tests          |
| `relwithdebinfo` | Release with debug symbols                 |
| `asan`           | AddressSanitizer + UBSan                   |
| `tsan`           | ThreadSanitizer                            |

## Run

```bash
./build/debug/DeepSeer
```

The proxy listens on `0.0.0.0:8080` by default. Test with curl:

```bash
# HTTP proxy
curl -v http://example.com --proxy "127.0.0.1:8080"

# HTTPS tunnel (CONNECT)
curl -v https://example.com --proxy "127.0.0.1:8080"
```

Stop the server with `Ctrl+C` (SIGINT) or `SIGTERM`.

## Test

```bash
# Run all tests
ctest --preset debug

# Run a specific test binary
./build/debug/test/TestConnection
./build/debug/test/TestEventLoop
./build/debug/test/TestBuffer
./build/debug/test/TestLogger
./build/debug/test/TestHttp1Codec
```

## Project structure

```
include/DeepSeer/
  Core/       Buffer, types, error handling
  Event/      Platform-abstracted event loop (kqueue/epoll)
  Http/       HTTP codec (llhttp-based), message types, header map
  Log/        Logger with pluggable sinks (console, file)
  Net/        Socket, Address, Listener, Connection
  Proxy/      ProxySession — per-connection proxy logic
  Server/     Top-level server, accept loop
src/          Implementation files (mirrors include/ layout)
test/         GoogleTest test suites
```
