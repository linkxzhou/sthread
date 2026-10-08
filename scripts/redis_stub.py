#!/usr/bin/env python3
# 冒烟用的 RESP 桩：只实现 PING / SET / GET / INCR。不是库的运行时依赖。
import socket
import sys


def read_exact(conn, n):
    buf = b""
    while len(buf) < n:
        chunk = conn.recv(n - len(buf))
        if not chunk:
            return None
        buf += chunk
    return buf


def read_line(conn):
    buf = b""
    while b"\r\n" not in buf:
        chunk = conn.recv(1)
        if not chunk:
            return None
        buf += chunk
        if len(buf) > 1024 * 1024:
            return None
    return buf[:-2]


def read_cmd(conn):
    line = read_line(conn)
    if line is None or not line.startswith(b"*"):
        return None
    try:
        argc = int(line[1:])
    except ValueError:
        return None
    args = []
    for _ in range(argc):
        header = read_line(conn)
        if header is None or not header.startswith(b"$"):
            return None
        try:
            length = int(header[1:])
        except ValueError:
            return None
        if length < 0:
            return None
        data = read_exact(conn, length)
        crlf = read_exact(conn, 2)
        if data is None or crlf != b"\r\n":
            return None
        args.append(data)
    return args


def handle(conn, store):
    while True:
        args = read_cmd(conn)
        if not args:
            return
        cmd = args[0].upper()
        if cmd == b"PING":
            conn.sendall(b"+PONG\r\n")
        elif cmd == b"SET" and len(args) >= 3:
            store[args[1]] = args[2]
            conn.sendall(b"+OK\r\n")
        elif cmd == b"GET" and len(args) >= 2:
            value = store.get(args[1])
            if value is None:
                conn.sendall(b"$-1\r\n")
            else:
                conn.sendall(b"$%d\r\n%s\r\n" % (len(value), value))
        elif cmd == b"INCR" and len(args) >= 2:
            current = store.get(args[1], b"0")
            try:
                number = int(current) + 1
            except ValueError:
                conn.sendall(b"-ERR value is not an integer\r\n")
                continue
            store[args[1]] = str(number).encode("ascii")
            conn.sendall(b":%d\r\n" % number)
        else:
            conn.sendall(b"-ERR unknown command\r\n")


def main():
    port = int(sys.argv[1]) if len(sys.argv) > 1 else 6379
    store = {}
    server = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
    server.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
    server.bind(("127.0.0.1", port))
    server.listen(64)
    sys.stdout.write("listening 127.0.0.1:%d\n" % port)
    sys.stdout.flush()
    while True:
        conn, _addr = server.accept()
        try:
            handle(conn, store)
        except (ConnectionError, OSError):
            pass
        finally:
            conn.close()


if __name__ == "__main__":
    main()
