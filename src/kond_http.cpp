#include "kond_http.hpp"
#include "kond_socket.hpp"

#ifndef _WIN32
#  include <arpa/inet.h>
#  include <netdb.h>
#  include <netinet/in.h>
#else
#  include <ws2tcpip.h>
#endif

namespace kond {

struct HttpParseError final : std::runtime_error {
    int status;

    HttpParseError(int errorStatus, std::string message)
        : std::runtime_error(std::move(message)), status(errorStatus) {}
};

[[noreturn]] static void httpParseFail(int status, const std::string &message) {
    throw HttpParseError(status, message);
}

static std::string trimHttp(std::string_view value) {
    std::size_t first = 0;
    while (first < value.size() && (value[first] == ' ' || value[first] == '\t')) ++first;
    std::size_t last = value.size();
    while (last > first && (value[last - 1] == ' ' || value[last - 1] == '\t')) --last;
    return std::string(value.substr(first, last - first));
}

static int httpHexDigit(char ch) {
    if (ch >= '0' && ch <= '9') return ch - '0';
    if (ch >= 'a' && ch <= 'f') return ch - 'a' + 10;
    if (ch >= 'A' && ch <= 'F') return ch - 'A' + 10;
    return -1;
}

static std::string decodeHttpComponent(std::string_view value, bool plusAsSpace) {
    std::string result;
    result.reserve(value.size());
    for (std::size_t i = 0; i < value.size(); ++i) {
        const char ch = value[i];
        if (ch == '%' ) {
            if (i + 2 >= value.size()) httpParseFail(400, "不正なURLエスケープです");
            const int high = httpHexDigit(value[i + 1]);
            const int low = httpHexDigit(value[i + 2]);
            if (high < 0 || low < 0) httpParseFail(400, "不正なURLエスケープです");
            result.push_back(static_cast<char>((high << 4) | low));
            i += 2;
        } else if (plusAsSpace && ch == '+') {
            result.push_back(' ');
        } else {
            result.push_back(ch);
        }
    }
    return result;
}

static Value untrustedHttpString(std::string value) {
    Value result = Value::stringValue(std::move(value));
    result.flow = FlowUntrusted;
    return result;
}

static void parseQueryString(std::string_view query, std::map<std::string, Value> &destination) {
    std::size_t cursor = 0;
    while (cursor <= query.size()) {
        const std::size_t separator = query.find('&', cursor);
        const std::size_t end = separator == std::string_view::npos ? query.size() : separator;
        const std::string_view item = query.substr(cursor, end - cursor);
        const std::size_t equals = item.find('=');
        const std::string key = decodeHttpComponent(item.substr(0, equals), true);
        const std::string value = equals == std::string_view::npos
                                      ? std::string{}
                                      : decodeHttpComponent(item.substr(equals + 1), true);
        if (!key.empty()) destination[key] = untrustedHttpString(value);
        if (separator == std::string_view::npos) break;
        cursor = separator + 1;
    }
}

static void parseCookies(std::string_view header, std::map<std::string, Value> &destination) {
    std::size_t cursor = 0;
    while (cursor < header.size()) {
        const std::size_t separator = header.find(';', cursor);
        const std::size_t end = separator == std::string_view::npos ? header.size() : separator;
        const std::string_view item = header.substr(cursor, end - cursor);
        const std::size_t equals = item.find('=');
        if (equals != std::string_view::npos) {
            const std::string key = asciiLower(trimHttp(item.substr(0, equals)));
            if (!key.empty()) destination[key] = untrustedHttpString(trimHttp(item.substr(equals + 1)));
        }
        if (separator == std::string_view::npos) break;
        cursor = separator + 1;
    }
}

static std::size_t parseContentLength(const std::string &text) {
    const std::string value = trimHttp(text);
    if (value.empty()) httpParseFail(400, "Content-Length が空です");
    std::size_t result = 0;
    for (char ch : value) {
        if (ch < '0' || ch > '9') httpParseFail(400, "Content-Length が不正です");
        const std::size_t digit = static_cast<std::size_t>(ch - '0');
        if (result > (std::numeric_limits<std::size_t>::max() - digit) / 10) {
            httpParseFail(413, "リクエストボディが大きすぎます");
        }
        result = result * 10 + digit;
    }
    return result;
}

static HttpRequestData readHttpRequest(Socket client, std::size_t maxBodyBytes) {
    constexpr std::size_t maxHeaderBytes = 64 * 1024;
    std::string input;
    input.reserve(8192);
    std::size_t headerEnd = std::string::npos;
    while ((headerEnd = input.find("\r\n\r\n")) == std::string::npos) {
        if (input.size() >= maxHeaderBytes) httpParseFail(431, "HTTPヘッダーが大きすぎます");
        char buffer[4096];
        const auto received = socketReceive(client, buffer, sizeof(buffer));
        if (received == 0) httpParseFail(400, "HTTPリクエストが途中で終了しました");
        if (received < 0) {
            if (socketInterrupted()) continue;
            httpParseFail(400, "HTTPリクエストを読み取れません");
        }
        input.append(buffer, static_cast<std::size_t>(received));
    }

    const std::string headerBlock = input.substr(0, headerEnd);
    const std::size_t firstLineEnd = headerBlock.find("\r\n");
    if (firstLineEnd == std::string::npos) httpParseFail(400, "HTTPリクエスト行がありません");
    const std::string requestLine = headerBlock.substr(0, firstLineEnd);
    std::istringstream requestParser(requestLine);
    std::string method;
    std::string target;
    std::string version;
    std::string trailing;
    if (!(requestParser >> method >> target >> version) || (requestParser >> trailing) || method.empty() || target.empty()) {
        httpParseFail(400, "HTTPリクエスト行が不正です");
    }
    if (version != "HTTP/1.0" && version != "HTTP/1.1") httpParseFail(505, "HTTPバージョンに対応していません");
    for (char ch : method) {
        const bool token = (ch >= 'a' && ch <= 'z') || (ch >= 'A' && ch <= 'Z') ||
                           (ch >= '0' && ch <= '9') || ch == '-' || ch == '_';
        if (!token) httpParseFail(400, "HTTPメソッドが不正です");
    }

    std::map<std::string, Value> headers;
    std::size_t cursor = firstLineEnd + 2;
    while (cursor < headerBlock.size()) {
        const std::size_t foundLineEnd = headerBlock.find("\r\n", cursor);
        const std::size_t lineEnd = foundLineEnd == std::string::npos ? headerBlock.size() : foundLineEnd;
        const std::string_view line(headerBlock.data() + cursor, lineEnd - cursor);
        const std::size_t colon = line.find(':');
        if (colon == std::string_view::npos || colon == 0) httpParseFail(400, "HTTPヘッダー名が不正です");
        const std::string name = asciiLower(std::string(line.substr(0, colon)));
        for (char ch : name) {
            const bool token = (ch >= 'a' && ch <= 'z') || (ch >= '0' && ch <= '9') ||
                               ch == '!' || ch == '#' || ch == '$' || ch == '%' || ch == '&' ||
                               ch == '\'' || ch == '*' || ch == '+' || ch == '-' || ch == '.' ||
                               ch == '^' || ch == '_' || ch == '`' || ch == '|' || ch == '~';
            if (!token) httpParseFail(400, "HTTPヘッダー名が不正です");
        }
        const std::string value = trimHttp(line.substr(colon + 1));
        for (unsigned char ch : value) {
            if (ch < 0x20 && ch != '\t') httpParseFail(400, "HTTPヘッダー値が不正です");
        }
        auto found = headers.find(name);
        if (found == headers.end()) {
            headers.emplace(name, untrustedHttpString(value));
        } else {
            found->second.string += "," + value;
        }
        cursor = foundLineEnd == std::string::npos ? headerBlock.size() : lineEnd + 2;
    }

    std::size_t contentLength = 0;
    const auto contentLengthHeader = headers.find("content-length");
    if (contentLengthHeader != headers.end()) contentLength = parseContentLength(contentLengthHeader->second.string);
    const auto transferEncoding = headers.find("transfer-encoding");
    if (transferEncoding != headers.end() && asciiLower(transferEncoding->second.string) != "identity") {
        httpParseFail(501, "Transfer-Encoding に対応していません");
    }
    if (contentLength > maxBodyBytes) httpParseFail(413, "リクエストボディが大きすぎます");

    std::string body = input.substr(headerEnd + 4);
    while (body.size() < contentLength) {
        char buffer[4096];
        const std::size_t remaining = contentLength - body.size();
        const auto received = socketReceive(client, buffer, static_cast<int>(std::min<std::size_t>(sizeof(buffer), remaining)));
        if (received == 0) httpParseFail(400, "HTTPリクエストボディが途中で終了しました");
        if (received < 0) {
            if (socketInterrupted()) continue;
            httpParseFail(400, "HTTPリクエストボディを読み取れません");
        }
        body.append(buffer, static_cast<std::size_t>(received));
    }
    body.resize(contentLength);

    HttpRequestData request;
    request.method = std::move(method);
    request.target = std::move(target);
    request.headers = std::move(headers);
    request.body = std::move(body);
    const std::size_t querySeparator = request.target.find('?');
    request.path = querySeparator == std::string::npos ? request.target : request.target.substr(0, querySeparator);
    const std::string query = querySeparator == std::string::npos ? std::string{} : request.target.substr(querySeparator + 1);
    if (request.path.empty() || (request.path.front() != '/' && request.path != "*")) {
        httpParseFail(400, "origin-form のリクエストターゲットが必要です");
    }
    parseQueryString(query, request.query);
    const auto cookie = request.headers.find("cookie");
    if (cookie != request.headers.end()) parseCookies(cookie->second.string, request.cookies);
    return request;
}

static const char *httpReasonPhrase(int status) {
    switch (status) {
    case 200: return "OK";
    case 201: return "Created";
    case 202: return "Accepted";
    case 301: return "Moved Permanently";
    case 302: return "Found";
    case 204: return "No Content";
    case 400: return "Bad Request";
    case 401: return "Unauthorized";
    case 403: return "Forbidden";
    case 404: return "Not Found";
    case 405: return "Method Not Allowed";
    case 409: return "Conflict";
    case 413: return "Payload Too Large";
    case 422: return "Unprocessable Entity";
    case 431: return "Request Header Fields Too Large";
    case 500: return "Internal Server Error";
    case 501: return "Not Implemented";
    case 503: return "Service Unavailable";
    case 505: return "HTTP Version Not Supported";
    default: return "HTTP Response";
    }
}

static HttpResponse genericHttpResponse(int status, const std::string &body) {
    HttpResponse response;
    response.status = status;
    response.body = body;
    response.headers["Content-Type"] = "text/plain; charset=utf-8";
    return response;
}

static bool validHttpHeaderName(std::string_view name) {
    if (name.empty()) return false;
    for (unsigned char ch : name) {
        const bool token = (ch >= 'a' && ch <= 'z') || (ch >= 'A' && ch <= 'Z') ||
                           (ch >= '0' && ch <= '9') || ch == '!' || ch == '#' || ch == '$' ||
                           ch == '%' || ch == '&' || ch == '\'' || ch == '*' || ch == '+' ||
                           ch == '-' || ch == '.' || ch == '^' || ch == '_' || ch == '`' ||
                           ch == '|' || ch == '~';
        if (!token) return false;
    }
    return true;
}

static bool sendHttpBytes(Socket client, std::string_view data) {
    std::size_t sent = 0;
    while (sent < data.size()) {
#ifdef MSG_NOSIGNAL
        constexpr int sendFlags = MSG_NOSIGNAL;
#else
        constexpr int sendFlags = 0;
#endif
        const auto result = socketSend(client, data.data() + sent,
                                       std::min<std::size_t>(data.size() - sent,
                                                            static_cast<std::size_t>(std::numeric_limits<int>::max())),
                                       sendFlags);
        if (result < 0) {
            if (socketInterrupted()) continue;
            return false;
        }
        if (result == 0) return false;
        sent += static_cast<std::size_t>(result);
    }
    return true;
}

static bool writeHttpResponse(Socket client, const HttpResponse &response, bool headRequest) {
    std::ostringstream output;
    const int status = response.status >= 100 && response.status <= 599 ? response.status : 500;
    output << "HTTP/1.1 " << status << ' ' << httpReasonPhrase(status) << "\r\n";
    for (const auto &header : response.headers) {
        if (asciiLower(header.first) == "connection" || asciiLower(header.first) == "content-length") continue;
        if (!validHttpHeaderName(header.first) || header.second.find_first_of("\r\n") != std::string::npos) return false;
        output << header.first << ": " << header.second << "\r\n";
    }
    output << "Content-Length: " << response.body.size() << "\r\n"
           << "Connection: close\r\n\r\n";
    const std::string headers = output.str();
    if (!sendHttpBytes(client, headers)) return false;
    return headRequest || response.body.empty() || sendHttpBytes(client, response.body);
}

static Socket createHttpListener(const std::string &bindAddress, std::uint16_t port,
                                 std::uint16_t &actualPort) {
    ensureSocketsInitialized();
    std::string host = bindAddress == "localhost" ? "127.0.0.1" : bindAddress;
    struct addrinfo hints {};
    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_STREAM;
    hints.ai_flags = AI_PASSIVE;
    struct addrinfo *addresses = nullptr;
    const std::string service = std::to_string(port);
    const int lookup = ::getaddrinfo(host.c_str(), service.c_str(), &hints, &addresses);
    if (lookup != 0 || !addresses) {
        throw std::runtime_error("bind先を解決できません: " + bindAddress);
    }

    Socket server = kInvalidSocket;
    for (struct addrinfo *address = addresses; address != nullptr; address = address->ai_next) {
        server = ::socket(address->ai_family, address->ai_socktype, address->ai_protocol);
        if (server == kInvalidSocket) continue;
        int reuse = 1;
        ::setsockopt(server, SOL_SOCKET, SO_REUSEADDR,
                     reinterpret_cast<const char *>(&reuse), static_cast<SocketLength>(sizeof(reuse)));
        if (::bind(server, address->ai_addr, static_cast<SocketLength>(address->ai_addrlen)) == 0 &&
            ::listen(server, 64) == 0) break;
        closeSocket(server);
        server = kInvalidSocket;
    }
    ::freeaddrinfo(addresses);
    if (server == kInvalidSocket) throw std::runtime_error("HTTPサーバーをbind/listenできません");

    struct sockaddr_in bound {};
    SocketLength boundLength = sizeof(bound);
    if (::getsockname(server, reinterpret_cast<struct sockaddr *>(&bound), &boundLength) != 0) {
        closeSocket(server);
        throw std::runtime_error("HTTPサーバーのポートを取得できません");
    }
    actualPort = ntohs(bound.sin_port);
    return server;
}



HttpServer::HttpServer(Interpreter &interpreter, std::string bindAddress, std::uint16_t port,
               std::size_t maxBodyBytes, bool once)
        : interpreter_(interpreter), bindAddress_(std::move(bindAddress)), port_(port),
          maxBodyBytes_(maxBodyBytes), once_(once) {}



void HttpServer::run() {
    runHttpServer(bindAddress_, port_, maxBodyBytes_, once_,
                  [this](const HttpRequestData &request) {
                      return dispatchHttpRequest(interpreter_, request);
                  });
}

void runHttpServer(const std::string &bindAddress, std::uint16_t port,
                   std::size_t maxBodyBytes, bool once,
                   const HttpRequestHandler &handler) {
    std::uint16_t actualPort = 0;
    const Socket server = createHttpListener(bindAddress, port, actualPort);
    std::cerr << "kond: listening on http://" << bindAddress << ':' << actualPort << '\n';
    while (true) {
        const Socket client = ::accept(server, nullptr, nullptr);
        if (client == kInvalidSocket) {
            if (socketInterrupted()) continue;
            closeSocket(server);
            throw std::runtime_error("HTTP接続を受け付けられません");
        }
        HttpResponse response;
        bool headRequest = false;
        try {
            HttpRequestData request = readHttpRequest(client, maxBodyBytes);
            headRequest = request.method == "HEAD";
            response = handler(request);
        } catch (const HttpParseError &error) {
            response = genericHttpResponse(error.status, std::string(httpReasonPhrase(error.status)) + "\n");
        } catch (const std::exception &) {
            response = genericHttpResponse(500, "Internal Server Error\n");
        }
        writeHttpResponse(client, response, headRequest);
        shutdownSocket(client);
        closeSocket(client);
        if (once) break;
    }
    closeSocket(server);
}

Socket HttpServer::createListener() {
    return createHttpListener(bindAddress_, port_, actualPort_);
}

} // namespace kond
