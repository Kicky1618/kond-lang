#include "kond_http.hpp"
#include "kond_jit.hpp"
#include "kond_package.hpp"
#include "kond_registry.hpp"

#include <cstdlib>
#include <fstream>
#include <filesystem>

namespace kond {

static Mode parseMode(const std::string &mode) {
    if (mode == "safe") return Mode::Safe;
    if (mode == "verified") return Mode::Verified;
    if (mode == "unsafe") return Mode::Unsafe;
    throw std::invalid_argument("unknown mode");
}

static std::string readFile(const std::string &path) {
    std::ifstream input(path);
    if (!input) throw std::runtime_error("ファイルを開けません: " + path);
    std::ostringstream contents;
    contents << input.rdbuf();
    return contents.str();
}

static void printUsage() {
    std::cout << "Kond interpreter (Draft 0.2 core)\n\n"
              << "usage:\n"
              << "  kond run <file.kd> [--entry NAME] [--mode safe|verified|unsafe] [--ifc explicit|strict]\n"
              << "       [--lib FILE] [--opt-lib FILE] [--explain-optimizations] [--trace-ownership] [--jit] [--dump-llvm]\n"
              << "  kond serve <file.kd> [--bind ADDRESS] [--port PORT] [--max-body BYTES] [--once]\n"
              << "       [--mode safe|verified|unsafe] [--ifc explicit|strict] [--lib FILE] [--opt-lib FILE]\n"
              << "  kond check <file.kd> [--lib FILE] [--opt-lib FILE]\n"
              << "  kond new <directory>        (create a minimal Kond project)\n"
              << "  kond install [directory]    (resolve local path dependencies)\n"
              << "  kond add <directory> [--project DIRECTORY]\n"
              << "  kond remove <name> [--project DIRECTORY]\n"
              << "  kond list [directory]       (list resolved packages)\n"
              << "  kond registry <directory> [--bind ADDRESS] [--port PORT] [--max-body BYTES] [--once]\n"
              << "  kond publish [directory] [--registry URL]  (default: http://kond.j9.si)\n"
              << "  kond fetch <name> <version> [--registry URL] [--project DIRECTORY]\n"
              << "  kond <file.kd>                 (same as run)\n";
}

static std::string configuredRegistryUrl() {
    const char *configured = std::getenv("KOND_REGISTRY");
    if (configured != nullptr && *configured != '\0') return configured;
    return std::string(kDefaultRegistryUrl);
}

static int runPackageCommand(const std::string &command, int argc, char **argv, int argument) {
    namespace fs = std::filesystem;
    auto isHelp = [](const std::string &value) { return value == "--help" || value == "-h"; };
    auto reportError = [](const std::exception &error) {
        std::cerr << "error: " << error.what() << "\n";
        return 1;
    };

    try {
        if (command == "install" || command == "list") {
            fs::path directory = ".";
            if (argument < argc) {
                if (isHelp(argv[argument])) {
                    printUsage();
                    return 0;
                }
                directory = argv[argument++];
            }
            if (argument < argc) {
                std::cerr << "error: kond " << command << " はディレクトリを1つまで受け取ります\n";
                return 2;
            }
            const PackageGraph graph = resolvePackageGraph(directory);
            if (command == "install") {
                (void)packageEntryFile(graph.root);
                (void)packageLibraryFiles(graph);
                writePackageLock(graph);
                std::cout << "kond: resolved " << graph.dependencies.size()
                          << " package(s) and wrote "
                          << (graph.root.root / "kond.lock").string() << "\n";
            } else {
                std::cout << graph.root.name << "@" << graph.root.version << " (root)\n";
                for (const PackageNode &node : graph.dependencies) {
                    std::cout << "  " << node.manifest.name << "@" << node.manifest.version
                              << " [" << node.manifest.root.string() << "]\n";
                }
            }
            return 0;
        }

        fs::path project = ".";
        std::vector<std::string> positional;
        while (argument < argc) {
            const std::string option = argv[argument++];
            if (isHelp(option)) {
                printUsage();
                return 0;
            }
            if (option == "--project" || option == "-p") {
                if (argument >= argc) {
                    std::cerr << "error: " << option << " にはディレクトリが必要です\n";
                    return 2;
                }
                project = argv[argument++];
            } else if (!option.empty() && option.front() == '-') {
                std::cerr << "error: 不明なオプションです: " << option << "\n";
                return 2;
            } else {
                positional.push_back(option);
            }
        }

        if (command == "add") {
            fs::path dependency;
            if (positional.size() == 1) {
                dependency = positional.front();
            } else if (positional.size() == 2 && project == ".") {
                project = positional[0];
                dependency = positional[1];
            } else {
                std::cerr << "error: kond add は依存パッケージのディレクトリを1つ受け取ります\n";
                return 2;
            }
            const std::string addedName = loadPackageManifest(dependency).name;
            addLocalDependency(project, dependency);
            const PackageGraph graph = resolvePackageGraph(project);
            (void)packageEntryFile(graph.root);
            (void)packageLibraryFiles(graph);
            writePackageLock(graph);
            std::cout << "kond: added " << addedName
                      << " and wrote " << (graph.root.root / "kond.lock").string() << "\n";
            return 0;
        }

        if (command == "remove") {
            if (positional.size() == 2 && project == ".") {
                project = positional[0];
                positional.erase(positional.begin());
            }
            if (positional.size() != 1) {
                std::cerr << "error: kond remove はパッケージ名を1つ受け取ります\n";
                return 2;
            }
            removeDependency(project, positional.front());
            const PackageGraph graph = resolvePackageGraph(project);
            (void)packageEntryFile(graph.root);
            (void)packageLibraryFiles(graph);
            writePackageLock(graph);
            std::cout << "kond: removed " << positional.front()
                      << " and wrote " << (graph.root.root / "kond.lock").string() << "\n";
            return 0;
        }
    } catch (const std::exception &error) {
        return reportError(error);
    }
    return 2;
}

static int runRegistryCommand(const std::string &command, int argc, char **argv, int argument) {
    namespace fs = std::filesystem;
    auto isHelp = [](const std::string &value) { return value == "--help" || value == "-h"; };
    auto reportError = [](const std::exception &error) {
        std::cerr << "error: " << error.what() << "\n";
        return 1;
    };
    auto parsePort = [](const std::string &value, const std::string &option,
                        std::uint16_t &destination) -> bool {
        try {
            std::size_t consumed = 0;
            const unsigned long parsed = std::stoul(value, &consumed);
            if (consumed != value.size() || parsed > std::numeric_limits<std::uint16_t>::max()) {
                throw std::invalid_argument("port");
            }
            destination = static_cast<std::uint16_t>(parsed);
            return true;
        } catch (const std::exception &) {
            std::cerr << "error: " << option << " は0以上65535以下の整数である必要があります\n";
            return false;
        }
    };
    auto parseSize = [](const std::string &value, const std::string &option,
                        std::size_t &destination) -> bool {
        try {
            std::size_t consumed = 0;
            const unsigned long long parsed = std::stoull(value, &consumed);
            if (consumed != value.size() || parsed > std::numeric_limits<std::size_t>::max()) {
                throw std::invalid_argument("size");
            }
            destination = static_cast<std::size_t>(parsed);
            return true;
        } catch (const std::exception &) {
            std::cerr << "error: " << option << " は0以上の整数である必要があります\n";
            return false;
        }
    };

    try {
        if (command == "registry") {
            if (argument >= argc || isHelp(argv[argument])) {
                printUsage();
                return argument >= argc ? 2 : 0;
            }
            fs::path storage;
            bool hasStorage = false;
            std::string bindAddress = "127.0.0.1";
            std::uint16_t port = 8787;
            std::size_t maxBodyBytes = 64 * 1024 * 1024;
            bool once = false;
            while (argument < argc) {
                const std::string option = argv[argument++];
                if (isHelp(option)) {
                    printUsage();
                    return 0;
                }
                if (option == "--bind" || option == "--host") {
                    if (argument >= argc) {
                        std::cerr << "error: " << option << " にはアドレスが必要です\n";
                        return 2;
                    }
                    bindAddress = argv[argument++];
                } else if (option == "--port") {
                    if (argument >= argc) {
                        std::cerr << "error: --port には番号が必要です\n";
                        return 2;
                    }
                    if (!parsePort(argv[argument++], "--port", port)) return 2;
                } else if (option == "--max-body") {
                    if (argument >= argc) {
                        std::cerr << "error: --max-body にはサイズが必要です\n";
                        return 2;
                    }
                    if (!parseSize(argv[argument++], "--max-body", maxBodyBytes)) return 2;
                } else if (option == "--once") {
                    once = true;
                } else if (!option.empty() && option.front() == '-') {
                    std::cerr << "error: 不明なオプションです: " << option << "\n";
                    return 2;
                } else if (hasStorage) {
                    std::cerr << "error: kond registry はstorageディレクトリを1つだけ受け取ります\n";
                    return 2;
                } else {
                    storage = option;
                    hasStorage = true;
                }
            }
            if (!hasStorage) {
                std::cerr << "error: kond registry にはstorageディレクトリが必要です\n";
                return 2;
            }
            runPackageRegistryServer(storage, bindAddress, port, maxBodyBytes, once);
            return 0;
        }

        fs::path project = ".";
        std::string registry = configuredRegistryUrl();
        std::vector<std::string> positional;
        while (argument < argc) {
            const std::string option = argv[argument++];
            if (isHelp(option)) {
                printUsage();
                return 0;
            }
            if (option == "--registry" || option == "-r") {
                if (argument >= argc) {
                    std::cerr << "error: " << option << " にはURLが必要です\n";
                    return 2;
                }
                registry = argv[argument++];
            } else if (option == "--project" || option == "-p") {
                if (argument >= argc) {
                    std::cerr << "error: " << option << " にはディレクトリが必要です\n";
                    return 2;
                }
                project = argv[argument++];
            } else if (!option.empty() && option.front() == '-') {
                std::cerr << "error: 不明なオプションです: " << option << "\n";
                return 2;
            } else {
                positional.push_back(option);
            }
        }
        if (registry.empty()) {
            std::cerr << "error: --registry URL が必要です\n";
            return 2;
        }
        if (command == "publish") {
            if (positional.size() > 1) {
                std::cerr << "error: kond publish はディレクトリを1つまで受け取ります\n";
                return 2;
            }
            if (!positional.empty()) project = positional.front();
            publishPackage(project, registry);
            return 0;
        }
        if (positional.size() != 2) {
            std::cerr << "error: kond fetch はnameとversionを受け取ります\n";
            return 2;
        }
        fetchPackage(project, positional[0], positional[1], registry);
        return 0;
    } catch (const std::exception &error) {
        return reportError(error);
    }
}


} // namespace kond

