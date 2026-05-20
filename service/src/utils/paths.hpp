#pragma once

#include <filesystem>
#include <source_location>
#include <stdexcept>

namespace imesvc {

inline std::filesystem::path find_project_root_from(std::filesystem::path start) {
    if (start.empty()) {
        start = std::filesystem::current_path();
    }

    if (start.is_relative()) {
        start = std::filesystem::absolute(start);
    }

    start = start.lexically_normal();
    if (start.has_filename()) {
        start = start.parent_path();
    }

    for (auto dir = start; !dir.empty(); dir = dir.parent_path()) {
        if (std::filesystem::exists(dir / "tables") && std::filesystem::exists(dir / "models")) {
            return dir;
        }

        if (dir == dir.root_path()) {
            break;
        }
    }

    throw std::runtime_error("project root not found from: " + start.string());
}

inline std::filesystem::path project_root(std::source_location loc = std::source_location::current()) {
    try {
        return find_project_root_from(loc.file_name());
    } catch (const std::exception&) {
        return find_project_root_from(std::filesystem::current_path());
    }
}

}  // namespace imesvc
