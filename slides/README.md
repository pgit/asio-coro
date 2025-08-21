# Slides Directory Binaries

This directory contains example programs demonstrating different approaches to asynchronous I/O and coroutines using Boost.Asio. Each C++ source file compiles to an executable with the same name.

## Echo Server Examples

These examples implement TCP echo servers using different programming paradigms:

### `echo_sync`
Synchronous echo server using blocking I/O operations. Demonstrates traditional blocking socket programming where each operation waits for completion before proceeding.

### `echo_async`
Asynchronous echo server using callback-based programming. Uses `async_read_some` and `async_write` with completion handlers to handle multiple connections without blocking.

### `echo_coro`
Coroutine-based echo server using `co_await` syntax. Demonstrates how coroutines can make asynchronous code look like synchronous code while maintaining non-blocking behavior.

### `echo_coro_threaded`
Multi-threaded coroutine echo server. Spawns multiple threads that all run the same IO context, allowing parallel processing of connections.

### `echo_coro_timeout`
Coroutine echo server with timeout handling. Uses `cancel_after()` to set timeouts on operations (2 seconds for reads, 60 seconds for sessions).

### `echo_coro_tuple`
Coroutine echo server using tuple-based error handling. Uses `as_tuple` to return both error codes and results as tuples instead of throwing exceptions.

### `echo_coro_context_pool`
Coroutine echo server with a context pool for load distribution. Creates multiple IO contexts running on separate threads and distributes incoming connections among them.

### `echo_sync_threaded`
Multi-threaded synchronous echo server. Uses traditional blocking I/O but spawns a new thread for each connection to handle multiple clients concurrently.

## Process Execution Examples

These examples demonstrate how to execute external processes and handle their output:

### `process`
Basic process execution with stdout logging. Runs `/usr/bin/ping ::1 -c 5 -i 0.1` and logs its output with colored formatting.

### `process_signal`
Process execution with signal handling capabilities. Similar to the basic process example but includes infrastructure for handling signals during process execution.

### `process_timeout`
Process execution with timeout handling. Runs `/usr/bin/ping ::1 -c 5 -i 0.1` with a 250ms timeout, demonstrating how to interrupt long-running processes.

## Building

All binaries are built using CMake. The `CMakeLists.txt` automatically creates an executable for each `.cpp` file in this directory.

## Purpose

These examples are designed to illustrate the progression from traditional synchronous programming to modern asynchronous coroutine-based programming, showing the benefits and trade-offs of each approach.