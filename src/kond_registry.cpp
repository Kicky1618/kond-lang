#include "kond_registry.hpp"

#include "kond_http.hpp"
#include "kond_socket.hpp"

#include <fstream>
#include <iterator>
#ifndef _WIN32
#  include <arpa/inet.h>
#  include <netdb.h>
#  include <netinet/in.h>
#else
#  include <ws2tcpip.h>
#endif
#include <set>

namespace kond {
namespace {

namespace fs = std::filesystem;

[[noreturn]] void registryError(const std::string &message) {
    throw std::runtime_error("レジストリ: " + message);
}

struct RegistryInputError final : std::runtime_error {
    explicit RegistryInputError(const std::string &message)
        : std::runtime_error(message) {}
};

[[noreturn]] void registryInputError(const std::string &message) {
    throw RegistryInputError("レジストリ: " + message);
}

struct RegistryAddress {
    std::string host;
    std::uint16_t port = 80;
};

struct RegistryResponse {
    int status = 0;
    std::string body;
};

std::string readFile(const fs::path &path) {
    std::ifstream input(path);
    if (!input) registryError("ファイルを開けません: " + path.string());
    std::ostringstream contents;
    contents << input.rdbuf();
    return contents.str();
}

std::string readBinaryFile(const fs::path &path) {
    std::ifstream input(path, std::ios::in | std::ios::binary);
    if (!input) registryError("ファイルを開けません: " + path.string());
    return std::string(std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>());
}

std::string jsonEscape(const std::string &value) {
    std::ostringstream output;
    for (unsigned char ch : value) {
        switch (ch) {
        case '"': output << "\\\""; break;
        case '\\': output << "\\\\"; break;
        case '\b': output << "\\b"; break;
        case '\f': output << "\\f"; break;
        case '\n': output << "\\n"; break;
        case '\r': output << "\\r"; break;
        case '\t': output << "\\t"; break;
        default:
            if (ch < 0x20) {
                output << "\\u00" << std::hex << std::uppercase
                       << static_cast<int>(ch) << std::dec << std::nouppercase;
            } else {
                output << static_cast<char>(ch);
            }
            break;
        }
    }
    return output.str();
}

std::string jsonQuoted(const std::string &value) {
    return "\"" + jsonEscape(value) + "\"";
}

std::string base64Encode(const std::string &bytes) {
    static constexpr char alphabet[] =
        "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    std::string encoded;
    encoded.reserve(((bytes.size() + 2) / 3) * 4);
    for (std::size_t i = 0; i < bytes.size(); i += 3) {
        const std::size_t remaining = bytes.size() - i;
        const std::uint32_t first = static_cast<unsigned char>(bytes[i]);
        const std::uint32_t second = remaining > 1
                                         ? static_cast<unsigned char>(bytes[i + 1])
                                         : 0;
        const std::uint32_t third = remaining > 2
                                        ? static_cast<unsigned char>(bytes[i + 2])
                                        : 0;
        const std::uint32_t value = (first << 16) | (second << 8) | third;
        encoded.push_back(alphabet[(value >> 18) & 0x3f]);
        encoded.push_back(alphabet[(value >> 12) & 0x3f]);
        encoded.push_back(remaining > 1 ? alphabet[(value >> 6) & 0x3f] : '=');
        encoded.push_back(remaining > 2 ? alphabet[value & 0x3f] : '=');
    }
    return encoded;
}

int base64Value(char ch) {
    if (ch >= 'A' && ch <= 'Z') return ch - 'A';
    if (ch >= 'a' && ch <= 'z') return ch - 'a' + 26;
    if (ch >= '0' && ch <= '9') return ch - '0' + 52;
    if (ch == '+') return 62;
    if (ch == '/') return 63;
    return -1;
}

std::string base64Decode(const std::string &encoded) {
    if (encoded.size() % 4 != 0) registryInputError("binary artifactのbase64が不正です");
    std::string bytes;
    bytes.reserve((encoded.size() / 4) * 3);
    for (std::size_t i = 0; i < encoded.size(); i += 4) {
        const int first = base64Value(encoded[i]);
        const int second = base64Value(encoded[i + 1]);
        if (first < 0 || second < 0) registryInputError("binary artifactのbase64が不正です");

        const bool thirdPadding = encoded[i + 2] == '=';
        const bool fourthPadding = encoded[i + 3] == '=';
        const int third = thirdPadding ? 0 : base64Value(encoded[i + 2]);
        const int fourth = fourthPadding ? 0 : base64Value(encoded[i + 3]);
        if (third < 0 || fourth < 0 || (thirdPadding && !fourthPadding) ||
            ((thirdPadding || fourthPadding) && i + 4 != encoded.size())) {
            registryInputError("binary artifactのbase64が不正です");
        }

        const std::uint32_t value = (static_cast<std::uint32_t>(first) << 18) |
                                    (static_cast<std::uint32_t>(second) << 12) |
                                    (static_cast<std::uint32_t>(third) << 6) |
                                    static_cast<std::uint32_t>(fourth);
        bytes.push_back(static_cast<char>((value >> 16) & 0xff));
        if (!thirdPadding) bytes.push_back(static_cast<char>((value >> 8) & 0xff));
        if (!fourthPadding) bytes.push_back(static_cast<char>(value & 0xff));
    }
    return bytes;
}

RegistryAddress parseRegistryUrl(const std::string &url) {
    constexpr std::string_view prefix = "http://";
    if (url.rfind(prefix, 0) != 0) {
        registryError("registry URLは http:// 形式で指定してください（httpsは未対応です）");
    }
    const std::string authorityAndPath = url.substr(prefix.size());
    const std::size_t slash = authorityAndPath.find('/');
    const std::string authority = authorityAndPath.substr(0, slash);
    if (authority.empty() ||
        (slash != std::string::npos && slash + 1 < authorityAndPath.size())) {
        registryError("registry URLにパスを指定できません");
    }
    RegistryAddress result;
    const std::size_t colon = authority.rfind(':');
    if (colon == std::string::npos) {
        result.host = authority;
    } else {
        result.host = authority.substr(0, colon);
        const std::string portText = authority.substr(colon + 1);
        if (result.host.empty() || portText.empty()) registryError("registry URLのhost/portが不正です");
        try {
            std::size_t consumed = 0;
            const unsigned long port = std::stoul(portText, &consumed);
            if (consumed != portText.size() || port > std::numeric_limits<std::uint16_t>::max()) {
                throw std::invalid_argument("port");
            }
            result.port = static_cast<std::uint16_t>(port);
        } catch (const std::exception &) {
            registryError("registry URLのportが不正です");
        }
    }
    if (result.host.find_first_of("\r\n") != std::string::npos) {
        registryError("registry URLのhostが不正です");
    }
    return result;
}

bool validRegistryPart(const std::string &value) {
    if (value.empty() || value == "." || value == "..") return false;
    for (unsigned char ch : value) {
        if ((ch >= 'a' && ch <= 'z') || (ch >= 'A' && ch <= 'Z') ||
            (ch >= '0' && ch <= '9') || ch == '-' || ch == '_' || ch == '.' || ch == '+') {
            continue;
        }
        return false;
    }
    return true;
}

void validatePackagePart(const std::string &value, const std::string &label) {
    if (!validRegistryPart(value)) registryInputError(label + "が不正です: " + value);
}

const Value &requireObject(const Value &value, const std::string &where) {
    if (value.kind != ValueKind::Object || !value.object) {
        registryInputError(where + "はJSON objectである必要があります");
    }
    return value;
}

const Value *findJsonField(const Value &value, const std::string &name, const std::string &where) {
    const Value &object = requireObject(value, where);
    const auto found = object.object->find(name);
    return found == object.object->end() ? nullptr : &found->second;
}

std::string jsonStringField(const Value &value, const std::string &name,
                            const std::string &defaultValue, bool required,
                            const std::string &where) {
    const Value *field = findJsonField(value, name, where);
    if (!field) {
        if (required) registryInputError(where + "にフィールド '" + name + "' がありません");
        return defaultValue;
    }
    if (field->kind != ValueKind::String) {
        registryInputError(where + "." + name + "はStringである必要があります");
    }
    return field->string;
}

struct BundleInfo {
    std::string name;
    std::string version;
    std::map<std::string, std::string> files;
    std::map<std::string, std::string> binaryFiles;
    std::string entry;
    std::string library;
    std::vector<std::string> nativeFiles;
};

void validateBundlePath(const std::string &file) {
    const fs::path path(file);
    if (file.empty() || path.is_absolute()) registryInputError("bundleのファイルパスが不正です");
    for (const auto &part : path) {
        if (part == "." || part == "..") registryInputError("bundleのファイルパスが不正です");
    }
}

BundleInfo parseBundle(const std::string &body) {
    Value document;
    try {
        document = JsonParser(body, SourcePos{"<registry>", 0, 1, 1}).parse();
    } catch (const KondError &error) {
        registryInputError("パッケージbundleのJSONが不正です: " + std::string(error.what()));
    }
    BundleInfo bundle;
    bundle.name = jsonStringField(document, "name", {}, true, "bundle");
    bundle.version = jsonStringField(document, "version", {}, true, "bundle");
    const Value *filesValue = findJsonField(document, "files", "bundle");
    if (!filesValue) registryInputError("bundle.filesがありません");
    requireObject(*filesValue, "bundle.files");
    const Value &files = *filesValue;
    for (const auto &entry : *files.object) {
        if (entry.second.kind != ValueKind::String) {
            registryInputError("bundle.filesの値はStringである必要があります");
        }
        bundle.files.emplace(entry.first, entry.second.string);
    }
    const Value *binaryValue = findJsonField(document, "binary", "bundle");
    if (binaryValue) {
        requireObject(*binaryValue, "bundle.binary");
        for (const auto &entry : *binaryValue->object) {
            if (entry.second.kind != ValueKind::String) {
                registryInputError("bundle.binaryの値はbase64 Stringである必要があります");
            }
            if (bundle.files.count(entry.first) != 0) {
                registryInputError("bundleの同じパスをtextとbinaryの両方に指定できません: " + entry.first);
            }
            (void)base64Decode(entry.second.string);
            bundle.binaryFiles.emplace(entry.first, entry.second.string);
        }
    }
    const auto manifest = bundle.files.find("kond.json");
    if (manifest == bundle.files.end()) registryInputError("bundleにkond.jsonがありません");

    Value manifestDocument;
    try {
        manifestDocument = JsonParser(manifest->second, SourcePos{"<registry:kond.json>", 0, 1, 1}).parse();
    } catch (const KondError &error) {
        registryInputError("bundle内kond.jsonが不正です: " + std::string(error.what()));
    }
    const std::string manifestName = jsonStringField(manifestDocument, "name", {}, true, "bundle.kond.json");
    const std::string manifestVersion = jsonStringField(manifestDocument, "version", "0.1.0", false,
                                                        "bundle.kond.json");
    if (manifestName != bundle.name || manifestVersion != bundle.version) {
        registryInputError("bundleのname/versionとkond.jsonが一致しません");
    }
    bundle.entry = jsonStringField(manifestDocument, "entry", "main.kd", false, "bundle.kond.json");
    bundle.library = jsonStringField(manifestDocument, "library", {}, false, "bundle.kond.json");
    const Value *nativeValue = findJsonField(manifestDocument, "native", "bundle.kond.json");
    if (nativeValue) {
        if (nativeValue->kind != ValueKind::Array || !nativeValue->array) {
            registryInputError("bundle.kond.json.nativeはJSON arrayである必要があります");
        }
        std::set<std::string> nativeNames;
        for (const Value &item : *nativeValue->array) {
            if (item.kind != ValueKind::String) {
                registryInputError("bundle.kond.json.nativeの各要素はStringである必要があります");
            }
            validateBundlePath(item.string);
            if (!nativeNames.insert(item.string).second) {
                registryInputError("bundle.kond.json.nativeに重複したファイルがあります: " + item.string);
            }
            bundle.nativeFiles.push_back(item.string);
        }
    }
    const Value *dependencies = findJsonField(manifestDocument, "dependencies", "bundle.kond.json");
    if (dependencies) {
        if (dependencies->kind != ValueKind::Object || !dependencies->object) {
            registryInputError("bundle.kond.json.dependenciesはJSON objectである必要があります");
        }
        if (!dependencies->object->empty()) {
            registryInputError("registryへの公開bundleは依存関係を含められません。依存関係を解決してから公開してください");
        }
    }
    validatePackagePart(bundle.name, "package name");
    validatePackagePart(bundle.version, "package version");
    validateBundlePath(bundle.entry);
    if (!bundle.library.empty()) validateBundlePath(bundle.library);
    for (const auto &file : bundle.files) {
        validateBundlePath(file.first);
    }
    for (const auto &file : bundle.binaryFiles) {
        validateBundlePath(file.first);
    }
    const auto validateFile = [&](const std::string &file) {
        if (bundle.files.count(file) == 0) registryInputError("bundleに必要なファイルがありません: " + file);
    };
    validateFile(bundle.entry);
    if (!bundle.library.empty()) validateFile(bundle.library);
    for (const std::string &nativeFile : bundle.nativeFiles) {
        if (bundle.binaryFiles.count(nativeFile) == 0) {
            registryInputError("bundleに必要なbinary artifactがありません: " + nativeFile);
        }
    }
    return bundle;
}

std::string buildBundle(const PackageManifest &manifest) {
    if (!manifest.dependencies.empty()) {
        registryError("現在のregistry公開は依存関係なしのパッケージだけに対応しています");
    }
    const fs::path entry = packageEntryFile(manifest);
    const fs::path library = packageLibraryFile(manifest);
    std::map<std::string, std::string> files;
    std::map<std::string, std::string> binaryFiles;
    files.emplace("kond.json", readFile(manifest.root / "kond.json"));
    files.emplace(manifest.entry, readFile(entry));
    if (!manifest.library.empty()) files.emplace(manifest.library, readFile(library));
    const std::vector<fs::path> nativePaths = packageNativeFiles(manifest);
    for (std::size_t i = 0; i < nativePaths.size(); ++i) {
        if (files.count(manifest.nativeFiles[i]) != 0) {
            registryError("native artifactがソースファイルと同じパスです: " + manifest.nativeFiles[i]);
        }
        binaryFiles.emplace(manifest.nativeFiles[i], base64Encode(readBinaryFile(nativePaths[i])));
    }

    std::ostringstream output;
    output << "{\n  \"name\": " << jsonQuoted(manifest.name)
           << ",\n  \"version\": " << jsonQuoted(manifest.version)
           << ",\n  \"files\": {\n";
    bool first = true;
    for (const auto &file : files) {
        if (!first) output << ",\n";
        first = false;
        output << "    " << jsonQuoted(file.first) << ": " << jsonQuoted(file.second);
    }
    output << "\n  }";
    if (!binaryFiles.empty()) {
        output << ",\n  \"binary\": {\n";
        first = true;
        for (const auto &file : binaryFiles) {
            if (!first) output << ",\n";
            first = false;
            output << "    " << jsonQuoted(file.first) << ": " << jsonQuoted(file.second);
        }
        output << "\n  }";
    }
    output << "\n}\n";
    return output.str();
}

Socket connectRegistry(const RegistryAddress &address) {
    ensureSocketsInitialized();
    struct addrinfo hints {};
    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_STREAM;
    struct addrinfo *addresses = nullptr;
    const std::string service = std::to_string(address.port);
    if (::getaddrinfo(address.host.c_str(), service.c_str(), &hints, &addresses) != 0 || !addresses) {
        registryError("registry hostを解決できません: " + address.host);
    }
    Socket client = kInvalidSocket;
    for (struct addrinfo *entry = addresses; entry != nullptr; entry = entry->ai_next) {
        client = ::socket(entry->ai_family, entry->ai_socktype, entry->ai_protocol);
        if (client == kInvalidSocket) continue;
        if (::connect(client, entry->ai_addr, entry->ai_addrlen) == 0) break;
        closeSocket(client);
        client = kInvalidSocket;
    }
    ::freeaddrinfo(addresses);
    if (client == kInvalidSocket) registryError("registryへ接続できません: " + address.host + ":" + std::to_string(address.port));
    return client;
}

void sendAll(Socket socket, const std::string &data) {
    std::size_t sent = 0;
    while (sent < data.size()) {
#ifdef MSG_NOSIGNAL
        constexpr int flags = MSG_NOSIGNAL;
#else
        constexpr int flags = 0;
#endif
        const auto result = socketSend(socket, data.data() + sent,
                                       std::min<std::size_t>(data.size() - sent,
                                                            static_cast<std::size_t>(std::numeric_limits<int>::max())),
                                       flags);
        if (result < 0) {
            if (socketInterrupted()) continue;
            registryError("registryへのリクエスト送信に失敗しました");
        }
        if (result == 0) registryError("registryへのリクエスト送信が中断されました");
        sent += static_cast<std::size_t>(result);
    }
}

RegistryResponse parseRegistryResponse(const std::string &raw) {
    const std::size_t headerEnd = raw.find("\r\n\r\n");
    if (headerEnd == std::string::npos) registryError("registryのHTTP応答が不正です");
    const std::size_t lineEnd = raw.find("\r\n");
    if (lineEnd == std::string::npos) registryError("registryのHTTPステータスがありません");
    std::istringstream statusLine(raw.substr(0, lineEnd));
    std::string version;
    int status = 0;
    if (!(statusLine >> version >> status) || version != "HTTP/1.1") registryError("registryのHTTPステータスが不正です");
    RegistryResponse response;
    response.status = status;
    response.body = raw.substr(headerEnd + 4);
    return response;
}

RegistryResponse registryRequest(const RegistryAddress &address, const std::string &method,
                                 const std::string &path, const std::string &body) {
    const Socket client = connectRegistry(address);
    std::ostringstream request;
    request << method << ' ' << path << " HTTP/1.1\r\n"
            << "Host: " << address.host << "\r\n"
            << "Content-Type: application/json\r\n"
            << "Content-Length: " << body.size() << "\r\n"
            << "Connection: close\r\n\r\n" << body;
    try {
        sendAll(client, request.str());
    } catch (...) {
        closeSocket(client);
        throw;
    }
    std::string raw;
    char buffer[8192];
    while (true) {
        const auto received = socketReceive(client, buffer, sizeof(buffer));
        if (received == 0) break;
        if (received < 0) {
            if (socketInterrupted()) continue;
            closeSocket(client);
            registryError("registryの応答を読み取れません");
        }
        raw.append(buffer, static_cast<std::size_t>(received));
        if (raw.size() > 64 * 1024 * 1024) {
            closeSocket(client);
            registryError("registryの応答が大きすぎます");
        }
    }
    shutdownSocket(client);
    closeSocket(client);
    return parseRegistryResponse(raw);
}

HttpResponse jsonResponse(int status, std::string body) {
    HttpResponse response;
    response.status = status;
    response.body = std::move(body);
    response.headers["Content-Type"] = "application/json; charset=utf-8";
    return response;
}

HttpResponse textResponse(int status, const std::string &body) {
    HttpResponse response;
    response.status = status;
    response.body = body;
    response.headers["Content-Type"] = "text/plain; charset=utf-8";
    return response;
}

std::vector<std::string> pathParts(const std::string &path) {
    std::vector<std::string> parts;
    std::size_t cursor = path == "/" ? 1 : 0;
    while (cursor < path.size()) {
        if (path[cursor] == '/') {
            ++cursor;
            continue;
        }
        const std::size_t end = path.find('/', cursor);
        parts.push_back(path.substr(cursor, end == std::string::npos ? path.size() - cursor : end - cursor));
        cursor = end == std::string::npos ? path.size() : end;
    }
    return parts;
}

class RegistryHandler {
public:
    explicit RegistryHandler(fs::path root) : root_(std::move(root)) {}

