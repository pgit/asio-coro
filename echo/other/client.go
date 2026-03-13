package main

import (
    "flag"
    "fmt"
    "io"
    "net"
    "runtime"
    "strconv"
    "sync"
    "sync/atomic"
    "time"
)

const (
    defaultHost       = "127.0.0.1"
    defaultPort       = 55555
    defaultConnections = 1
    defaultDuration   = 1.0
    defaultBufferSize = 64 * 1024
)

type sessionStats struct {
    written  uint64
    read     uint64
    elapsed  time.Duration
    connected bool
}

func makePayload(size int) []byte {
    payload := make([]byte, size)
    for i := range payload {
        payload[i] = byte(i)
    }
    return payload
}

func writeLoop(conn net.Conn, payload []byte, duration time.Duration) uint64 {
    var total uint64
    var deadline time.Time
    if duration > 0 {
        deadline = time.Now().Add(duration)
    }

    for {
        if !deadline.IsZero() && time.Now().After(deadline) {
            break
        }
        n, err := conn.Write(payload)
        if n > 0 {
            total += uint64(n)
        }
        if err != nil {
            break
        }
    }

    if tcpConn, ok := conn.(*net.TCPConn); ok {
        _ = tcpConn.CloseWrite()
    }
    return total
}

func readLoop(conn net.Conn, bufferSize int) uint64 {
    buffer := make([]byte, bufferSize)
    var total uint64
    for {
        n, err := conn.Read(buffer)
        if n > 0 {
            total += uint64(n)
        }
        if err != nil {
            if err == io.EOF {
                break
            }
            break
        }
    }
    return total
}

func runSession(host string, port int, duration time.Duration) sessionStats {
    stats := sessionStats{}
    address := net.JoinHostPort(host, strconv.Itoa(port))

    conn, err := net.Dial("tcp", address)
    if err != nil {
        fmt.Printf("ERROR: %v\n", err)
        return stats
    }
    stats.connected = true
    fmt.Printf("connected to: %s\n", address)

    payload := makePayload(defaultBufferSize)
    start := time.Now()

    var wg sync.WaitGroup
    var written uint64
    var read uint64

    wg.Add(2)
    go func() {
        defer wg.Done()
        written = writeLoop(conn, payload, duration)
    }()
    go func() {
        defer wg.Done()
        read = readLoop(conn, defaultBufferSize)
    }()
    wg.Wait()

    _ = conn.Close()

    stats.written = written
    stats.read = read
    stats.elapsed = time.Since(start)
    return stats
}

func main() {
    host := flag.String("host", defaultHost, "host to connect to")
    port := flag.Int("port", defaultPort, "port number")
    connections := flag.Int("connections", defaultConnections, "number of connections")
    threads := flag.Int("threads", runtime.NumCPU(), "number of OS threads to run in parallel")
    duration := flag.Float64("duration", defaultDuration, "number of seconds to run before closing the connection")
    debug := flag.Bool("debug", false, "enable debug mode (single threaded with additional logging)")

    flag.StringVar(host, "h", defaultHost, "host to connect to")
    flag.IntVar(port, "p", defaultPort, "port number")
    flag.IntVar(connections, "c", defaultConnections, "number of connections")
    flag.IntVar(threads, "t", runtime.NumCPU(), "number of OS threads to run in parallel")
    flag.Float64Var(duration, "d", defaultDuration, "number of seconds to run before closing the connection")

    flag.Parse()

    if *threads <= 0 {
        fmt.Println("ERROR: number of threads must be at least 1")
        return
    }
    if *connections <= 0 {
        fmt.Println("ERROR: number of connections must be at least 1")
        return
    }
    if *duration < 0 {
        fmt.Println("ERROR: duration must be non-negative")
        return
    }

    if *debug {
        *threads = 1
        fmt.Println("DEBUG mode enabled")
    }

    if *threads > *connections {
        *threads = *connections
    }
    runtime.GOMAXPROCS(*threads)

    runDuration := time.Duration(*duration * float64(time.Second))

    start := time.Now()
    var totalBytes uint64

    var wg sync.WaitGroup
    for i := 0; i < *connections; i++ {
        wg.Add(1)
        go func() {
            defer wg.Done()
            stats := runSession(*host, *port, runDuration)
            if stats.connected {
                elapsedMs := stats.elapsed.Milliseconds()
                delta := int64(stats.read) - int64(stats.written)
                fmt.Printf("wrote %d and read %d (delta %d) in %dms\n", stats.written, stats.read, delta, elapsedMs)
            }
            atomic.AddUint64(&totalBytes, stats.read)
        }()
    }
    wg.Wait()

    elapsedMs := time.Since(start).Milliseconds()
    if elapsedMs < 1 {
        elapsedMs = 1
    }
    mibPerSec := int64(atomic.LoadUint64(&totalBytes)) * 1000 / 1024 / 1024 / elapsedMs
    fmt.Printf("Total bytes echoed: %d at %d MiB/s\n", atomic.LoadUint64(&totalBytes), mibPerSec)
}
