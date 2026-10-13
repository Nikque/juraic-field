#pragma once
// What the settings screen changes, and the file it is kept in
// (JuraicField.ini next to the executable; plain "name=value" lines).
#include "../ai/level.h"
#include "../game/rules.h"
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <functional>
#include <string>
#include <vector>

namespace play {

// The scenes that have music of their own (Settings::bgm): a match and the replay of one, toko and the
// replay of one, everything else (the menu, the settings). With the names of their settings, and the
// folders below "bgm" that hold their files: a scene is offered the files of its own folder only (so
// that the music of one scene cannot be set for another by mistake).
enum MusicScene { MUSIC_VERSUS, MUSIC_TOKO, MUSIC_OTHER, MUSIC_SCENES };
inline const char* const MUSIC_KEYS[MUSIC_SCENES] = {"bgmVersus", "bgmToko", "bgmOther"};
inline const wchar_t* const MUSIC_FOLDERS[MUSIC_SCENES] = {L"versus", L"toko", L"menu"};

// The places that have a picture of their own (Settings::background): the field of the first player (and
// the one field of toko), the field of the second player, everything else (the whole screen behind them).
// With the names of their settings, and the folders below "background" that hold their files: a place is
// offered the files of its own folder only, as a scene its music.
enum PicturePlace { PICTURE_FIELD1, PICTURE_FIELD2, PICTURE_OTHER, PICTURE_PLACES };
inline const char* const PICTURE_KEYS[PICTURE_PLACES] = {"background1P", "background2P", "backgroundOther"};
inline const wchar_t* const PICTURE_FOLDERS[PICTURE_PLACES] = {L"1p", L"2p", L"other"};

struct Settings {
    juraic::Rules rules;
    int volume = 60;       // percent
    bool prediction = true; // show what the current placement and the best placement would do
    bool ghost = true;      // show where the pair will land
    // The search of the AI seats. Automatic: 40 x 10 with three queues, made wider when the machine
    // has time to spare and narrower when it has not. By hand: the values below, as they are.
    int aiAuto = 1;
    int aiWidth = 40, aiDepth = 10, aiQueues = 3;
    int aiBudget = 180; // milliseconds a choice may take (the AI begins 200 ms before its pair appears)
    int aiThreads = 0;  // threads of a search; 0 = as many as queues, one less than the machine has at most
    // Frames before its pair appears at which the AI begins a choice (12 = as soon as the pair before
    // has come to rest); 0 = the AI sets it for every round, as late as this machine's searches allow.
    int aiLead = 0;
    // 1: the AI does not fire its main chain with a placement that only builds while another placement is left
    // (ai/params.h x3BuildHold); 0: as it always played.
    int aiBuildHold = 0;
    // The AI of a seat: 0 = Juraic as it is (it pokes when its field fills up: the agent x3; on the screen "Juraic 催促（小）"),
    // 1 = the AI that hardly pokes (the agent x3q, ai/agents.h quietPoke; "Juraic 催促（極小）").
    // aiStyle: the 2P side - the
    // opponent of a person, and one of two AIs that are watched; aiStyle1: the 1P side when two AIs are watched.
    int aiStyle = 0, aiStyle1 = 0;
    // The level of the AI of a seat, 1 to 36 (ai/level.h; 36 = the strongest, the AI as it is, and the level a new
    // settings file has). Either of the two AIs above plays at every level (the agents lv1 to lv36 and lv1q to lv36q).
    // aiLevel: the 2P side, aiLevel1: the 1P side when two AIs are watched. On the screen the levels come in six
    // groups, each with a name and a colour (LEVEL_GROUP_NAMES, LEVEL_GROUP_COLORS below).
    int aiLevel = juraic::ai::LEVELS, aiLevel1 = juraic::ai::LEVELS;
    // How the buttons of a person reach the game (play/input.h). Not rules: a replay holds the
    // buttons as these have made them.
    // A match over a network, the host's side: the frames by which garbage is late (game/link.h); 0 = set by the
    // time the line takes when the guest calls (play/net.h delayFor).
    int netDelay = 0;
    int diagonal = 1;  // 0: a lever of four ways, of down and a direction held together only the one pushed last
    int quickDrop = 0; // 1: up drops the pair at once (BTN_DROP)
    // The picture behind the game and the music (play/media.h): files of the folders "background"
    // and "bgm" next to the executable (a picture in the folder of its place below "background",
    // PICTURE_FOLDERS; the music in the folder of its scene below "bgm", MUSIC_FOLDERS), chosen by
    // their place among the names found there when
    // the game starts (0 = none). The settings file keeps the names, so that a file added or
    // taken away does not change the choice. There is a picture for every place (PicturePlace) and
    // music for every scene (MusicScene).
    int background[PICTURE_PLACES]{}, backgroundDim = 40; // ... and how much the pictures are darkened, percent
    int bgm[MUSIC_SCENES]{}, bgmVolume = 40;              // ... and the volume of the music, percent
    std::vector<std::wstring> backgroundFiles[PICTURE_PLACES], bgmFiles[MUSIC_SCENES];
    // (the names until a settings file says otherwise: the pictures and the tunes that come with the package, media/)
    std::wstring backgroundName[PICTURE_PLACES] = {L"field-1p-silhouette.png", L"field-2p-juraic.png", L"stage-grid.png"};
    std::wstring bgmName[MUSIC_SCENES] = {L"synth-versus.flac", L"synth-solo.flac", L"synth-menu.flac"};
    // The file of a scene's music below the folder "bgm" ("versus/name"), nothing when the scene has none.
    std::wstring musicFile(int scene) const { return bgm[scene] && !bgmName[scene].empty() ? std::wstring(MUSIC_FOLDERS[scene]) + L"/" + bgmName[scene] : std::wstring(); }
};

// The six groups of the AI's levels as the screen names and colours them (ai/level.h levelGroup): levels 1 to 9,
// 10 to 20, 21 to 25, 26 to 30, 31 to 35, and 36. The colours are 0xRRGGBB, chosen to be told apart on the dark screen.
inline const wchar_t* const LEVEL_GROUP_NAMES[juraic::ai::LEVEL_GROUPS] = {L"超初心者用", L"初心者用", L"初〜中級者用", L"中〜上級者用", L"上級〜超級者用", L"超級者用"};
inline constexpr unsigned LEVEL_GROUP_COLORS[juraic::ai::LEVEL_GROUPS] = {0x8FD694, 0x6EC6FF, 0xFFD966, 0xFFA24D, 0xFF6B6B, 0xD18CFF};
inline std::wstring levelText(int level) { return L"Lv " + std::to_wstring(level) + L"　" + LEVEL_GROUP_NAMES[juraic::ai::levelGroup(level)]; }
inline unsigned levelColor(int level) { return LEVEL_GROUP_COLORS[juraic::ai::levelGroup(level)]; }

// The parts of the settings screen.
enum SettingSection { SEC_RULES, SEC_CONTROLS, SEC_AI, SEC_VIEW, SETTING_SECTIONS };
inline const wchar_t* const SETTING_SECTION_NAMES[SETTING_SECTIONS] = {L"ルール", L"操作", L"AI", L"表示と音"};

struct SettingItem {
    const wchar_t* name;
    std::string key;
    int low, high, step;
    std::function<int&(Settings&)> value;
    std::function<std::wstring(int)> show;
    int section = SEC_RULES;
    // False: the setting has no effect with the other settings as they are; the screen shows it
    // grey and passes over it. (Empty = it always has.)
    std::function<bool(const Settings&)> enabled;
    // The colour its value is written in, 0xRRGGBB (empty = white, as every value but the AI's levels).
    std::function<unsigned(int)> color;
};

inline std::wstring number(int v) { return std::to_wstring(v); }
inline std::wstring onOff(int v) { return v ? L"あり" : L"なし"; }

// bool members are edited through an int; the screen writes them back.
struct SettingsTable {
    int softDrop = 1, prediction = 1, ghost = 1, correction = 1, preInput = 1;
    std::vector<SettingItem> items;

