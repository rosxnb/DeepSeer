# DeepSeer

DeepSeer is a MITM proxy with deep packet inspection (DPI) capabilities. It intercepts HTTP and HTTPS traffic, inspects requests/responses, and forwards them transparently. For HTTPS, it performs TLS interception by dynamically generating per-host certificates signed by a local CA.

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

### HTTP-only (no TLS interception)

```bash
./build/debug/DeepSeer
```

HTTPS CONNECT requests are tunneled as opaque TCP — the proxy cannot inspect the encrypted traffic.

### With TLS interception (MITM)

Generate a local CA certificate:

```bash
./tools/gen_ca.sh <out-dir>
```

This creates `ca.crt` and `ca.key` in the output directory. Trust the CA on your system so browsers/curl accept the forged certificates:

```bash
# macOS
sudo security add-trusted-cert -d -r trustRoot \
    -k /Library/Keychains/System.keychain ca.crt

# Linux (Debian/Ubuntu)
sudo cp ca.crt /usr/local/share/ca-certificates/deepseer-ca.crt
sudo update-ca-certificates
```

Start the proxy with the CA:

```bash
./build/debug/DeepSeer --ca-cert ca.crt --ca-key ca.key
```

### CLI options

| Flag        | Description                     | Default |
|-------------|---------------------------------|---------|
| `--port N`  | Listen port                     | 8080    |
| `--ca-cert` | Path to CA certificate PEM      | (none)  |
| `--ca-key`  | Path to CA private key PEM      | (none)  |
| `--version` | Print version and exit          |         |

### Testing with curl

```bash
# HTTP proxy
curl -v http://example.com --proxy "127.0.0.1:8080"

# HTTPS with MITM (requires CA trust or --cacert)
curl -v https://example.com --proxy "127.0.0.1:8080" --cacert ca.crt

# HTTPS tunnel (no MITM, when CA not provided)
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
  Tls/        TLS interception: CertGenerator, CertCache, TlsConnection
src/          Implementation files (mirrors include/ layout)
test/         GoogleTest test suites
tools/        Helper scripts (gen_ca.sh)
```