    HttpResponse handle(const HttpRequestData &request) const {
        try {
            return handleRequest(request);
        } catch (const RegistryInputError &error) {
            return textResponse(400, std::string(error.what()) + "\n");
        }
    }

private:
    HttpResponse handleRequest(const HttpRequestData &request) const {
        const std::vector<std::string> parts = pathParts(request.path);
        if (request.path == "/healthz") return textResponse(200, "ok\n");
        if (request.method == "GET" || request.method == "HEAD") {
            if (parts.size() == 1 && parts[0] == "index.json") return jsonResponse(200, index());
            if (parts.size() == 3 && parts[0] == "packages") {
                try {
                    validatePackagePart(parts[1], "package name");
                    validatePackagePart(parts[2], "package version");
                } catch (const std::exception &error) {
                    return textResponse(400, std::string(error.what()) + "\n");
                }
                const fs::path bundle = root_ / parts[1] / parts[2] / "package.json";
                std::error_code error;
                if (!fs::is_regular_file(bundle, error)) return textResponse(404, "package not found\n");
                return jsonResponse(200, readFile(bundle));
            }
            return textResponse(404, "not found\n");
        }
        if ((request.method == "POST" || request.method == "PUT") && parts.size() == 3 && parts[0] == "packages") {
            try {
                validatePackagePart(parts[1], "package name");
                validatePackagePart(parts[2], "package version");
            } catch (const std::exception &error) {
                return textResponse(400, std::string(error.what()) + "\n");
            }
            BundleInfo bundle;
            try {
                bundle = parseBundle(request.body);
            } catch (const std::exception &error) {
                return textResponse(400, std::string(error.what()) + "\n");
            }
            if (bundle.name != parts[1] || bundle.version != parts[2]) {
                return textResponse(400, "package path and bundle identity differ\n");
            }
            const fs::path packageDirectory = root_ / bundle.name / bundle.version;
            const fs::path bundlePath = packageDirectory / "package.json";
            std::error_code error;
            if (fs::exists(bundlePath, error)) return textResponse(409, "package version already exists\n");
            if (error) return textResponse(500, "storage error\n");
            if (!fs::create_directories(packageDirectory, error) && error) return textResponse(500, "storage error\n");
            try {
                writeFile(bundlePath, request.body);
            } catch (const std::exception &) {
                return textResponse(500, "storage error\n");
            }
            return jsonResponse(201, "{\"name\": " + jsonQuoted(bundle.name) +
                                ", \"version\": " + jsonQuoted(bundle.version) + "}\n");
        }
        HttpResponse response = textResponse(405, "method not allowed\n");
        response.headers["Allow"] = "GET, HEAD, POST, PUT";
        return response;
    }

