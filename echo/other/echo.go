package main

import (
    "io"
    "log"
    "net"
)

func main() {
    addr := "localhost:55555"
    server, err := net.Listen("tcp", addr)
    if err != nil {
        log.Fatalln(err)
    }
    defer server.Close()

    log.Println("Server is running on:", addr)

    for {
        conn, err := server.Accept()
        if err != nil {
            log.Println("Failed to accept conn.", err)
            continue
        }

        go func(conn net.Conn) {
            defer func() {
                conn.Close()
            }()
            io.Copy(conn, conn)
        }(conn)
    }
}