    explicit SettingsTable(Settings& s) {
        softDrop = s.rules.softDropBonus, prediction = s.prediction, ghost = s.ghost, correction = int(s.rules.tsumoCorrection);
        preInput = s.rules.preInput;
        const auto member = [](int juraic::Rules::*m) { return [m](Settings& x) -> int& { return x.rules.*m; }; };
        const auto picture = [&s](int place) {
            return std::function<std::wstring(int)>([&s, place](int v) {
                const std::vector<std::wstring>& files = s.backgroundFiles[place];
                if (v >= 1 && v <= int(files.size())) return files[size_t(v - 1)];
                return files.empty() ? std::wstring(L"なし（background の中の ") + PICTURE_FOLDERS[place] + L" フォルダーに png・jpg・bmp を置くと選べます）" : std::wstring(L"なし");
            });
        };
        const auto music = [&s](int scene) {
            return std::function<std::wstring(int)>([&s, scene](int v) {
                const std::vector<std::wstring>& files = s.bgmFiles[scene];
                if (v >= 1 && v <= int(files.size())) return files[size_t(v - 1)];
                return files.empty() ? std::wstring(L"なし（bgm の中の ") + MUSIC_FOLDERS[scene] + L" フォルダーに mp3・wav・m4a・flac を置くと選べます）" : std::wstring(L"なし");
            });
        };
        items = {
            {L"効果音の音量", "volume", 0, 100, 5, [](Settings& x) -> int& { return x.volume; }, [](int v) { return number(v) + L" %"; }},
            {L"連鎖の予測表示", "prediction", 0, 1, 1, [this](Settings&) -> int& { return prediction; }, onOff},
            {L"着地位置の表示", "ghost", 0, 1, 1, [this](Settings&) -> int& { return ghost; }, onOff},
            {L"色数", "colors", 3, 5, 1, member(&juraic::Rules::colors), [](int v) { return number(v) + L" 色"; }},
            {L"消せる個数", "clearCount", 2, 8, 1, member(&juraic::Rules::clearCount), [](int v) { return number(v) + L" 個"; }},
            {L"ツモ補正", "tsumoCorrection", 0, 2, 1, [this](Settings&) -> int& { return correction; },
             [](int v) { return std::wstring(v == 0 ? L"なし" : v == 1 ? L"初手 2 手が 3 色以内" : L"初手 3 手が 3 色以内"); }},
            {L"ゲームスピード", "speedPercent", 50, 300, 25, member(&juraic::Rules::speedPercent), [](int v) { return L"×" + number(v / 100) + L"." + (v % 100 < 10 ? L"0" : L"") + number(v % 100); }},
            {L"落下ボーナス", "softDropBonus", 0, 1, 1, [this](Settings&) -> int& { return softDrop; }, onOff},
            {L"先行入力", "preInput", 0, 1, 1, [this](Settings&) -> int& { return preInput; },
             [](int v) { return std::wstring(v ? L"あり（出る直前の回転・横移動が効く）" : L"なし"); }},
            {L"斜め入力", "diagonal", 0, 1, 1, [](Settings& x) -> int& { return x.diagonal; },
             [](int v) { return std::wstring(v ? L"あり（下と横を同時に入れられる）" : L"なし（4 方向レバー: 後から入れた方向だけ）"); }},
            {L"クイックドロップ", "quickDrop", 0, 1, 1, [](Settings& x) -> int& { return x.quickDrop; },
             [](int v) { return std::wstring(v ? L"あり（上で一気に落とす）" : L"なし"); }},
            {L"おじゃまレート", "targetPoints", 10, 990, 10, member(&juraic::Rules::targetPoints), number},
            {L"マージンタイム", "marginSeconds", 0, 992, 16, member(&juraic::Rules::marginSeconds), [](int v) { return number(v) + L" 秒"; }},
            {L"連鎖しばり", "chainRequirement", 1, 20, 1, member(&juraic::Rules::chainRequirement), [](int v) { return v <= 1 ? std::wstring(L"なし") : number(v) + L" 連鎖"; }},
            {L"全消しボーナス", "allClearBonus", 0, 9900, 100, member(&juraic::Rules::allClearBonus), [](int v) { return number(v) + L" 点"; }},
            {L"1 回に降るおじゃまの上限", "maxGarbagePerDrop", 6, 72, 6, member(&juraic::Rules::maxGarbagePerDrop), [](int v) { return number(v) + L" 個"; }},
            {L"先取本数", "winsNeeded", 1, 99, 1, member(&juraic::Rules::winsNeeded), [](int v) { return number(v) + L" 本"; }},
            {L"先取セット数", "setsNeeded", 1, 99, 1, member(&juraic::Rules::setsNeeded), [](int v) { return number(v) + L" セット"; }},
            {L"ネット対戦の遅延（部屋を立てる側）", "netDelay", 0, 30, 1, [](Settings& x) -> int& { return x.netDelay; },
             [](int v) { return v ? number(v) + L" フレーム" : std::wstring(L"自動（回線に合わせる）"); }},
            // (the AI: the three values of the search stand right below their switch, grey while it is automatic)
            {L"AI の探索", "aiAuto", 0, 1, 1, [](Settings& x) -> int& { return x.aiAuto; },
             [](int v) { return std::wstring(v ? L"自動（PC の速さに合わせる）" : L"手動（下の値のまま）"); }},
            {L"AI 探索の幅", "aiWidth", 10, 400, 10, [](Settings& x) -> int& { return x.aiWidth; }, number},
            {L"AI 探索の深さ", "aiDepth", 4, 20, 1, [](Settings& x) -> int& { return x.aiDepth; }, [](int v) { return number(v) + L" 手"; }},
            {L"AI 仮のツモ列の数", "aiQueues", 1, 8, 1, [](Settings& x) -> int& { return x.aiQueues; }, [](int v) { return number(v) + L" 本"; }}, // (up to the eight of the automatic search, ai/params.h)
            {L"AI の探索スレッド", "aiThreads", 0, 16, 1, [](Settings& x) -> int& { return x.aiThreads; },
             [](int v) { return v ? number(v) + L" 本" : std::wstring(L"自動"); }},
            // (automatic: the AI sets the frames for every round only with the automatic search; with a
            //  search by hand it begins twelve frames before its pair, play/main.cpp, seat())
            {L"AI が判断を始める時点", "aiLead", 0, 12, 2, [](Settings& x) -> int& { return x.aiLead; },
             [&s](int v) { return v ? L"組が出る " + number(v) + L" フレーム前" : std::wstring(s.aiAuto ? L"自動（PC の速さで決める）" : L"自動（組が出る 12 フレーム前）"); }},
            {L"AI の持ち時間", "aiBudget", 50, 1000, 10, [](Settings& x) -> int& { return x.aiBudget; }, [](int v) { return number(v) + L" ms"; }},
            {L"AI が積みの手で本線を撃つ", "aiBuildHold", 0, 1, 1, [](Settings& x) -> int& { return x.aiBuildHold; },
             [](int v) { return std::wstring(v ? L"ほかに置ける間は撃たない" : L"撃つ（これまでどおり）"); }},
            // (each AI seat: which of the two AIs it is, and right below it its level - either AI plays at every level)
            {L"AI（2P 側）", "aiStyle", 0, 1, 1, [](Settings& x) -> int& { return x.aiStyle; },
             [](int v) { return std::wstring(v ? L"Juraic 催促（極小）" : L"Juraic 催促（小）"); }},
            {L"AI のレベル（2P 側）", "aiLevel", 1, juraic::ai::LEVELS, 1, [](Settings& x) -> int& { return x.aiLevel; }, levelText},
            {L"AI（観戦の 1P 側）", "aiStyle1", 0, 1, 1, [](Settings& x) -> int& { return x.aiStyle1; },
             [](int v) { return std::wstring(v ? L"Juraic 催促（極小）" : L"Juraic 催促（小）"); }},
            {L"AI のレベル（観戦の 1P 側）", "aiLevel1", 1, juraic::ai::LEVELS, 1, [](Settings& x) -> int& { return x.aiLevel1; }, levelText},
            // (the pictures: one for each field and one for the rest of the screen; toko's field is the first player's)
            {L"背景（1P 側のフィールド）", PICTURE_KEYS[PICTURE_FIELD1], 0, int(s.backgroundFiles[PICTURE_FIELD1].size()), 1, [](Settings& x) -> int& { return x.background[PICTURE_FIELD1]; }, picture(PICTURE_FIELD1)},
            {L"背景（2P 側のフィールド）", PICTURE_KEYS[PICTURE_FIELD2], 0, int(s.backgroundFiles[PICTURE_FIELD2].size()), 1, [](Settings& x) -> int& { return x.background[PICTURE_FIELD2]; }, picture(PICTURE_FIELD2)},
            {L"背景（それ以外）", PICTURE_KEYS[PICTURE_OTHER], 0, int(s.backgroundFiles[PICTURE_OTHER].size()), 1, [](Settings& x) -> int& { return x.background[PICTURE_OTHER]; }, picture(PICTURE_OTHER)},
            {L"背景の暗さ", "backgroundDim", 0, 90, 10, [](Settings& x) -> int& { return x.backgroundDim; }, [](int v) { return number(v) + L" %"; }},
            // (the music: one choice for every scene; on the settings screen the one under the cursor is heard)
            {L"BGM（対戦・対戦のリプレイ）", MUSIC_KEYS[MUSIC_VERSUS], 0, int(s.bgmFiles[MUSIC_VERSUS].size()), 1, [](Settings& x) -> int& { return x.bgm[MUSIC_VERSUS]; }, music(MUSIC_VERSUS)},
            {L"BGM（とこ・とこのリプレイ）", MUSIC_KEYS[MUSIC_TOKO], 0, int(s.bgmFiles[MUSIC_TOKO].size()), 1, [](Settings& x) -> int& { return x.bgm[MUSIC_TOKO]; }, music(MUSIC_TOKO)},
            {L"BGM（メニューなど）", MUSIC_KEYS[MUSIC_OTHER], 0, int(s.bgmFiles[MUSIC_OTHER].size()), 1, [](Settings& x) -> int& { return x.bgm[MUSIC_OTHER]; }, music(MUSIC_OTHER)},
            {L"BGM の音量", "bgmVolume", 0, 100, 5, [](Settings& x) -> int& { return x.bgmVolume; }, [](int v) { return number(v) + L" %"; }},
        };
        // Where each setting stands on the screen, and when it has no effect.
        const auto in = [this](int section, std::initializer_list<const char*> keys) {
            for (SettingItem& item : items)
                for (const char* key : keys)
                    if (item.key == key) item.section = section;
        };
        in(SEC_CONTROLS, {"preInput", "diagonal", "quickDrop"});
        in(SEC_AI, {"aiAuto", "aiLead", "aiBudget", "aiThreads", "aiWidth", "aiDepth", "aiQueues", "aiBuildHold", "aiStyle", "aiStyle1", "aiLevel", "aiLevel1"});
        for (SettingItem& item : items)
            if (item.key == "aiLevel" || item.key == "aiLevel1") item.color = levelColor;
        in(SEC_VIEW, {"volume", "prediction", "ghost", "background1P", "background2P", "backgroundOther", "backgroundDim", "bgmVersus", "bgmToko", "bgmOther", "bgmVolume"});
        const auto when = [this](const char* key, std::function<bool(const Settings&)> enabled) {
            for (SettingItem& item : items)
                if (item.key == key) item.enabled = enabled;
        };
        for (const char* key : {"aiWidth", "aiDepth", "aiQueues"}) when(key, [](const Settings& x) { return x.aiAuto == 0; }); // (the automatic search sets them itself)
        when("tsumoCorrection", [](const Settings& x) { return x.rules.colors > 3; }); // (with three colours the first pairs hold no more anyway)
        when("backgroundDim", [](const Settings& x) { return x.background[PICTURE_FIELD1] || x.background[PICTURE_FIELD2] || x.background[PICTURE_OTHER]; });
        when("bgmVolume", [](const Settings& x) { return x.bgm[MUSIC_VERSUS] || x.bgm[MUSIC_TOKO] || x.bgm[MUSIC_OTHER]; });
    }
    bool enabled(const SettingItem& item, const Settings& s) const { return !item.enabled || item.enabled(s); }
    // Copies the edited ints back and rescales what depends on them.
    void apply(Settings& s) const {
        s.rules.softDropBonus = softDrop != 0;
        s.rules.preInput = preInput != 0;
        s.prediction = prediction != 0;
        s.ghost = ghost != 0;
        s.rules.tsumoCorrection = juraic::TsumoCorrection(correction);
        // The margin table belongs to 70 points; keep its proportions for another rate.
        const juraic::Rules standard;
        for (int i = 0; i < standard.marginSteps; ++i) {
            const int rate = standard.marginRates[size_t(i)] * s.rules.targetPoints / standard.targetPoints;
            s.rules.marginRates[size_t(i)] = rate < 1 ? 1 : rate;
        }
        // (the names the settings file keeps follow the choice)
        for (int k = 0; k < PICTURE_PLACES; ++k)
            s.backgroundName[k] = s.background[k] >= 1 && s.background[k] <= int(s.backgroundFiles[k].size()) ? s.backgroundFiles[k][size_t(s.background[k] - 1)] : std::wstring();
        for (int k = 0; k < MUSIC_SCENES; ++k)
            s.bgmName[k] = s.bgm[k] >= 1 && s.bgm[k] <= int(s.bgmFiles[k].size()) ? s.bgmFiles[k][size_t(s.bgm[k] - 1)] : std::wstring();
    }
};

// "key=value" as a tool takes it on its command line (--set): the value is kept within the range of the
// setting. False for a key that is not known.
inline bool setByText(SettingsTable& table, Settings& s, const std::string& pair) {
    const size_t eq = pair.find('=');
    bool known = false;
    for (auto& item : table.items)
        if (eq != std::string::npos && item.key == pair.substr(0, eq)) {
            const int v = std::atoi(pair.c_str() + eq + 1);
            item.value(s) = v < item.low ? item.low : v > item.high ? item.high : v;
            known = true;
        }
    return known;
}

// The place of `name` among `files`, counted from 1; 0 = it is not there. A file of the same name with another
// ending is the same choice: a settings file that names a tune as .wav finds the .flac of the package.
inline int placeOf(const std::vector<std::wstring>& files, const std::wstring& name) {
    for (size_t i = 0; i < files.size(); ++i)
        if (files[i] == name) return int(i) + 1;
    const std::wstring stem = name.substr(0, name.rfind(L'.'));
    for (size_t i = 0; !stem.empty() && i < files.size(); ++i)
        if (files[i].substr(0, files[i].rfind(L'.')) == stem) return int(i) + 1;
    return 0;
}

// File names in the settings file are UTF-8.
inline std::string toUtf8(const std::wstring& w) {
    std::string out;
    for (size_t i = 0; i < w.size(); ++i) {
        uint32_t c = uint32_t(w[i]);
        if (sizeof(wchar_t) == 2 && c >= 0xD800 && c < 0xDC00 && i + 1 < w.size()) c = 0x10000 + ((c - 0xD800) << 10) + (uint32_t(w[++i]) - 0xDC00);
        if (c < 0x80) out += char(c);
        else if (c < 0x800) out += char(0xC0 | c >> 6), out += char(0x80 | (c & 63));
        else if (c < 0x10000) out += char(0xE0 | c >> 12), out += char(0x80 | (c >> 6 & 63)), out += char(0x80 | (c & 63));
        else out += char(0xF0 | c >> 18), out += char(0x80 | (c >> 12 & 63)), out += char(0x80 | (c >> 6 & 63)), out += char(0x80 | (c & 63));
    }
    return out;
}
inline std::wstring fromUtf8(const std::string& u) {
    std::wstring out;
    for (size_t i = 0; i < u.size();) {
        const unsigned char b = static_cast<unsigned char>(u[i]);
        const int more = b < 0x80 ? 0 : b < 0xE0 ? 1 : b < 0xF0 ? 2 : 3;
        uint32_t c = more == 0 ? b : more == 1 ? b & 31u : more == 2 ? b & 15u : b & 7u;
        for (int k = 1; k <= more && i + size_t(k) < u.size(); ++k) c = c << 6 | (static_cast<unsigned char>(u[i + size_t(k)]) & 63u);
        i += size_t(more) + 1;
        if (sizeof(wchar_t) == 2 && c >= 0x10000) out += wchar_t(0xD800 + ((c - 0x10000) >> 10)), out += wchar_t(0xDC00 + ((c - 0x10000) & 0x3FF));
        else out += wchar_t(c);
    }
    return out;
}

inline void loadSettings(const std::filesystem::path& path, Settings& s) {
    SettingsTable table(s);
    std::ifstream in(path);
    std::string line;
    bool pictureNamed[PICTURE_PLACES]{}, musicNamed[MUSIC_SCENES]{};
    std::wstring picture[PICTURE_PLACES], music[MUSIC_SCENES];
    while (std::getline(in, line)) {
        if (!line.empty() && line.back() == '\r') line.pop_back();
        const size_t eq = line.find('=');
        if (eq == std::string::npos) continue;
        for (int k = 0; k < PICTURE_PLACES; ++k)
            if (line.substr(0, eq) == std::string(PICTURE_KEYS[k]) + "File") pictureNamed[k] = true, picture[k] = fromUtf8(line.substr(eq + 1));
        for (int k = 0; k < MUSIC_SCENES; ++k)
            if (line.substr(0, eq) == std::string(MUSIC_KEYS[k]) + "File") musicNamed[k] = true, music[k] = fromUtf8(line.substr(eq + 1));
        for (auto& item : table.items)
            if (item.key == line.substr(0, eq)) {
                try {
                    const int v = std::stoi(line.substr(eq + 1));
                    item.value(s) = v < item.low ? item.low : v > item.high ? item.high : v;
                } catch (...) {
                }
            }
    }
    // (the picture and the music by their names: the files of the folders may have changed)
    for (int k = 0; k < PICTURE_PLACES; ++k)
        if (pictureNamed[k]) s.background[k] = placeOf(s.backgroundFiles[k], picture[k]);
    for (int k = 0; k < MUSIC_SCENES; ++k)
        if (musicNamed[k]) s.bgm[k] = placeOf(s.bgmFiles[k], music[k]);
    table.apply(s);
}

inline void saveSettings(const std::filesystem::path& path, Settings& s) {
    SettingsTable table(s);
    std::ofstream out(path);
    for (auto& item : table.items) out << item.key << '=' << item.value(s) << '\n';
    for (int k = 0; k < PICTURE_PLACES; ++k) out << PICTURE_KEYS[k] << "File=" << toUtf8(s.backgroundName[k]) << '\n';
    for (int k = 0; k < MUSIC_SCENES; ++k) out << MUSIC_KEYS[k] << "File=" << toUtf8(s.bgmName[k]) << '\n';
}

} // namespace play
