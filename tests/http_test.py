#!/usr/bin/env python3
"""Black-box tests for the built-in HTTP server."""

import socket
import subprocess
import sys
import tempfile
import time
from pathlib import Path


def free_port():
    with socket.socket() as sock:
        sock.bind(("127.0.0.1", 0))
        return sock.getsockname()[1]


def request(binary, source, raw_request, *extra_args):
    port = free_port()
    process = subprocess.Popen(
        [binary, "serve", str(source), "--bind", "127.0.0.1", "--port", str(port), "--once", *extra_args],
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
    )
    connection = None
    try:
        deadline = time.monotonic() + 5
        while connection is None:
            try:
                connection = socket.create_connection(("127.0.0.1", port), timeout=1)
            except OSError:
                if time.monotonic() >= deadline:
                    stderr = process.stderr.read().decode("utf-8", errors="replace")
                    raise AssertionError(f"HTTP server did not start: {stderr}")
                time.sleep(0.02)
        connection.sendall(raw_request)
        chunks = []
        while True:
            chunk = connection.recv(4096)
            if not chunk:
                break
            chunks.append(chunk)
        return b"".join(chunks)
    finally:
        if connection is not None:
            connection.close()
        try:
            process.wait(timeout=5)
        except subprocess.TimeoutExpired:
            process.kill()
            process.wait()


def assert_response(response, status, body, headers=()):
    head, separator, actual_body = response.partition(b"\r\n\r\n")
    assert separator, response
    lines = head.decode("ascii").split("\r\n")
    assert lines[0].startswith(f"HTTP/1.1 {status} "), lines[0]
    for header in headers:
        assert any(header.lower() in line.lower() for line in lines[1:]), lines
    assert actual_body == body, (actual_body, body)


def main():
    binary = sys.argv[1]
    root = Path(__file__).resolve().parents[1]
    http_source = root / "examples" / "http_server.kd"

    response = request(
        binary,
        http_source,
        b"GET /inspect?name=Ada+Lovelace HTTP/1.1\r\n"
        b"Host: localhost\r\nX-Token: secret\r\nCookie: sid=abc123\r\n\r\n",
    )
    assert_response(
        response,
        200,
        b'{"method":"GET","name":"Ada Lovelace","path":"/inspect",'
        b'"session":"abc123","token":"secret"}',
        ("Content-Type: application/json",),
    )

    assert_response(
        request(binary, http_source, b"GET /created HTTP/1.1\r\nHost: localhost\r\n\r\n"),
        201,
        b"created",
        ("X-Kond: std",),
    )
    assert_response(
        request(binary, http_source, b"HEAD /inspect HTTP/1.1\r\nHost: localhost\r\n\r\n"),
        200,
        b"",
    )
    assert_response(
        request(binary, http_source, b"GET /missing HTTP/1.1\r\nHost: localhost\r\n\r\n"),
        404,
        b"Not Found\n",
    )
    assert_response(
        request(binary, http_source, b"POST /inspect HTTP/1.1\r\nHost: localhost\r\n\r\n"),
        405,
        b"Method Not Allowed\n",
        ("Allow: GET",),
    )
    assert_response(
        request(binary, http_source, b"GET /inspect?name=%ZZ HTTP/1.1\r\nHost: localhost\r\n\r\n"),
        400,
        b"Bad Request\n",
    )

    with tempfile.TemporaryDirectory() as directory:
        source = Path(directory) / "body.kd"
        source.write_text('route POST "/echo" (req) { return req.body; }\n', encoding="utf-8")
        assert_response(
            request(
                binary,
                source,
                b"POST /echo HTTP/1.1\r\nHost: localhost\r\nContent-Length: 4\r\n\r\ntest",
                "--max-body",
                "3",
            ),
            413,
            b"Payload Too Large\n",
        )


if __name__ == "__main__":
    main()
