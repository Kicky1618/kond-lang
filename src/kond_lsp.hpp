#pragma once

namespace kond {

// Run the Kond language server over stdin/stdout using the LSP's
// Content-Length framed JSON-RPC transport.
int runLspServer();

} // namespace kond