int main(int argc, char **argv) {
    using namespace kond;
    if (argc < 2) {
        printUsage();
        return 2;
    }

    std::string command = "run";
    int argument = 1;
    if (std::string(argv[argument]) == "run" || std::string(argv[argument]) == "check" ||
        std::string(argv[argument]) == "serve" || std::string(argv[argument]) == "new" ||
        std::string(argv[argument]) == "install" || std::string(argv[argument]) == "add" ||
        std::string(argv[argument]) == "remove" || std::string(argv[argument]) == "list" ||
        std::string(argv[argument]) == "registry" || std::string(argv[argument]) == "publish" ||
        std::string(argv[argument]) == "fetch") {
        command = argv[argument++];
    }

    if (command == "new") {
        if (argument >= argc || std::string(argv[argument]) == "--help" || std::string(argv[argument]) == "-h") {
            printUsage();
            return argument >= argc ? 2 : 0;
        }
        const std::string projectPath = argv[argument++];
        if (argument < argc) {
            std::cerr << "error: kond new はディレクトリを1つだけ受け取ります\n";
            return 2;
        }
        try {
            return createPackageProject(projectPath);
        } catch (const std::exception &error) {
            std::cerr << "error: " << error.what() << "\n";
            return 1;
        }
    }

    if (command == "registry" || command == "publish" || command == "fetch") {
        return runRegistryCommand(command, argc, argv, argument);
    }

    if (command == "install" || command == "add" || command == "remove" || command == "list") {
        return runPackageCommand(command, argc, argv, argument);
    }

    if (argument >= argc || std::string(argv[argument]) == "--help" || std::string(argv[argument]) == "-h") {
        printUsage();
        return argument >= argc ? 2 : 0;
    }

    std::string file = argv[argument++];
    std::string entry;
    Mode mode = Mode::Safe;
    bool strictIfc = false;
    bool explainOptimizations = false;
    bool traceOwnership = false;
    bool useJit = false;
    bool dumpLlvm = false;
    std::vector<std::string> sourceLibraries;
    std::vector<std::string> optimizationLibraries;
    std::string bindAddress = "127.0.0.1";
    std::uint16_t port = 8080;
    std::size_t maxBodyBytes = 1024 * 1024;
    bool once = false;
    while (argument < argc) {
        const std::string option = argv[argument++];
        if (option == "--entry" && argument < argc) {
            entry = argv[argument++];
        } else if ((option == "--bind" || option == "--host") && argument < argc) {
            bindAddress = argv[argument++];
        } else if (option == "--port" && argument < argc) {
            try {
                const std::string value = argv[argument++];
                std::size_t consumed = 0;
                const unsigned long parsed = std::stoul(value, &consumed);
                if (consumed != value.size()) throw std::invalid_argument("port");
                if (parsed > std::numeric_limits<std::uint16_t>::max()) throw std::out_of_range("port");
                port = static_cast<std::uint16_t>(parsed);
            } catch (const std::exception &) {
                std::cerr << "error: --port は0以上65535以下の整数である必要があります\n";
                return 2;
            }
        } else if (option == "--max-body" && argument < argc) {
            try {
                const std::string value = argv[argument++];
                std::size_t consumed = 0;
                const unsigned long long parsed = std::stoull(value, &consumed);
                if (consumed != value.size()) throw std::invalid_argument("max-body");
                if (parsed > std::numeric_limits<std::size_t>::max()) throw std::out_of_range("max-body");
                maxBodyBytes = static_cast<std::size_t>(parsed);
            } catch (const std::exception &) {
                std::cerr << "error: --max-body は0以上の整数である必要があります\n";
                return 2;
            }
        } else if (option == "--once") {
            once = true;
        } else if (option == "--mode" && argument < argc) {
            try {
                mode = parseMode(argv[argument++]);
            } catch (const std::invalid_argument &) {
                std::cerr << "error: --mode は safe, verified, unsafe のいずれかです\n";
                return 2;
            }
        } else if (option == "--ifc" && argument < argc) {
            const std::string profile = argv[argument++];
            if (profile == "strict") strictIfc = true;
            else if (profile == "explicit") strictIfc = false;
            else {
                std::cerr << "error: --ifc は explicit または strict です\n";
                return 2;
            }
        } else if (option == "--explain-optimizations") {
            explainOptimizations = true;
        } else if (option == "--trace-ownership") {
            traceOwnership = true;
        } else if (option == "--jit") {
            useJit = true;
        } else if (option == "--dump-llvm") {
            useJit = true;
            dumpLlvm = true;
        } else if ((option == "--lib" || option == "--library") && argument < argc) {
            sourceLibraries.push_back(argv[argument++]);
        } else if (option == "--opt-lib" && argument < argc) {
            optimizationLibraries.push_back(argv[argument++]);
        } else {
            std::cerr << "error: 不明なオプションです: " << option << "\n";
            return 2;
        }
    }

    try {
        std::error_code packageInputError;
        if (std::filesystem::is_directory(file, packageInputError)) {
            const PackageGraph packageGraph = resolvePackageGraph(file);
            validatePackageLock(packageGraph);
            const std::vector<std::filesystem::path> packageLibraries = packageLibraryFiles(packageGraph);
            std::vector<std::string> resolvedLibraries;
            resolvedLibraries.reserve(packageLibraries.size() + sourceLibraries.size());
            for (const auto &library : packageLibraries) resolvedLibraries.push_back(library.string());
            resolvedLibraries.insert(resolvedLibraries.end(), sourceLibraries.begin(), sourceLibraries.end());
            sourceLibraries = std::move(resolvedLibraries);
            file = packageEntryFile(packageGraph.root).string();
        }
        Program program = parseProgram(readFile(file), file);
        for (const std::string &library : sourceLibraries) {
            Program libraryProgram = parseProgram(readFile(library), library);
            mergeLibrary(program, libraryProgram);
        }
        for (const std::string &library : optimizationLibraries) {
            Program libraryProgram = parseProgram(readFile(library), library);
            mergeOptimizationLibrary(program, libraryProgram);
        }
        auto interpreter = makeInterpreter(program, mode, file, strictIfc, explainOptimizations, traceOwnership);
        if (command == "check") {
            if (useJit || dumpLlvm) {
                std::cerr << "error: --jit/--dump-llvm は run コマンドでのみ使用できます\n";
                return 2;
            }
            checkInterpreter(*interpreter);
        } else if (command == "serve") {
            if (useJit || dumpLlvm) {
                std::cerr << "error: --jit/--dump-llvm は serve では使用できません\n";
                return 2;
            }
            if (!entry.empty()) {
                std::cerr << "error: serve では --entry を指定できません\n";
                return 2;
            }
            validateInterpreter(*interpreter);
            HttpServer server(*interpreter, bindAddress, port, maxBodyBytes, once);
            server.run();
        } else {
            if (useJit) {
                validateInterpreter(*interpreter);
                runJit(program, entry, JitOptions{dumpLlvm, mode == Mode::Verified, mode == Mode::Unsafe});
            } else {
                runInterpreter(*interpreter, entry);
            }
        }
        return 0;
    } catch (const KondError &error) {
        std::cerr << "error[" << error.code << "]: " << error.what() << "\n"
                  << "  --> " << error.pos.file << ":" << error.pos.line << ":" << error.pos.column << "\n";
        return 1;
    } catch (const std::exception &error) {
        std::cerr << "error: " << error.what() << "\n";
        return 1;
    }
}
