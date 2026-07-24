#pragma once

#include "kond_package.hpp"

namespace kond {

inline constexpr std::string_view kDefaultRegistryUrl = "http://kond.j9.si";

void runPackageRegistryServer(const std::filesystem::path &storageDirectory,
                              const std::string &bindAddress,
                              std::uint16_t port,
                              std::size_t maxBodyBytes,
                              bool once);

void publishPackage(const std::filesystem::path &projectDirectory,
                    const std::string &registryUrl);

void fetchPackage(const std::filesystem::path &projectDirectory,
                  const std::string &name,
                  const std::string &version,
                  const std::string &registryUrl);

} // namespace kond
