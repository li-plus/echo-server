# Echo Server

A simple echo server like `nc` using event-driven programming.

## Quick Start

Compile the project

```sh
mkdir -p build && cd build
cmake .. -DCMAKE_BUILD_TYPE=Release
make -j
```

Run echo server

```sh
./echo_server -p 9999
```

Connect to server with `nc` in another terminal

```sh
nc localhost 9999
```
