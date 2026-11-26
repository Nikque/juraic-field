#pragma once
#include "session.h"
#include <fstream>

namespace play {

// Recompute both seats through the game's level menu, then compare their
// buttons with a recorded arena round. The case supplies clocks and seeds.
inline int checkAiRound(const std::filesystem::path& directory, const std::filesystem::path& report) {
    std::ofstream result(report);
    Replay replay;
    if (!result || !loadReplay(directory / "inputs.jfreplay", replay) ||
        replay.rounds.size() != 1 || replay.players != 2) return 2;
    const auto& log = replay.rounds.front();
    int split, width, depth, queues, fixed, ahead;
    uint64_t secondSeed, agentSeed[2];
    int level[2], ex[2];
    std::ifstream draws(directory / "tsumo.txt"), settings(directory / "ai-case.txt");
    if (!(draws >> split >> secondSeed) || !(settings >> width >> depth >> queues >> fixed >> ahead) ||
        !(settings >> level[0] >> ex[0] >> agentSeed[0] >> level[1] >> ex[1] >> agentSeed[1]) ||
        log.frames.empty() || (split != 0 && split != 1) || width < 1 || depth < 1 || queues < 1 || fixed < 1) return 2;
    auto s = replay.settings;
    s.aiLevel1 = level[0], s.aiLevel = level[1];
    s.aiEx1 = ex[0] != 0, s.aiEx = ex[1] != 0;
    auto session = std::make_unique<Versus>();
    session->adjust = [&](int, juraic::ai::Params& params) {
        params.beamWidth = width, params.beamDepth = depth, params.beamQueues = queues;
        params.beamFixedQueues = fixed, params.thinkAhead = ahead;
        params.thinkBudget = 0, params.searchThreads = 1;
    };
    session->game.startReplay(replay, log);
    if (split) session->game.round.splitTsumo(secondSeed);
    for (int i = 0; i < 2; ++i) {
        session->seat(i, aiOfLevel(i ? s.aiLevel : s.aiLevel1, 0), s, false);
        if (!session->agent[i]) return 2;
        session->agent[i]->startRound(i, agentSeed[i]);
        result << "seat " << i << " agent " << session->seatLog[i].name << " ex " << ex[i] << '\n';
    }
    for (size_t frame = 0; frame < log.frames.size(); ++frame) {
        uint8_t buttons[2]{session->agentButtons(0),session->agentButtons(1)};
        for (int seat = 0; seat < 2; ++seat) {
            const auto expected = uint8_t(log.frames[frame] >> (8 * seat));
            if (buttons[seat] != expected) {
                result << "mismatch at frame " << frame+1 << " seat " << seat
                       << " expected " << int(expected) << " actual " << int(buttons[seat]) << '\n';
                return 1;
            }
        }
        juraic::PlayerEvents events[2];
        session->game.step(buttons, events);
    }
    result << "matched every button of both seats in " << log.frames.size() << " frames\n";
    return result.good() ? 0 : 2;
}
} // namespace play
