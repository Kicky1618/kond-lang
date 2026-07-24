#include "kond_package.hpp"

#include <fstream>
#include <functional>
#include <set>

namespace kond {
namespace {

namespace fs = std::filesystem;

[[noreturn]] void packageError(const std::string &message) {
    throw std::runtime_error("パッケージ: " + message);
}

std::string readText(const fs::path &path) {
    std::ifstream input(path);
    if (!input) packageError("ファイルを開けません: " + path.string());
    std::ostringstream contents;
    contents << input.rdbuf();
    if (!input.good() && !input.eof()) packageError("ファイルを読み取れません: " + path.string());
    return contents.str();
}

fs::path canonicalDirectory(const fs::path &input) {
    fs::path candidate = input;
    std::error_code error;
    if (fs::is_regular_file(candidate, error) && candidate.filename() == "kond.json") {
        candidate = candidate.parent_path();
    }
    error.clear();
    if (!fs::is_directory(candidate, error)) {
        if (error) packageError("ディレクトリを確認できません: " + candidate.string());
        packageError("ディレクトリがありません: " + candidate.string());
    }
    fs::path absolute = fs::absolute(candidate, error);
    if (error) packageError("ディレクトリを解決できません: " + candidate.string());
    fs::path canonical = fs::weakly_canonical(absolute, error);
    if (error) packageError("ディレクトリを正規化できません: " + candidate.string());
    return canonical;
}

const std::map<std::string, Value> &objectFields(const Value &value, const std::string &where) {
    if (value.kind != ValueKind::Object || !value.object) {
        packageError(where + " はJSON objectである必要があります");
    }
    return *value.object;
}

const Value *findField(const std::map<std::string, Value> &fields, const std::string &name) {
    const auto found = fields.find(name);
    return found == fields.end() ? nullptr : &found->second;
}

std::string stringField(const std::map<std::string, Value> &fields, const std::string &name,
                       const std::string &defaultValue, bool required, const std::string &where) {
    const Value *value = findField(fields, name);
    if (!value) {
        if (required) packageError(where + " に必須フィールド '" + name + "' がありません");
        return defaultValue;
    }
    if (value->kind != ValueKind::String) {
        packageError(where + "." + name + " はStringである必要があります");
    }
    return value->string;
}

void validateProjectRelativeFile(const std::string &path, const std::string &field) {
    if (path.empty()) packageError(field + " は空にできません");
    const fs::path candidate(path);
    if (candidate.is_absolute()) packageError(field + " はプロジェクト相対パスである必要があります");
    for (const auto &part : candidate) {
        if (part == "..") packageError(field + " はプロジェクトの外側を参照できません");
    }
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

void writeText(const fs::path &path, const std::string &contents) {
    std::ofstream output(path, std::ios::out | std::ios::trunc);
    if (!output) packageError("ファイルを生成できません: " + path.string());
    output << contents;
    if (!output) packageError("ファイルを書き込めません: " + path.string());
}

std::string relativePackagePath(const fs::path &from, const fs::path &to) {
    std::error_code error;
    const fs::path relative = fs::relative(to, from, error);
    if (error || relative.empty()) return to.generic_string();
    return relative.generic_string();
}

} // namespace

PackageManifest loadPackageManifest(const fs::path &directory) {
    PackageManifest manifest;
    manifest.root = canonicalDirectory(directory);
    const fs::path manifestPath = manifest.root / "kond.json";
    std::error_code error;
    if (!fs::is_regular_file(manifestPath, error)) {
        packageError("kond.json がありません: " + manifest.root.string());
    }

    Value document;
    try {
        document = JsonParser(readText(manifestPath),
                              SourcePos{manifestPath.string(), 0, 1, 1}).parse();
    } catch (const KondError &parseError) {
        packageError("kond.json のJSONが不正です: " + std::string(parseError.what()));
    }
    const auto fields = objectFields(document, manifestPath.string());
    manifest.name = stringField(fields, "name", {}, true, manifestPath.string());
    if (manifest.name.empty() || manifest.name == "." || manifest.name == "..") {
        packageError(manifestPath.string() + ".name は空または '.'/'..' にできません");
    }
    manifest.version = stringField(fields, "version", "0.1.0", false, manifestPath.string());
    manifest.entry = stringField(fields, "entry", "main.kd", false, manifestPath.string());
    manifest.library = stringField(fields, "library", {}, false, manifestPath.string());
    validateProjectRelativeFile(manifest.entry, "entry");
    if (!manifest.library.empty()) validateProjectRelativeFile(manifest.library, "library");

    const Value *nativeValue = findField(fields, "native");
    if (nativeValue) {
        if (nativeValue->kind != ValueKind::Array || !nativeValue->array) {
            packageError(manifestPath.string() + ".native はJSON arrayである必要があります");
        }
        std::set<std::string> nativeNames;
        for (const Value &item : *nativeValue->array) {
            if (item.kind != ValueKind::String) {
                packageError(manifestPath.string() + ".native の各要素はStringである必要があります");
            }
            validateProjectRelativeFile(item.string, manifestPath.string() + ".native");
            if (!nativeNames.insert(item.string).second) {
                packageError(manifestPath.string() + ".native に重複したファイルがあります: " + item.string);
            }
            manifest.nativeFiles.push_back(item.string);
        }
    }

    const Value *dependencyValue = findField(fields, "dependencies");
    if (!dependencyValue) return manifest;
    const auto dependencies = objectFields(*dependencyValue, manifestPath.string() + ".dependencies");
    for (const auto &entry : dependencies) {
        PackageDependency dependency;
        if (entry.second.kind == ValueKind::String) {
            dependency.path = entry.second.string;
        } else {
            const auto dependencyFields = objectFields(entry.second,
                                                       manifestPath.string() + ".dependencies." + entry.first);
            dependency.path = stringField(dependencyFields, "path", {}, true,
                                          manifestPath.string() + ".dependencies." + entry.first);
            dependency.version = stringField(dependencyFields, "version", {}, false,
                                              manifestPath.string() + ".dependencies." + entry.first);
        }
        if (dependency.path.empty()) {
            packageError("依存パッケージ '" + entry.first + "' のpathが空です");
        }
        manifest.dependencies.emplace(entry.first, std::move(dependency));
    }
    return manifest;
}

PackageGraph resolvePackageGraph(const fs::path &directory) {
    PackageGraph graph;
    graph.root = loadPackageManifest(directory);

    std::map<std::string, fs::path> packageNames;
    std::set<std::string> active;
    std::set<std::string> visited;
    packageNames.emplace(graph.root.name, graph.root.root);
    active.insert(graph.root.root.generic_string());

    std::function<void(const PackageManifest &)> visit = [&](const PackageManifest &parent) {
        for (const auto &dependencyEntry : parent.dependencies) {
            const std::string &dependencyName = dependencyEntry.first;
            const PackageDependency &dependency = dependencyEntry.second;
            const PackageManifest child = loadPackageManifest(parent.root / dependency.path);
            if (child.name != dependencyName) {
                packageError("依存パッケージ名が一致しません: 要求=" + dependencyName +
                             ", 実体=" + child.name);
            }
            if (!dependency.version.empty() && dependency.version != child.version) {
                packageError("依存パッケージ '" + dependencyName + "' のversionが一致しません: 要求=" +
                             dependency.version + ", 実体=" + child.version);
            }
            const std::string key = child.root.generic_string();
            if (active.count(key) != 0) {
                packageError("依存関係に循環があります: " + dependencyName);
            }
            const auto named = packageNames.find(child.name);
            if (named != packageNames.end() && named->second != child.root) {
                packageError("同じ名前の別パッケージが依存関係にあります: " + child.name);
            }
            packageNames.emplace(child.name, child.root);
            if (visited.count(key) != 0) continue;

            active.insert(key);
            visit(child);
            active.erase(key);
            visited.insert(key);
            graph.dependencies.push_back(PackageNode{child});
        }
    };

    visit(graph.root);
    return graph;
}

void validatePackageLock(const PackageGraph &graph) {
    const fs::path lockPath = graph.root.root / "kond.lock";
    std::error_code error;
    const bool lockExists = fs::exists(lockPath, error);
    if (error) packageError("kond.lockを確認できません: " + lockPath.string());
    if (!lockExists) return;
    if (error || !fs::is_regular_file(lockPath, error)) {
        packageError("kond.lock を読み取れません: " + lockPath.string());
    }

    Value document;
    try {
        document = JsonParser(readText(lockPath), SourcePos{lockPath.string(), 0, 1, 1}).parse();
    } catch (const KondError &parseError) {
        packageError("kond.lock のJSONが不正です: " + std::string(parseError.what()));
    }
    const auto fields = objectFields(document, lockPath.string());
    const Value *lockVersion = findField(fields, "lockfileVersion");
    if (!lockVersion || lockVersion->kind != ValueKind::Integer || lockVersion->integer != 1) {
        packageError("対応していないkond.lockのバージョンです");
    }
    const Value *rootValue = findField(fields, "root");
    const auto rootFields = rootValue ? objectFields(*rootValue, lockPath.string() + ".root")
                                      : std::map<std::string, Value>{};
    if (stringField(rootFields, "name", {}, true, lockPath.string() + ".root") != graph.root.name ||
        stringField(rootFields, "version", {}, true, lockPath.string() + ".root") != graph.root.version) {
        packageError("kond.lock のrootがkond.jsonと一致しません。kond installを実行してください");
    }

    const Value *packagesValue = findField(fields, "packages");
    const auto packages = packagesValue ? objectFields(*packagesValue, lockPath.string() + ".packages")
                                        : std::map<std::string, Value>{};
    if (packages.size() != graph.dependencies.size()) {
        packageError("kond.lockと依存関係が一致しません。kond installを実行してください");
    }
    for (const PackageNode &node : graph.dependencies) {
        const Value *packageValue = findField(packages, node.manifest.name);
        if (!packageValue) {
            packageError("kond.lockに依存パッケージがありません: " + node.manifest.name);
        }
        const auto packageFields = objectFields(*packageValue,
                                                lockPath.string() + ".packages." + node.manifest.name);
        const std::string version = stringField(packageFields, "version", {}, true,
                                                 lockPath.string() + ".packages." + node.manifest.name);
        const std::string path = stringField(packageFields, "path", {}, true,
                                              lockPath.string() + ".packages." + node.manifest.name);
        if (version != node.manifest.version ||
            path != relativePackagePath(graph.root.root, node.manifest.root)) {
            packageError("kond.lockと依存パッケージが一致しません。kond installを実行してください");
        }
    }
}

fs::path packageEntryFile(const PackageManifest &manifest) {
    const fs::path path = manifest.root / manifest.entry;
    std::error_code error;
    if (!fs::is_regular_file(path, error)) {
        packageError("entryファイルがありません: " + path.string());
    }
    return path;
}

fs::path packageLibraryFile(const PackageManifest &manifest) {
    const fs::path path = manifest.root / (manifest.library.empty() ? manifest.entry : manifest.library);
    std::error_code error;
    if (!fs::is_regular_file(path, error)) {
        packageError("libraryファイルがありません: " + path.string());
    }
    return path;
}

std::vector<fs::path> packageNativeFiles(const PackageManifest &manifest) {
    std::vector<fs::path> result;
    result.reserve(manifest.nativeFiles.size());
    for (const std::string &nativeFile : manifest.nativeFiles) {
        const fs::path path = manifest.root / nativeFile;
        std::error_code error;
        if (!fs::is_regular_file(path, error)) {
            packageError("nativeファイルがありません: " + path.string());
        }
        result.push_back(path);
    }
    return result;
}

std::vector<fs::path> packageLibraryFiles(const PackageGraph &graph) {
    std::vector<fs::path> result;
    result.reserve(graph.dependencies.size());
    for (const PackageNode &node : graph.dependencies) {
        (void)packageNativeFiles(node.manifest);
        result.push_back(packageLibraryFile(node.manifest));
    }
    return result;
}

void writePackageManifest(const PackageManifest &manifest) {
    std::ostringstream output;
    output << "{\n"
           << "  \"name\": " << jsonQuoted(manifest.name) << ",\n"
           << "  \"version\": " << jsonQuoted(manifest.version) << ",\n"
           << "  \"entry\": " << jsonQuoted(manifest.entry);
    if (!manifest.library.empty()) output << ",\n  \"library\": " << jsonQuoted(manifest.library);
    if (!manifest.nativeFiles.empty()) {
        output << ",\n  \"native\": [";
        for (std::size_t i = 0; i < manifest.nativeFiles.size(); ++i) {
            if (i != 0) output << ", ";
            output << jsonQuoted(manifest.nativeFiles[i]);
        }
        output << "]";
    }
    output << ",\n  \"dependencies\": {";
    if (!manifest.dependencies.empty()) output << '\n';
    bool first = true;
    for (const auto &entry : manifest.dependencies) {
        if (!first) output << ",\n";
        first = false;
        output << "    " << jsonQuoted(entry.first) << ": {\"path\": " << jsonQuoted(entry.second.path);
        if (!entry.second.version.empty()) output << ", \"version\": " << jsonQuoted(entry.second.version);
        output << "}";
    }
    if (!manifest.dependencies.empty()) output << '\n' << "  ";
    output << "}\n}\n";
    writeText(manifest.root / "kond.json", output.str());
}

void writePackageLock(const PackageGraph &graph) {
    std::ostringstream output;
    output << "{\n"
           << "  \"lockfileVersion\": 1,\n"
           << "  \"root\": {\"name\": " << jsonQuoted(graph.root.name)
           << ", \"version\": " << jsonQuoted(graph.root.version) << "},\n"
           << "  \"packages\": {";
    if (!graph.dependencies.empty()) output << '\n';
    bool first = true;
    for (const PackageNode &node : graph.dependencies) {
        if (!first) output << ",\n";
        first = false;
        output << "    " << jsonQuoted(node.manifest.name) << ": {\"version\": "
               << jsonQuoted(node.manifest.version) << ", \"path\": "
               << jsonQuoted(relativePackagePath(graph.root.root, node.manifest.root)) << "}";
    }
    if (!graph.dependencies.empty()) output << '\n' << "  ";
    output << "}\n}\n";
    writeText(graph.root.root / "kond.lock", output.str());
}

void addLocalDependency(const fs::path &projectDirectory, const fs::path &dependencyDirectory) {
    PackageManifest project = loadPackageManifest(projectDirectory);
    const PackageManifest dependency = loadPackageManifest(dependencyDirectory);
    if (project.root == dependency.root) packageError("プロジェクト自身を依存関係に追加できません");
    if (project.name == dependency.name) packageError("プロジェクト自身と同じ名前の依存関係です: " + dependency.name);
    (void)packageLibraryFile(dependency);
    (void)packageNativeFiles(dependency);

    PackageDependency specification;
    specification.path = relativePackagePath(project.root, dependency.root);
    specification.version = dependency.version;
    project.dependencies[dependency.name] = std::move(specification);
    writePackageManifest(project);
}

void removeDependency(const fs::path &projectDirectory, const std::string &name) {
    PackageManifest project = loadPackageManifest(projectDirectory);
    const auto found = project.dependencies.find(name);
    if (found == project.dependencies.end()) packageError("依存パッケージがありません: " + name);
    project.dependencies.erase(found);
    writePackageManifest(project);
}

int createPackageProject(const std::string &rawPath) {
    const fs::path projectPath(rawPath);
    std::error_code error;
    const bool alreadyExists = fs::exists(projectPath, error);
    if (error) packageError("生成先を確認できません: " + projectPath.string());
    if (alreadyExists && !fs::is_directory(projectPath, error)) {
        packageError("生成先がディレクトリではありません: " + projectPath.string());
    }
    if (error) packageError("生成先を確認できません: " + projectPath.string());

    if (alreadyExists) {
        fs::directory_iterator entries(projectPath, error);
        if (error) packageError("生成先を読み取れません: " + projectPath.string());
        if (entries != fs::directory_iterator()) {
            packageError("生成先のディレクトリが空ではありません: " + projectPath.string());
        }
    } else if (!fs::create_directories(projectPath, error) && error) {
        packageError("プロジェクトディレクトリを作成できません: " + projectPath.string());
    }

    const fs::path absoluteProject = canonicalDirectory(projectPath);
    const std::string projectName = absoluteProject.filename().empty()
                                        ? "kond-project"
                                        : absoluteProject.filename().string();
    PackageManifest manifest;
    manifest.root = absoluteProject;
    manifest.name = projectName;
    writeText(absoluteProject / "main.kd",
              "fn main() {\n"
              "    print(\"Hello, Kond!\")\n"
              "}\n");
    writePackageManifest(manifest);
    writeText(absoluteProject / "README.md",
              "# " + projectName + "\n\n"
              "A Kond project.\n\n"
              "```sh\n"
              "kond install\n"
              "kond check main.kd\n"
              "kond run main.kd\n"
              "```\n");

    std::cout << "kond: created project " << projectPath.string() << "\n"
              << "  kond.json\n"
              << "  main.kd\n"
              << "  README.md\n";
    return 0;
}

} // namespace kond
