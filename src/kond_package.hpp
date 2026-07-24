#pragma once

#include "kond_value.hpp"

#include <filesystem>

namespace kond {

struct PackageDependency {
    std::string path;
    std::string version;
};

struct PackageManifest {
    std::filesystem::path root;
    std::string name;
    std::string version = "0.1.0";
    std::string entry = "main.kd";
    std::string library;
    std::map<std::string, PackageDependency> dependencies;
};

struct PackageNode {
    PackageManifest manifest;
};

struct PackageGraph {
    PackageManifest root;
    std::vector<PackageNode> dependencies;
};

PackageManifest loadPackageManifest(const std::filesystem::path &directory);
PackageGraph resolvePackageGraph(const std::filesystem::path &directory);
void validatePackageLock(const PackageGraph &graph);
std::filesystem::path packageEntryFile(const PackageManifest &manifest);
std::filesystem::path packageLibraryFile(const PackageManifest &manifest);
std::vector<std::filesystem::path> packageLibraryFiles(const PackageGraph &graph);

void writePackageManifest(const PackageManifest &manifest);
void writePackageLock(const PackageGraph &graph);
void addLocalDependency(const std::filesystem::path &projectDirectory,
                        const std::filesystem::path &dependencyDirectory);
void removeDependency(const std::filesystem::path &projectDirectory, const std::string &name);

int createPackageProject(const std::string &rawPath);

} // namespace kond
