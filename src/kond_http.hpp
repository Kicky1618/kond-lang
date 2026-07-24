#pragma once

#include "kond_interpreter_api.hpp"

namespace kond {

using HttpRequestHandler = std::function<HttpResponse(const HttpRequestData &)>;

// Run a small synchronous HTTP/1.0/1.1 server with the same bounded parser
// used by route-based `serve`.  Package registries use this transport layer
// without creating an Interpreter.
void runHttpServer(const std::string &bindAddress, std::uint16_t port,
                   std::size_t maxBodyBytes, bool once,
                   const HttpRequestHandler &handler);

class HttpServer {
public:
    HttpServer(Interpreter &interpreter, std::string bindAddress, std::uint16_t port,
               std::size_t maxBodyBytes, bool once);
    void run();

private:
    Interpreter &interpreter_;
    std::string bindAddress_;
    std::uint16_t port_ = 0;
    std::uint16_t actualPort_ = 0;
    std::size_t maxBodyBytes_ = 1024 * 1024;
    bool once_ = false;

    int createListener();
};

} // namespace kond
