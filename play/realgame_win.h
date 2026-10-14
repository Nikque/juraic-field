#pragma once
// The mode in which Juraic plays the second player of the real game running on the same PC (the Windows build
// only): the real game's offline versus of two players, a person on 1P. Never for a match over a network.
// This part holds what the screen of the mode sets: which of the two AIs plays and at which level. Each time the
// mode is opened both start from the AI of the 2P side in the settings (Settings::aiStyle, aiLevel); what is
// changed here is not written back to the settings.
#include "../ai/level.h"

namespace play {

struct RealGameMode {
    enum Row { ROW_STYLE, ROW_LEVEL, ROW_BACK, ROWS };
    int row = ROW_STYLE;
    int style = 0;                 // 0: Juraic 催促（小）, 1: Juraic 催促（極小） (as Settings::aiStyle)
    int level = juraic::ai::LEVELS; // 1 .. LEVELS (as Settings::aiLevel)

    void open(int settingsStyle, int settingsLevel) {
        row = ROW_STYLE;
        style = settingsStyle ? 1 : 0;
        level = settingsLevel < 1 ? 1 : settingsLevel > juraic::ai::LEVELS ? juraic::ai::LEVELS : settingsLevel;
    }
    // The value of the row under the cursor changed by `by` steps; past either end it comes in again at the other
    // (as on the settings screen: Lv 36 → Lv 1). False: the row has no value.
    bool change(int by) {
        if (row == ROW_STYLE) style = ((style + by) % 2 + 2) % 2;
        else if (row == ROW_LEVEL) level = ((level - 1 + by) % juraic::ai::LEVELS + juraic::ai::LEVELS) % juraic::ai::LEVELS + 1;
        else return false;
        return true;
    }
};

} // namespace play