    fs::path root_;

    void writeFile(const fs::path &path, const std::string &contents) const {
        const fs::path temporary = path.parent_path() /
                                   ("." + path.filename().string() + ".tmp." + std::to_string(processId()));
        std::ofstream output(temporary, std::ios::out | std::ios::trunc);
        if (!output) throw std::runtime_error("cannot write registry bundle");
        output << contents;
        if (!output) {
            output.close();
            std::error_code cleanupError;
            fs::remove(temporary, cleanupError);
            throw std::runtime_error("cannot write registry bundle");
        }
        output.close();
        std::error_code renameError;
        fs::rename(temporary, path, renameError);
        if (renameError) {
            std::error_code cleanupError;
            fs::remove(temporary, cleanupError);
            throw std::runtime_error("cannot commit registry bundle");
        }
    }

    std::string index() const {
        std::map<std::string, std::vector<std::string>> packages;
        std::error_code error;
        for (fs::directory_iterator packageIt(root_, error); !error && packageIt != fs::directory_iterator(); packageIt.increment(error)) {
            if (!packageIt->is_directory()) continue;
            const std::string name = packageIt->path().filename().string();
            if (!validRegistryPart(name)) continue;
            for (fs::directory_iterator versionIt(packageIt->path(), error);
                 !error && versionIt != fs::directory_iterator(); versionIt.increment(error)) {
                if (!versionIt->is_directory()) continue;
                const std::string version = versionIt->path().filename().string();
                if (validRegistryPart(version) && fs::is_regular_file(versionIt->path() / "package.json")) {
                    packages[name].push_back(version);
                }
            }
        }
        std::ostringstream output;
        output << "{\n  \"packages\": {";
        if (!packages.empty()) output << '\n';
        bool firstPackage = true;
        for (auto &package : packages) {
            std::sort(package.second.begin(), package.second.end());
            if (!firstPackage) output << ",\n";
            firstPackage = false;
            output << "    " << jsonQuoted(package.first) << ": [";
            for (std::size_t i = 0; i < package.second.size(); ++i) {
                if (i != 0) output << ", ";
                output << jsonQuoted(package.second[i]);
            }
            output << "]";
        }
        if (!packages.empty()) output << '\n' << "  ";
        output << "}\n}\n";
        return output.str();
    }
};

} // namespace

void runPackageRegistryServer(const fs::path &storageDirectory, const std::string &bindAddress,
                              std::uint16_t port, std::size_t maxBodyBytes, bool once) {
    std::error_code error;
    if (fs::exists(storageDirectory, error) && !fs::is_directory(storageDirectory, error)) {
        registryError("storageがディレクトリではありません: " + storageDirectory.string());
    }
    if (!fs::create_directories(storageDirectory, error) && error) {
        registryError("storageディレクトリを作成できません: " + storageDirectory.string());
    }
    const fs::path absolute = fs::absolute(storageDirectory, error);
    if (error) registryError("storageディレクトリを解決できません");
    const fs::path root = fs::weakly_canonical(absolute, error);
    if (error) registryError("storageディレクトリを解決できません");
    RegistryHandler handler(root);
    runHttpServer(bindAddress, port, maxBodyBytes, once,
                  [&handler](const HttpRequestData &request) { return handler.handle(request); });
}

void publishPackage(const fs::path &projectDirectory, const std::string &registryUrl) {
    const PackageManifest manifest = loadPackageManifest(projectDirectory);
    validatePackagePart(manifest.name, "package name");
    validatePackagePart(manifest.version, "package version");
    const RegistryAddress address = parseRegistryUrl(registryUrl);
    const RegistryResponse response = registryRequest(
        address, "POST", "/packages/" + manifest.name + "/" + manifest.version, buildBundle(manifest));
    if (response.status == 409) registryError("同じpackage versionがすでにregistryにあります");
    if (response.status != 201) registryError("package公開に失敗しました (HTTP " + std::to_string(response.status) + ")");
    std::cout << "kond: published " << manifest.name << "@" << manifest.version
              << " to " << registryUrl << "\n";
}

void fetchPackage(const fs::path &projectDirectory, const std::string &name,
                  const std::string &version, const std::string &registryUrl) {
    validatePackagePart(name, "package name");
    validatePackagePart(version, "package version");
    PackageManifest project = loadPackageManifest(projectDirectory);
    const RegistryAddress address = parseRegistryUrl(registryUrl);
    const RegistryResponse response = registryRequest(address, "GET", "/packages/" + name + "/" + version, {});
    if (response.status == 404) registryError("packageがregistryにありません: " + name + "@" + version);
    if (response.status != 200) registryError("package取得に失敗しました (HTTP " + std::to_string(response.status) + ")");
    const BundleInfo bundle = parseBundle(response.body);
    if (bundle.name != name || bundle.version != version) registryError("registry応答のpackage identityが不正です");

    const fs::path packageDirectory = project.root / "vendor" / name;
    std::error_code error;
    if (fs::exists(packageDirectory, error)) {
        if (!fs::is_directory(packageDirectory, error)) registryError("vendor先がディレクトリではありません");
        fs::directory_iterator entries(packageDirectory, error);
        if (error || entries != fs::directory_iterator()) registryError("既存のvendor packageを上書きしません");
    } else if (!fs::create_directories(packageDirectory, error) && error) {
        registryError("vendorディレクトリを作成できません");
    }
    for (const auto &file : bundle.files) {
        const fs::path relative(file.first);
        if (relative.is_absolute()) registryError("取得したファイルパスが絶対パスです");
        for (const auto &part : relative) if (part == "..") registryError("取得したファイルがvendor外を参照しています");
        const fs::path destination = packageDirectory / relative;
        fs::create_directories(destination.parent_path(), error);
        if (error) registryError("取得先ディレクトリを作成できません");
        std::ofstream output(destination, std::ios::out | std::ios::trunc);
        if (!output) registryError("取得したファイルを書き込めません: " + destination.string());
        output << file.second;
        if (!output) registryError("取得したファイルを書き込めません: " + destination.string());
    }
    for (const auto &file : bundle.binaryFiles) {
        const fs::path relative(file.first);
        if (relative.is_absolute()) registryError("取得したbinary artifactのパスが絶対パスです");
        for (const auto &part : relative) {
            if (part == "..") registryError("取得したbinary artifactがvendor外を参照しています");
        }
        const fs::path destination = packageDirectory / relative;
        fs::create_directories(destination.parent_path(), error);
        if (error) registryError("binary artifactの取得先ディレクトリを作成できません");
        const std::string bytes = base64Decode(file.second);
        std::ofstream output(destination, std::ios::out | std::ios::binary | std::ios::trunc);
        if (!output) registryError("取得したbinary artifactを書き込めません: " + destination.string());
        output.write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
        if (!output) registryError("取得したbinary artifactを書き込めません: " + destination.string());
    }
    addLocalDependency(project.root, packageDirectory);
    const PackageGraph graph = resolvePackageGraph(project.root);
    writePackageLock(graph);
    std::cout << "kond: fetched " << name << "@" << version << " into "
              << packageDirectory.string() << "\n";
}

} // namespace kond
