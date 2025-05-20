# Overview
This is a collection of samples for using C++20 coroutines with ASIO.

# Development Devcontainer
This project is prepared to run in a Visual Studio Code Development Container.

# Echo Servers
There are several implementations of TCP echo servers in this project, for speed comparison.

To test echo speed, you can use `netcat` like this:

```bash
cmake --build build --target all -- && build/async_tcp_echo_server 8080&
dd if=/dev/zero bs=1K count=1M|nc -N localhost 8080|dd of=/dev/null
```
