#pragma once
#include "gui_editor.h"

namespace juraic_gui {
enum class UrlFormat { Ishikawa, Park, Mattulwan };
inline constexpr std::array<const char*, 3> urlFormatNames{
    "石川", "対戦パーク", "mattulwan系（pndsngミラー）"};
inline constexpr std::array<const char*, 3> urlFormatIds{"ishikawa", "park", "mattulwan"};

// Export only a 6 x 13 board. Engine identity and CLI history keep using
// fieldToUrl; changing a destination cannot change the search or dedupe.
inline std::string urlUnavailableReason(const Board& board, UrlFormat format) {
    if (board.wide()) return "10列×18段の盤面を表せるサイトがないため、URLは出力できません。";
    for (const auto& col : board.cells)
        for (Cell v : col) {
            if (v > Wall) return "未対応の種別です。";
            if (v == PointPiece && format != UrlFormat::Mattulwan)
                return "「得点」に対応していないため、このサイトへは出力できません。";
        }
    return {};
}
inline std::string simulatorUrl(const Board& board, UrlFormat format) {
    const auto reason = urlUnavailableReason(board, format);
    if (!reason.empty()) throw std::runtime_error(reason);
    bool special = false;
    for (const auto& col : board.cells)
        for (Cell v : col) special |= v >= PointPiece;
    const auto longCode = [&](const std::array<int, 11>& mapping) {
        std::string code;
        for (int y = 12; y >= 0; --y)
            for (int x = 0; x < 6; ++x) code += char('0' + mapping[board.cells[x][y]]);
        const auto first = code.find_first_not_of('0');
        return first == std::string::npos ? std::string{} : code.substr(first);
    };
    // Colours and garbage in the short code shared by two of the sites.
    const auto shortCode = [&] {
        return fieldToUrl(bitsFromBoard(board)).substr(std::string("https://ishikawapuyo.net/simu/pe.html?").size());
    };
    switch (format) {
    case UrlFormat::Ishikawa:
        if (special) return "https://ishikawapuyo.net/simu/pe.html?~" + longCode({0, 1, 2, 3, 4, 5, 6, -1, 9, 8, 7});
        return "https://ishikawapuyo.net/simu/pe.html?" + shortCode();
    case UrlFormat::Park:
        // Both formats pack two cells into six bits, top row first.
        if (special) return "https://www.puyop.com/s/=" + longCode({0, 1, 2, 3, 4, 5, 6, -1, 7, 8, 9});
        return "https://www.puyop.com/s/" + shortCode();
    case UrlFormat::Mattulwan: {
        constexpr char symbols[] = "abecdfgjkhi";
        std::string code;
        char previous = 0;
        int count = 0;
        const auto flush = [&] {
            if (count) {
                code += previous;
                if (count > 1) code += std::to_string(count);
            }
        };
        for (int y = 12; y >= 0; --y)
            for (int x = 0; x < 6; ++x) {
                const char symbol = symbols[board.cells[x][y]];
                if (symbol != previous) {
                    flush();
                    previous = symbol;
                    count = 0;
                }
                ++count;
            }
        flush();
        return "https://www.pndsng.com/puyo/index.html?" + code;
    }
    }
    throw std::runtime_error("不明なURL形式です。");
}
inline std::string simulatorUrl(const BitField& field, UrlFormat format) {
    return simulatorUrl(boardFromBits(field), format);
}
// The URLs of the 6 x 13 results; 10 x 18 results have none and are left out. Returns how many were written.
inline size_t writeSimulatorUrls(const std::filesystem::path& path, const std::vector<Record>& records, UrlFormat format) {
    std::ofstream out(path, std::ios::binary);
    if (!out) throw std::runtime_error("URL一覧を保存できません。");
    size_t written = 0;
    for (const auto& record : records)
        if (!record.wide) {
            out << simulatorUrl(record.solution.field, format) << '\n';
            ++written;
        }
    out.flush();
    if (!out) throw std::runtime_error("URL一覧の書き込みに失敗しました。");
    return written;
}
} // namespace juraic_gui
