# ASIO & CORO
This is a collection of samples for using C++20 coroutines with [Boost ASIO](https://www.boost.org/libs/asio/).   [![Build and run tests](https://github.com/pgit/asio-coro/actions/workflows/build-and-test.yml/badge.svg)](https://github.com/pgit/asio-coro/actions/workflows/build-and-test.yml)

## Slides

![async](doc/slides-small.jpg)\
[ASIO & CORO - Part I - From Sync to Async to As-Sync.pdf](doc/part1.pdf)

## Development Devcontainer
This project is prepared to run in a [Visual Studio Code Development Container](https://code.visualstudio.com/docs/devcontainers/containers). It also runs in [GitHub Codespaces](https://github.com/features/codespaces). The container image is based on [CPP Devcontainer](https://github.com/pgit/cpp-devcontainer), which contains recent LLVM and Boost versions.

After opening the project in the container, press `F7` to compile using CMake. On first startup, the CMake Plugin will ask you for a kit, select `[Unspecified]` or `clang` (the latter may take some time to appear, when scanning has finished).

After that, you can open a `*.cpp` file. This workspace is configured to use the [clangd](https://marketplace.visualstudio.com/items?itemName=llvm-vs-code-extensions.vscode-clangd) plugin for syntax highlighting and indexing. That may take a while to complete, watch the `indexing 1/20` in the status bar. You may have to run the command `clangd: Restart language server` once for it to pick up on changes to `build/compile_commands.json`.

## Echo Servers
There are several implementations of TCP echo servers in this project, for demonstration purposes.

```bash
cmake --build build && build/echo/echo_coro
```

Test client:
```bash
cmake --build build && build/bin/client -c 10
```

To test interactively, use `socat`:
```bash
# with line buffering
socat STDIN TCP-CONNECT:[::1]:55555

# raw
socat STDIN,raw,echo=0,icrnl,escape=0x03 TCP:[::1]:55555
```

# Testcases
A lot of examples are implement as Google Test units `test/test_*.cpp`. This way, we can easily run and debug them in Visual Studio Code, using [C++ TestMate](https://marketplace.visualstudio.com/items?itemName=matepek.vscode-catch2-test-adapter).

# Resources

See [doc/resources.md](doc/resources.md).
