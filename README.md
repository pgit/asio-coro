# Overview
This is a collection of samples for using C++20 coroutines with ASIO.

# Development Devcontainer
This project is prepared to run in a [Visual Studio Code Development Container](https://code.visualstudio.com/docs/devcontainers/containers). It also runs in a [GitHub Codespace](https://github.com/features/codespaces).

After opening, press `F7` to compile using CMake. On first startup, the CMake Plugin will ask you for a kit, select `[Unspecified]` or `clang` (the latter may take some time to appear, when scanning has finished).

After that, you can open a `*.cpp` file. This workspace is configured to use the [clangd](https://marketplace.visualstudio.com/items?itemName=llvm-vs-code-extensions.vscode-clangd) plugin for syntax highlighting and indexing. That may take a while to complete, watch the `indexìng 1/20` in the status bar. You may have to run the command `clangd: Restart language server` once for it to pick up on changes to `build/compile_commands.json`.

# Echo Servers
There are several implementations of TCP echo servers in this project, for demonstraction purposes.

```bash
cmake --build build --target all -- && build/slides/echo_coro
```

To test, use `socat`:
```bash
socat STDIN TCP-CONNECT:[::1]:55555
```

To test echo speed, you can use `netcat` like this:

```bash
dd if=/dev/zero bs=1K count=1M|nc -N localhost 55555|dd of=/dev/null
```

# Testcases
A few examples are implement as Google Test units `test/test_*.cpp`. This way, we can easily run and debug them in Visual Studio Code, using [C++ TestMate](https://marketplace.visualstudio.com/items?itemName=matepek.vscode-catch2-test-adapter).