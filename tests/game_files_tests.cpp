// Tests of complete Android asset generations (play/game_files.h):
// an interrupted update or promotion keeps the last complete set, a successful update replaces the set,
// and a repeat with the same manifest does not copy it again.
#include "../play/game_files.h"
#include <iostream>
#include <map>
#include <stdexcept>

using namespace play;

static void check(bool ok, const std::string& message) {
    if (!ok) throw std::runtime_error(message);
}
static std::string contents(const std::filesystem::path& file) {
    std::ifstream in(file, std::ios::binary);
    return std::string((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
}

// The files of an APK: name -> contents; a name missing cannot be read.
struct Apk {
    std::map<std::string, std::string> files;
    int read = 0;
    bool operator()(const std::string& name, std::string& bytes) {
        const auto at = files.find(name);
        if (at == files.end()) return false;
        bytes = at->second, ++read;
        return true;
    }
};

int main() {
    try {
        const std::filesystem::path data = std::filesystem::absolute("game-files-tests");
        std::filesystem::remove_all(data);
        std::filesystem::create_directories(data);
        const std::string listA = "version 1\ndigest aa\nbgm/a.flac 2\nfonts/f.ttf 2\n";
        const std::string listB = "version 2\ndigest bb\nbackground/b.png 2\nfonts/f.ttf 2\n";
        const auto active = [&] { return gameFilesDirectory(data); };
        const auto seesA = [&] {
            return contents(active() / "files.txt") == listA && contents(active() / "bgm/a.flac") == "A1" &&
                   contents(active() / "fonts/f.ttf") == "A2" && !std::filesystem::exists(active() / "background/b.png");
        };
        Apk a{{{"bgm/a.flac", "A1"}, {"fonts/f.ttf", "A2"}}};
        Apk b{{{"background/b.png", "B1"}, {"fonts/f.ttf", "B2"}}};
        Apk interrupted{{{"background/b.png", "B1"}, {"fonts/f.ttf", "x"}}};
        check(!copyGameFiles(listB, data, interrupted) && active().empty(),
              "an incomplete first installation was published");
        check(copyGameFiles(listA, data, a) && seesA(), "version A was not published");
        Apk same{a.files};
        check(copyGameFiles(listA, data, same) && same.read == 0, "the same list copied the files again");
        check(!copyGameFiles(listB, data, interrupted) && seesA(),
              "a failed update hid A or exposed part of B");
        std::filesystem::rename(active(), data / "previous");
        check(seesA(), "an interruption between the promotion renames lost the last full set");
        check(copyGameFiles(listB, data, b) && contents(active() / "files.txt") == listB &&
              contents(active() / "background/b.png") == "B1" && contents(active() / "fonts/f.ttf") == "B2" &&
              !std::filesystem::exists(active() / "bgm/a.flac"), "version B still shows a file from A");
        check(copyGameFiles(listA, data, a) && seesA(), "rolling back to version A still shows a file from B");

        // A manifest that needs a directory under a file cannot install a complete generation.
        Apk blocked{{{"fonts/f.ttf", "C1"}, {"fonts/f.ttf/child", "C2"}}};
        check(!copyGameFiles("version 3\ndigest cc\nfonts/f.ttf 2\nfonts/f.ttf/child 2\n", data, blocked) && seesA(),
              "a failed file write replaced the previous generation");
        check(!writeWhole(active() / "fonts", "x", 1) && writeWhole(data / "one", "xy", 2) && contents(data / "one") == "xy",
              "writeWhole does not tell a file written from one that could not be");
        const auto old = data / "legacy-install" / "game";
        const auto newCache = old / "packaged";
        const auto user = old.parent_path() / "user";
        std::filesystem::create_directories(old / "bgm");
        std::filesystem::create_directories(old / "fonts");
        std::filesystem::create_directories(user);
        const auto write = [](const std::filesystem::path& file, const std::string& text) {
            return writeWhole(file, text.data(), text.size());
        };
        check(write(old / "bgm/a.flac", "A1") && write(old / "bgm/removed.flac", "unused") &&
              write(old / "fonts/f.ttf", "A2") && write(user / "user.flac", "U1"), "the legacy fixture could not be written");
        for (const auto& manifest : {std::string("version 0\nbgm/a.flac 2\nfonts/f.ttf 2\n"), listA}) {
            check(write(old / "files.txt", manifest), "the legacy manifest could not be written");
            check(!copyGameFiles(listB, newCache, interrupted) && gameFilesDirectory(newCache).empty(),
                  "an interrupted first upgrade was published");
            const auto legacy = legacyGameFiles(old);
            check(legacy && legacy->contains("bgm/a.flac") && legacy->contains("fonts/f.ttf") &&
                  !legacy->contains("bgm/removed.flac") && legacy->size() == 2,
                  "the fallback lost listed legacy media or included unlisted media");
        }
        check(write(old / "fonts/f.ttf", "x") && !legacyGameFiles(old),
              "a partial legacy installation was accepted");
        check(write(old / "fonts/f.ttf", "A2") &&
              write(old / "files.txt", "version 0\nbgm/a.flac 2\n../user/user.flac 2\n") && !legacyGameFiles(old),
              "the legacy list accepted an entry outside the package");
        check(write(old / "files.txt", listA) && copyGameFiles(listB, newCache, b) &&
              contents(gameFilesDirectory(newCache) / "background/b.png") == "B1" &&
              !std::filesystem::exists(gameFilesDirectory(newCache) / "bgm/a.flac") &&
              contents(old / "bgm/a.flac") == "A1" && contents(old / "bgm/removed.flac") == "unused" &&
              contents(user / "user.flac") == "U1", "publishing the first new set changed existing legacy or user files");
        std::cout << "game files tests passed\n";
        return 0;
    } catch (const std::exception& e) {
        std::cerr << "FAILED: " << e.what() << '\n';
        return 1;
    }
}
