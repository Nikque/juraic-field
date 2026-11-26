#pragma once
// Files the game writes whole or not at all, without SDL (play/platform_sdl.h calls them on Android): the files that
// come with the game, copied out of the APK into the own folder of the app, and a file handed over by the system.
#include <charconv>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <mutex>
#include <optional>
#include <set>
#include <sstream>
#include <string>

namespace play {

// Writes `size` bytes to `file`, replacing what was there. False when they could not be written whole.
inline bool writeWhole(const std::filesystem::path& file, const void* bytes, size_t size) {
    std::ofstream out(file, std::ios::binary | std::ios::trunc);
    out.write(static_cast<const char*>(bytes), std::streamsize(size));
    out.close();
    return bool(out);
}

using LegacyGameFiles = std::set<std::filesystem::path>;

namespace game_files_detail {

template <class Visit> bool visitManifest(const std::string& manifest, bool hasLegacyFormat, Visit&& visit) {
    std::istringstream lines(manifest);
    std::string line;
    if (!std::getline(lines, line) || !line.starts_with("version ")) return false;
    const auto lineStart = lines.tellg();
    if (!std::getline(lines, line) || !line.starts_with("digest ")) {
        if (!hasLegacyFormat || lineStart == std::streampos(-1)) return false;
        lines.clear();
        lines.seekg(lineStart);
    }
    bool any = false;
    while (std::getline(lines, line)) {
        if (!line.empty() && line.back() == '\r') line.pop_back();
        const size_t space = line.rfind(' ');
        if (space == std::string::npos || space == 0) return false;
        const std::string name = line.substr(0, space);
        const auto file = std::filesystem::path(name);
        if (file.has_root_path() || file.filename().empty() || name.find('\\') != std::string::npos ||
            name.find('\0') != std::string::npos || file == "files.txt") return false;
        for (const auto& part : file)
            if (part == "." || part == "..") return false;
        size_t size = 0;
        const char* const end = line.data() + line.size();
        const auto parsed = std::from_chars(line.data() + space + 1, end, size);
        if (parsed.ec != std::errc{} || parsed.ptr != end || !visit(name, file, size)) return false;
        any = true;
    }
    return any;
}

inline std::mutex installation;

inline bool hasLinkedRoot(const std::filesystem::path& cache) {
    std::error_code error;
    return std::filesystem::is_symlink(std::filesystem::symlink_status(cache, error)) ||
           std::filesystem::is_symlink(std::filesystem::symlink_status(cache.parent_path(), error));
}

} // namespace game_files_detail

// The old APK put its assets straight under game/. Read them in place if the new APK cannot
// install a complete generation. The list is an allowlist, as the old directory can contain
// assets already removed from the APK. This fallback needs no disk space.
inline std::optional<LegacyGameFiles> legacyGameFiles(const std::filesystem::path& directory) {
    std::error_code error;
    if (!std::filesystem::is_directory(std::filesystem::symlink_status(directory, error))) return std::nullopt;
    if (!std::filesystem::is_regular_file(std::filesystem::symlink_status(directory / "files.txt", error))) return std::nullopt;
    std::ifstream input(directory / "files.txt", std::ios::binary);
    if (!input) return std::nullopt;
    const std::string manifest((std::istreambuf_iterator<char>(input)), std::istreambuf_iterator<char>());
    LegacyGameFiles files;
    const auto verified = [&](const std::string&, const std::filesystem::path& relative, size_t size) {
        auto file = directory;
        for (const auto& part : relative) {
            file /= part;
            if (std::filesystem::is_symlink(std::filesystem::symlink_status(file, error)) || error) return false;
        }
        return std::filesystem::is_regular_file(file, error) && std::filesystem::file_size(file, error) == size &&
               !error && files.insert(relative).second;
    };
    if (!game_files_detail::visitManifest(manifest, true, verified)) return std::nullopt;
    return files;
}

// Only a directory with a completed manifest can be used. The previous generation is kept
// across the two renames that publish an update, so a restart between them still has a full set.
inline std::filesystem::path gameFilesDirectory(const std::filesystem::path& cache) {
    if (game_files_detail::hasLinkedRoot(cache)) return {};
    std::error_code error;
    for (const char* name : {"current", "previous"}) {
        const auto generation = cache / name;
        if (std::filesystem::is_directory(std::filesystem::symlink_status(generation, error)) &&
            std::filesystem::is_regular_file(std::filesystem::symlink_status(generation / "files.txt", error))) return generation;
    }
    return {}; // No full generation has been installed.
}

template <class Load> bool copyGameFiles(const std::string& wanted, const std::filesystem::path& cache, Load&& load) {
    const std::lock_guard lock(game_files_detail::installation);
    if (game_files_detail::hasLinkedRoot(cache)) return false;
    std::error_code error;
    const auto current = cache / "current", previous = cache / "previous", incoming = cache / "incoming";
    const auto active = gameFilesDirectory(cache);
    if (!active.empty()) {
        std::ifstream had(active / "files.txt", std::ios::binary);
        const std::string have((std::istreambuf_iterator<char>(had)), std::istreambuf_iterator<char>());
        if (had && have == wanted) return true;
    }
    std::filesystem::remove_all(incoming, error);
    if (error || !std::filesystem::create_directories(incoming, error) || error) return false;

    const auto copy = [&](const std::string& name, const std::filesystem::path& file, size_t size) {
        std::string bytes;
        if (!load(name, bytes) || bytes.size() != size) return false;
        const auto destination = incoming / file;
        std::filesystem::create_directories(destination.parent_path(), error);
        return !error && writeWhole(destination, bytes.data(), bytes.size());
    };
    if (!game_files_detail::visitManifest(wanted, false, copy)) return false;
    if (!writeWhole(incoming / "files.txt", wanted.data(), wanted.size())) return false;

    if (active == current) {
        std::filesystem::remove_all(previous, error);
        if (error) return false;
        std::filesystem::rename(current, previous, error);
        if (error) return false;
    } else {
        // Remove an unusable current directory, keeping the complete previous generation (if any).
        std::filesystem::remove_all(current, error);
        if (error) return false;
    }
    std::filesystem::rename(incoming, current, error);
    if (error) return false;
    // The new generation is already active. A failed cleanup can be retried on a later update.
    std::filesystem::remove_all(previous, error);
    return true;
}

} // namespace play
