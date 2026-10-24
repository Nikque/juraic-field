#pragma once
// The beat before a round of a match: two low beats and a high one, GAP frames apart, the high one heard in the frame
// the first pair of the round appears in (the first frame a button moves it). A person presses with a beat that can be
// counted ahead; a single sound is answered some 140 to 180 ms (8 to 11 frames) after it is heard.

namespace play {

enum class Beat { None, Low, High };

class StartBeat {
public:
    static constexpr int GAP = 30; // frames from one beat to the next
    static constexpr int LEAD = 2; // frames a beat is played before it is to be heard (the way of the sound out)

    // The beat of a frame, `untilSpawn` frames before the one the first pair appears in (0: this frame). The same count
    // again (a frame of a match on a line that waits for the other side) gives none.
    Beat at(int untilSpawn) {
        if (untilSpawn == last) return Beat::None;
        last = untilSpawn;
        if (untilSpawn == LEAD) return Beat::High;
        return untilSpawn == LEAD + GAP || untilSpawn == LEAD + 2 * GAP ? Beat::Low : Beat::None;
    }

private:
    int last = -1;
};

} // namespace play
