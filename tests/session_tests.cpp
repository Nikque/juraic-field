// Tests of the seating of an AI in a match of the game (play/session.h, Versus::seat).
//
//  1. the time of a choice the AI of a seat is given (Params::thinkBudget): in a match played live, from the settings
//     aiLead and aiBudget - a millisecond at least, also for aiLead 1 read from the settings file (the screen steps it
//     by two), never 0 or less, which is a search without a clock; in a self-test (not live), none.
#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX // (play/udp.h takes in winsock2.h, whose min and max would stand for std::min and std::max)
#endif
#endif
#include "../play/session.h"
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <string>

using namespace play;

static void check(bool ok, const std::string& message) {
    if (!ok) throw std::runtime_error(message);
}

// The time of a choice the AI of seat 0 is given, as Versus::seat makes its settings.
static int budgetOf(const Settings& s, bool live) {
    Versus v;
    int budget = -1000;
    v.adjust = [&budget](int, juraic::ai::Params& p) { budget = p.thinkBudget; };
    v.seat(0, "", s, live);
    return budget;
}

static void testSeatBudget() {
    Settings s;
    s.aiBudget = 180;
    s.aiLead = 0;
    check(budgetOf(s, true) == 180, "aiLead 0: the choice has " + std::to_string(budgetOf(s, true)) + " ms, not the time set");
    s.aiLead = 1;
    check(budgetOf(s, true) == 1, "aiLead 1: the choice has " + std::to_string(budgetOf(s, true)) + " ms");
    s.aiLead = 2;
    check(budgetOf(s, true) == 13, "aiLead 2: the choice has " + std::to_string(budgetOf(s, true)) + " ms");
    s.aiLead = 12;
    check(budgetOf(s, true) == 180, "aiLead 12: the choice has " + std::to_string(budgetOf(s, true)) + " ms, not the time set");
    check(budgetOf(s, false) == 0, "a self-test: the choice has a clock");
    // aiLead 1 as the settings file may hold it.
    const std::filesystem::path folder = "session-tests";
    std::filesystem::create_directories(folder);
    const std::filesystem::path file = folder / "lead.settings";
    std::ofstream(file, std::ios::binary) << "aiLead=1\naiBudget=180\n";
    Settings read;
    loadSettings(file, read);
    check(read.aiLead == 1, "aiLead 1 was not read from the settings file");
    check(budgetOf(read, true) > 0, "aiLead 1 read from the settings file leaves the choice without a clock");
}

static void testSettingsFile() {
    Settings settings;
    SettingsTable table(settings);
    settings.volume = 50;
    for (const char* bad : {"volume=", "volume=x", "volume=5x", "volume=+-1", "volume=2147483648"})
        check(!setByText(table, settings, bad) && settings.volume == 50, "invalid setting changed volume");
    check(setByText(table, settings, "volume= +0 ") && settings.volume == 0, "valid zero was rejected");

    const std::filesystem::path folder = "session-tests";
    std::filesystem::create_directories(folder);
    const auto file = folder / "complete.settings";
    check(saveSettings(file, settings, [](std::ostream& out) { out << "device1=2\ndevice2=1\n"; }),
          "could not save the full configuration");
    settings.volume = 75;
    check(!saveSettings(file, settings, [](std::ostream& out) {
        out << "device1=7\n";
        out.setstate(std::ios::badbit);
    }), "a partial write was reported as saved");
    Settings read;
    int devices[2] = {-1, -1};
    loadSettings(file, read, [&](const std::string& line, size_t eq) {
        if (line.substr(0, eq) == "device1") settingInteger(std::string_view(line).substr(eq + 1), devices[0]);
        if (line.substr(0, eq) == "device2") settingInteger(std::string_view(line).substr(eq + 1), devices[1]);
    });
    check(read.volume == 0 && devices[0] == 2 && devices[1] == 1, "failed save changed previous settings");
    check(saveSettings(file, settings), "could not replace an existing configuration");
    loadSettings(file, read);
    check(read.volume == 75, "successful replacement lost the new settings");
    std::ofstream(file, std::ios::binary) << "volume=2x\naiLead=1\n";
    read = Settings{};
    loadSettings(file, read);
    check(read.volume == Settings{}.volume && read.aiLead == 1, "malformed number in old INI");
}

// A replay played without a window (checkReplay, --check-replay-expected): a <file>.result that cannot be written is
// not taken for a replay as expected.
static void testResultWritten() {
#ifdef JURAIC_REPLAY_FIXTURES
    const std::filesystem::path folder = "session-tests/replay", from = std::filesystem::path(JURAIC_REPLAY_FIXTURES) / "selftest-3003.jfreplay";
    std::filesystem::remove_all(folder);
    std::filesystem::create_directories(folder);
    const std::filesystem::path file = folder / "selftest-3003.jfreplay";
    std::filesystem::copy_file(from, file);
    std::filesystem::copy_file(std::filesystem::path(from) += ".expected", std::filesystem::path(file) += ".expected");
    check(checkReplay(file, true) == 0, "a replay that ends as expected is not taken as such");
    // (a folder where the result is to be written)
    std::filesystem::path result = file;
    result += ".result";
    std::filesystem::remove(result);
    std::filesystem::create_directory(result);
    const int code = checkReplay(file, true);
    check(code == 10, "a result that could not be written gives " + std::to_string(code));
#endif
}

int main() {
    try {
        testSeatBudget();
        testSettingsFile();
        testResultWritten();
        std::cout << "session tests passed\n";
        return 0;
    } catch (const std::exception& e) {
        std::cerr << "FAILED: " << e.what() << '\n';
        return 1;
    }
}
