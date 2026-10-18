# UDP Message Router for Resource-Constrained Embedded Systems

A POSIX C process receives fixed-format UDP messages and routes them to one of two application threads. Runtime message and queue storage is fixed-size; the program makes no heap allocations.

## Requirements

- GCC, GNU Make, POSIX threads, and Python 3 for the automated checks.
- Linux, WSL, or another POSIX system. VS Code can open this folder; use its integrated terminal.

## Build, run, and check

```sh
make
./router
```

The router listens on UDP port **9876** on all local IPv4 interfaces. Press `Ctrl+C` to stop it cleanly. `make clean` removes executables and object files.

`make test` builds the router and sender, starts a local router process, sends all five acceptance cases, and checks captured router stdout and stderr. It requires UDP port 9876 to be free.

On Windows, use WSL with GCC and GNU Make installed; the program uses POSIX sockets and pthreads and does not build natively with MSVC.

## Message format

Each datagram must be exactly 1,032 bytes: 258 consecutive unsigned 32-bit integers in network byte order (big endian).

| Byte offset | Field | Meaning |
| --- | --- | --- |
| 0 | `app_id` | `1` routes to Application 1; `2` routes to Application 2 |
| 4 | `command_id` | Application command identifier |
| 8 through 1031 | `param[256]` | 256 unsigned 32-bit parameters |

Datagrams with another length or unsupported `app_id` are discarded and reported to `stderr`. Application threads print the app ID, command ID, and all parameters to `stdout`.

For example, this Python snippet sends one valid message to Application 1:

```sh
python3 -c 'import socket,struct; s=socket.socket(socket.AF_INET,socket.SOCK_DGRAM); s.sendto(struct.pack("!II256I",1,42,*range(256)),("127.0.0.1",9876))'
```

## Architecture and queue behavior

- One router thread owns the UDP socket, validates datagrams, applies rate limiting, and dispatches messages.
- Two application threads consume their own queues.
- Each queue is a statically allocated circular buffer with capacity 8, protected by a mutex and condition variables. If a queue fills, the router waits for space instead of allocating memory or overwriting a message.
- The router accepts at most one supported, valid message per one-second interval, measured with `CLOCK_MONOTONIC`. Rate-limited datagrams are discarded and reported to `stderr`; they do not restart the interval.
- `SIGINT` requests shutdown. The receive timeout lets the router notice it promptly; queued messages are drained before the application threads exit.

## Coding rules

- Use fixed-width integer types for protocol fields and explicitly convert wire integers from network byte order.
- Keep runtime storage static or on the stack; do not allocate from the heap.
- Check relevant POSIX API results, report recoverable input and socket errors, and release initialized resources on shutdown.
- Keep queue state behind its mutex and use condition variables for producer/consumer waits.

`message.h` is the shared protocol definition. `sender.c` creates network-byte-order test datagrams and checks socket, address conversion, sleep, and close results. `test_router.py` validates captured router output; the sender alone generates traffic and does not perform assertions.

These rules are MISRA-C inspired, but this POSIX program is not claimed as formally MISRA-C compliant. A compliance claim requires a selected MISRA edition, a project deviation policy, and static analysis of the complete target toolchain and operating-system interfaces.
