#include <algorithm>
#include <array>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <string>
#include <system_error>
#include <utility>
#include <vector>

namespace fs = std::filesystem;

struct CopyAction {
    fs::path source;
    fs::path destination;
    fs::path relative;
};

bool isWithin(const fs::path& child, const fs::path& parent) {
    auto childPart = child.begin();
    for (auto parentPart = parent.begin(); parentPart != parent.end(); ++parentPart, ++childPart) {
        if (childPart == child.end() || *childPart != *parentPart) {
            return false;
        }
    }
    return true;
}

bool sameContents(const fs::path& leftPath, const fs::path& rightPath) {
    if (fs::file_size(leftPath) != fs::file_size(rightPath)) {
        return false;
    }

    std::ifstream left(leftPath, std::ios::binary);
    std::ifstream right(rightPath, std::ios::binary);
    if (!left || !right) {
        throw std::runtime_error("Could not open a file for comparison.");
    }

    std::array<char, 64 * 1024> leftBuffer{};
    std::array<char, 64 * 1024> rightBuffer{};
    while (left && right) {
        left.read(leftBuffer.data(), static_cast<std::streamsize>(leftBuffer.size()));
        right.read(rightBuffer.data(), static_cast<std::streamsize>(rightBuffer.size()));
        const auto leftCount = left.gcount();
        const auto rightCount = right.gcount();
        if (leftCount != rightCount ||
            !std::equal(leftBuffer.begin(), leftBuffer.begin() + leftCount, rightBuffer.begin())) {
            return false;
        }
    }
    return left.eof() && right.eof();
}

int syncDirectories(const fs::path& sourceArg, const fs::path& destinationArg, bool apply) {
    const fs::path source = fs::canonical(sourceArg);
    const fs::path destination = fs::weakly_canonical(fs::absolute(destinationArg));

    if (!fs::is_directory(source)) {
        std::cerr << "Error: source directory does not exist: " << source << '\n';
        return 2;
    }
    if (isWithin(destination, source) || isWithin(source, destination)) {
        std::cerr << "Error: source and destination must be separate, non-nested directories.\n";
        return 2;
    }

    std::vector<CopyAction> actions;
    for (const auto& entry : fs::recursive_directory_iterator(source)) {
        const auto sourcePath = entry.path();
        const auto relative = fs::relative(sourcePath, source);
        if (entry.is_symlink()) {
            std::cout << "Skip symlink: " << relative.generic_string() << '\n';
            continue;
        }
        if (!entry.is_regular_file()) {
            continue;
        }

        const fs::path target = destination / relative;
        std::error_code error;
        const auto targetStatus = fs::symlink_status(target, error);
        if (error == std::errc::no_such_file_or_directory) {
            error.clear();
        }
        if (error) {
            throw fs::filesystem_error("Could not inspect destination path", target, error);
        }
        if (fs::is_symlink(targetStatus) ||
            (fs::exists(targetStatus) && !fs::is_regular_file(targetStatus))) {
            std::cerr << "Error: destination path is not a regular file: " << target << '\n';
            return 2;
        }

        if (!fs::exists(targetStatus) || !sameContents(sourcePath, target)) {
            actions.push_back({sourcePath, target, relative});
        }
    }

    if (actions.empty()) {
        std::cout << "Already in sync; no files to copy.\n";
        return 0;
    }

    for (const auto& action : actions) {
        if (!apply) {
            std::cout << "Would copy: " << action.relative.generic_string() << '\n';
            continue;
        }

        fs::create_directories(action.destination.parent_path());
        fs::copy_file(action.source, action.destination, fs::copy_options::overwrite_existing);
        std::cout << "Copied: " << action.relative.generic_string() << '\n';
    }

    if (apply) {
        std::cout << "Sync complete: " << actions.size() << " file(s) copied.\n";
    } else {
        std::cout << "Dry run: " << actions.size()
                  << " file(s) would be copied. Add --apply to sync.\n";
    }
    return 0;
}

int main(int argc, char* argv[]) {
    if (argc != 3 && argc != 4) {
        std::cerr << "Usage: filesync <source> <destination> [--apply]\n";
        return 2;
    }

    const bool apply = argc == 4 && std::string(argv[3]) == "--apply";
    if (argc == 4 && !apply) {
        std::cerr << "Error: unknown option: " << argv[3] << '\n';
        return 2;
    }

    try {
        return syncDirectories(argv[1], argv[2], apply);
    } catch (const std::exception& error) {
        std::cerr << "Error: " << error.what() << '\n';
        return 2;
    }
}
