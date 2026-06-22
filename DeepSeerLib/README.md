## Project structure

```
include/DeepSeer/
  Core/             Buffer, types, error handling
  Event/            Platform-abstracted event loop (kqueue/epoll)
  Http/             HTTP codec (llhttp-based), message types, header map
  Log/              Logger with pluggable sinks (console, file)
  Net/              Socket, Address, Listener, Connection
  Proxy/            ProxySession — per-connection proxy logic
  Server/           Top-level server, accept loop
  Tls/              TLS interception: CertGenerator, CertCache, TlsConnection
src/                Implementation files (mirrors DeepSeer/include/ layout)
test/               GoogleTest test suites
```
