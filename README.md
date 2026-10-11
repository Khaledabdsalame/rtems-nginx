# RTEMS Nginx

A lightweight event-driven HTTP server for RTEMS 7 and ARM-based embedded systems.

The project serves static files over HTTP using non-blocking sockets, a `poll()`-based event loop, and RTEMS worker tasks.

## Features

- Non-blocking client sockets
- Event-driven I/O using level-triggered `poll()`
- Epoll-like event API adapted for RTEMS
- RTEMS worker-task architecture
- Automatic CPU-core detection
- Optional CPU affinity on SMP builds
- Static file serving from `/www`
- Basic HTTP/1.x `GET` support
- MIME type detection
- URL percent-decoding
- `TCP_NODELAY` and `SO_REUSEADDR`
- Directory traversal protection
- Nginx-style logging
- Graceful shutdown support when POSIX signals are available

## Architecture

```text
RTEMS Init Task
      |
      v
Master Task
  - Initializes networking
  - Creates the listening socket
  - Detects available CPUs
  - Creates worker tasks
      |
      +---- Worker 1 ----+
      +---- Worker 2 ----+---- Shared listening socket
      +---- Worker N ----+

Each worker runs an independent poll() event loop and handles multiple clients.
```

The request lifecycle is:

```text
Accept connection
      -> Read request
      -> Parse HTTP method, path, and version
      -> Resolve and validate file path
      -> Send headers and file data
      -> Close connection
```

## Important Implementation Note

This project does not use native Linux `epoll`. RTEMS uses a custom event abstraction backed by `poll()` because `poll()` is the available portable mechanism in this environment.

The event system is level-triggered by default.

## HTTP Support

Supported:

- `GET` requests
- HTTP/1.0 and HTTP/1.1 request versions
- Static file responses
- `Connection: close`

Response codes include:

- `200 OK`
- `400 Bad Request`
- `404 Not Found`
- `501 Not Implemented`
- `505 HTTP Version Not Supported`

This is a small embedded HTTP server, not a complete general-purpose HTTP implementation.

## Static Files

The document root is:

```text
/www
```

The default startup code creates:

```text
/www/index.html
```

Requested paths are validated with `realpath()` and checked to ensure they remain inside the document root. Only regular files are served.

## MIME Types

The server detects MIME types from file extensions, including:

- HTML: `text/html`
- CSS: `text/css`
- JavaScript: `application/javascript`
- JSON: `application/json`
- Plain text: `text/plain`
- JPEG, PNG, GIF, SVG, and ICO images
- WOFF, WOFF2, and TTF fonts

Unknown extensions use:

```text
application/octet-stream
```

## Default Network Configuration

The sample RTEMS initialization configures the `cgem0` interface with:

```text
Address:  169.254.1.2
Netmask:  255.255.0.0
Port:     8080
```

The server listens on all interfaces at:

```text
0.0.0.0:8080
```

These values can be changed in `src/init.c`.

## Requirements

- RTEMS 7
- An installed RTEMS toolchain
- A supported RTEMS BSP
- RTEMS BSD networking
- Waf
- QEMU, when running the provided ARM emulation setup

The included build configuration targets RTEMS 7. Existing build artifacts include:

```text
arm-rtems7-xilinx_zynq_a9_qemu
```

## Build

Configure the project for your BSP:

```bash
./waf configure \
  --rtems=/path/to/rtems/7 \
  --rtems-bsp=arm/xilinx_zynq_a9_qemu
```

Build the application:

```bash
./waf build
```

The generated executable is placed in the BSP-specific build directory, for example:

```text
build/arm-rtems7-xilinx_zynq_a9_qemu/hello.exe
```

The output is named `hello.exe` because that is the target name configured in `wscript`.

## Run with QEMU

A typical QEMU command is:

```bash
qemu-system-arm \
  -serial null \
  -serial mon:stdio \
  -nographic \
  -M xilinx-zynq-a9 \
  -m 256M \
  -net nic,model=cadence_gem \
  -net tap,ifname=qtap,script=no,downscript=no \
  -kernel build/arm-rtems7-xilinx_zynq_a9_qemu/hello.exe
```

Configure the host TAP interface before starting QEMU:

```bash
sudo ip addr add 169.254.1.1/16 dev qtap
sudo ip link set qtap up
```

The server should then be available at:

```text
http://169.254.1.2:8080/
```

Test it with:

```bash
curl -i http://169.254.1.2:8080/
```

## Project Structure

```text
.
├── src/
│   ├── client.c       Client initialization
│   ├── client.h       Client data structures
│   ├── event.c        poll()-based event implementation
│   ├── event.h        Event system API
│   ├── init.c         RTEMS startup and network setup
│   ├── master.c       Master and worker-task management
│   ├── master.h       Master process definitions
│   ├── server.c       Listening socket management
│   ├── server.h       Server definitions
│   ├── worker.c       Event loop and HTTP handling
│   ├── worker.h       Worker API
│   └── dbg.h          Debugging helpers
├── test/              RTEMS test sources
├── rtems_waf/         RTEMS Waf integration
├── waf                 Waf executable
├── wscript             Build configuration
└── README.md
```

## Worker Management

The master task detects the number of online processors using:

```c
sysconf(_SC_NPROCESSORS_ONLN)
```

It creates up to `MAX_WORKERS_COUNT` worker tasks. The current maximum is defined in `src/master.h`:

```c
#define MAX_WORKERS_COUNT 64
```

When built with RTEMS SMP support, workers can be assigned to specific CPU cores using RTEMS affinity APIs.

## Logging

Logs use the following format:

```text
YYYY/MM/DD HH:MM:SS [level] message
```

Example:

```text
2026/03/27 14:23:45 [notice] master process starting
2026/03/27 14:23:45 [notice] listening on port 8080
2026/03/27 14:23:46 [info] client connection accepted
2026/03/27 14:23:47 [notice] "GET /index.html HTTP/1.1" 200 25
```

Available levels include `notice`, `info`, and `error`.

## Limitations

- No HTTPS or TLS support
- No dynamic content or CGI
- Only `GET` requests are implemented
- No persistent keep-alive connections
- No chunked transfer encoding
- No range requests
- No compression
- Static files are sent with buffered `read()` and `send()` calls rather than `sendfile()`
- Runtime behavior depends on the selected RTEMS BSP and network configuration

## License

This project is licensed under the MIT License.

See the source files for the complete license text.
