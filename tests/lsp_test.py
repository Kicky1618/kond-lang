#!/usr/bin/env python3
"""Small protocol smoke test for kond lsp."""

import json
import subprocess
import sys


def send(process, message):
    payload = json.dumps(message, ensure_ascii=False, separators=(",", ":")).encode("utf-8")
    process.stdin.write(f"Content-Length: {len(payload)}\r\n\r\n".encode("ascii") + payload)
    process.stdin.flush()


def receive(process):
    headers = {}
    while True:
        line = process.stdout.readline()
        if not line:
            raise AssertionError("kond lsp closed before replying")
        line = line.rstrip(b"\r\n")
        if not line:
            break
        key, value = line.split(b":", 1)
        headers[key.decode("ascii").lower()] = value.strip()
    length = int(headers["content-length"])
    return json.loads(process.stdout.read(length).decode("utf-8"))


def main():
    binary = sys.argv[1]
    process = subprocess.Popen([binary, "lsp"], stdin=subprocess.PIPE, stdout=subprocess.PIPE)
    uri = "file:///tmp/kond-lsp-test.kd"
    try:
        send(process, {"jsonrpc": "2.0", "id": 1, "method": "initialize", "params": {}})
        initialize = receive(process)
        capabilities = initialize["result"]["capabilities"]
        assert capabilities["definitionProvider"] is True
        assert capabilities["completionProvider"]["triggerCharacters"] == ["."]

        send(process, {
            "jsonrpc": "2.0",
            "method": "textDocument/didOpen",
            "params": {"textDocument": {"uri": uri, "version": 1, "text": "fn main( {\n"}},
        })
        invalid_diagnostics = receive(process)
        assert invalid_diagnostics["method"] == "textDocument/publishDiagnostics"
        assert invalid_diagnostics["params"]["diagnostics"]

        source = (
            "condition positive(x) = x > 0;\n"
            "fn helper(x) { return x; }\n"
            "fn main() { let value = helper(1); print(value); }\n"
        )
        send(process, {
            "jsonrpc": "2.0",
            "method": "textDocument/didChange",
            "params": {
                "textDocument": {"uri": uri, "version": 2},
                "contentChanges": [{"text": source}],
            },
        })
        valid_diagnostics = receive(process)
        assert valid_diagnostics["params"]["diagnostics"] == []

        send(process, {
            "jsonrpc": "2.0",
            "id": 2,
            "method": "textDocument/documentSymbol",
            "params": {"textDocument": {"uri": uri}},
        })
        symbols = receive(process)["result"]
        assert {symbol["name"] for symbol in symbols} == {"positive", "helper", "main"}

        send(process, {
            "jsonrpc": "2.0",
            "id": 3,
            "method": "textDocument/definition",
            "params": {"textDocument": {"uri": uri}, "position": {"line": 2, "character": 24}},
        })
        definitions = receive(process)["result"]
        assert len(definitions) == 1
        assert definitions[0]["range"]["start"] == {"line": 1, "character": 3}

        send(process, {
            "jsonrpc": "2.0",
            "id": 4,
            "method": "textDocument/references",
            "params": {
                "textDocument": {"uri": uri},
                "position": {"line": 2, "character": 24},
                "context": {"includeDeclaration": True},
            },
        })
        assert len(receive(process)["result"]) == 2

        send(process, {
            "jsonrpc": "2.0",
            "id": 5,
            "method": "textDocument/hover",
            "params": {"textDocument": {"uri": uri}, "position": {"line": 2, "character": 24}},
        })
        assert "fn helper" in receive(process)["result"]["contents"]["value"]

        send(process, {
            "jsonrpc": "2.0",
            "id": 6,
            "method": "textDocument/completion",
            "params": {"textDocument": {"uri": uri}, "position": {"line": 2, "character": 16}},
        })
        completion_labels = {item["label"] for item in receive(process)["result"]["items"]}
        assert "helper" in completion_labels
        assert "std" in completion_labels

        send(process, {"jsonrpc": "2.0", "id": 7, "method": "shutdown"})
        assert receive(process)["result"] is None
        send(process, {"jsonrpc": "2.0", "method": "exit"})
        process.stdin.close()
        assert process.wait(timeout=5) == 0
    finally:
        if process.poll() is None:
            process.kill()
            process.wait()


if __name__ == "__main__":
    main()
