# Overview
This is a collection of samples for using C++20 coroutines with ASIO.

# Development Devcontainer
This project is prepared to run in a [Visual Studio Code Development Container](https://code.visualstudio.com/docs/devcontainers/containers). It also runs in [GitHub Codespaces](https://github.com/features/codespaces). The container image is based on [CPP Devcontainer](https://github.com/pgit/cpp-devcontainer), which contains recent LLVM and Boost versions.

After opening the project in the container, press `F7` to compile using CMake. On first startup, the CMake Plugin will ask you for a kit, select `[Unspecified]` or `clang` (the latter may take some time to appear, when scanning has finished).

After that, you can open a `*.cpp` file. This workspace is configured to use the [clangd](https://marketplace.visualstudio.com/items?itemName=llvm-vs-code-extensions.vscode-clangd) plugin for syntax highlighting and indexing. That may take a while to complete, watch the `indexing 1/20` in the status bar. You may have to run the command `clangd: Restart language server` once for it to pick up on changes to `build/compile_commands.json`.

# Echo Servers
There are several implementations of TCP echo servers in this project, for demonstration purposes.

```bash
cmake --build build --target all -- && build/slides/echo_coro
```

To test, use `socat`:
```bash
socat STDIN TCP-CONNECT:[::1]:55555
socat STDIN,raw,echo=0,icrnl,crlf,escape=0x03 TCP:[::1]:55555
```

To test echo speed, you can use `netcat` like this:

```bash
dd if=/dev/zero bs=1M count=512|nc -N localhost 55555|dd of=/dev/null
```

To simulate multiple clients:

```bash
for ((i=0; i<5; ++i))
do
   dd if=/dev/zero bs=1M count=512 | nc -N localhost 55555 >/dev/null&
done; time wait
```

# Testcases
A few examples are implement as Google Test units `test/test_*.cpp`. This way, we can easily run and debug them in Visual Studio Code, using [C++ TestMate](https://marketplace.visualstudio.com/items?itemName=matepek.vscode-catch2-test-adapter).

## Manual Testing

Example: 5 clients, each sending 512 MiB of data to a echo server listening on `[::1]:55555`:

# Resources

## Examples
* [Boost ASIO C++20 Examples](https://www.boost.org/doc/libs/1_88_0/doc/html/boost_asio/examples/cpp20_examples.html)


## YT Watchlist
### Using Asio with C++20 Coroutines
* [Cancellations in Asio: a tale of coroutines and timeouts - Rubén Pérez Hidalgo](https://www.youtube.com/watch?v=80Zs0WbXAMY)
* [Asynchrony with ASIO and coroutines - Andrzej Krzemieński - code::dive 2022](https://www.youtube.com/watch?v=0i_pFZSijZc&t=2789s)
* [C++20 Coroutines, with Boost ASIO in production: Frightening but awesome](https://www.youtube.com/watch?v=RBldGKfLb9I&t=2375s)
### Chris Kohloff
* [Talking Async Ep1: Why C++20 is the Awesomest Language for Network Programming](https://www.youtube.com/watch?v=icgnqFM-aY4&t=2477s)
* [Talking Async Ep2: Cancellation in depth](https://www.youtube.com/watch?v=hHk5OXlKVFg&t=3718s)
