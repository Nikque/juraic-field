#pragma once
// The one door every AI goes through, in the game and in the tools: before each
// frame it is shown what is on the screen and answers with the buttons of that
// frame. A round played by agents is a list of buttons like any other round,
// so it can be saved and watched as a replay.
#include "input.h"
#include "observe.h"
#include <chrono>
#include <cstdlib>
#include <future>
#include <memory>
#include <string>
#include <utility>
#include <vector>

namespace juraic::ai {

constexpr uint64_t AGENT_STREAM = 0x6167656E74; // random stream of an agent's own choices

struct Agent {
    virtual ~Agent() = default;
    // A round begins. `seed` is the agent's own: whatever it decides at random
    // comes from it, so the same seed gives the same round again.
    virtual void startRound(int seat, uint64_t seed) = 0;
    // Before every frame of the round. Returns the Button bits of that frame.
    virtual uint8_t frame(const Observation& o) = 0;
};

// How an agent that thinks in placements works out a choice.
//   AtOnce: inside frame(), which returns when the choice is made (the tools).
//   Thread: on another thread, while the frames before the pair appears are played. When the
//           choice is not made as the pair appears, that frame waits for it: the buttons are
//           those of AtOnce.
//   Free:   on another thread, and nothing waits for it: a pair whose choice is not made yet is
//           left to fall until it is, and moved from where it is then. A game that draws sixty
//           frames a second never stands still for the AI; on a machine too slow for the search
//           the buttons depend on how fast it is.
enum class Thinking { AtOnce, Thread, Free };

// An agent that thinks in placements: it chooses where a pair goes, and the
// buttons come from InputPlanner.
//
// The choice is made `ahead` frames before the pair appears: in the pause after
// the last pair has come to rest (Timing::nextFrames), in which the own field
// no longer changes and the pair is on the screen. 1 = in the frame in which
// the pair appears. A class that sets `ahead` above 1 calls finish() in its
// destructor (a thread works with the members of that class).
class PlacementAgent : public Agent {
public:
    int ahead = 1;
    Thinking thinking = Thinking::AtOnce;
    int64_t lateFrames = 0; // Free: frames in which a pair in the air had no placement yet (for tools)
    // Free: the pairs of this round that waited for their choice: the moves the player had made, then
    // the frames the pair waited, one pair after the other (the game writes them into its replay).
    std::vector<int> lateLog;
    // A round of the game played again by a tool (AtOnce): the lateLog of that round. The pairs that
    // waited there wait as long here, so that the buttons are those of the game. Null = no pair waits.
    const std::vector<int>* lateScript = nullptr;
    int64_t rerouted = 0;   // pairs whose chosen placement had no path from where they appeared, and that went to another (for tools)
    int64_t steered = 0;    // pairs sent to the nearest placement they could still reach, no way leading to the chosen one any more (steer, a late choice; for tools)
    int64_t steeredLow = 0; // ... of them, those a way through the 13th row still led to the chosen one (keepLow; for tools)
    int lastWanted = -1, lastTaken = -1; // ... the last of them
    // true: a placement is offered to choose() only when the pair has a path to it from where it
    // appears. (legalPlacements follows the rule of reach of core/reach.h; on a field with columns
    // twelve high it allows placements beyond them that InputPlanner finds no buttons for. Counted:
    // 14 of 4814 pairs of X3 against itself, all of them upright on the sixth column; the pair then
    // went to the placement with the next number, which nobody chose.)
    bool pathCheck = false;
    // The second choice. The last pair of the window is on the screen only four frames before a
    // pair appears (Timing::nextShown), or earlier when the opponent's next pair appears. A choice
    // begun before that is made once more when the screen shows a pair it did not know, if again()
    // says so. The first choice stands until the second has come; the pair then goes to the second
    // one if a path leads there from where it is, so that no pair waits for it.
    //   AtOnce: the second choice comes againLate frames after the pair has appeared (0 = before
    //           it appears; for measuring), or, with againScript, as in the round played again.
    //   Free:   when its thread has ended.
    int againLate = 0;
    // The second choices of this round: the moves the player had made, then the frames after the
    // pair appeared at which the choice came (0 = before it appeared, -1 = not while the pair was
    // in the air), one after the other (the game writes them into its replay).
    std::vector<int> againLog;
    const std::vector<int>* againScript = nullptr; // a tool: the againLog of the round that is played again
    // (for tools) times the screen showed more than a choice knew, second choices
    // begun, pairs that went to another placement by one, and second choices no path led to any more
    int64_t againAsked = 0, againBegun = 0, againSwitched = 0, againNoPath = 0;
    // While a second choice is on its way the pair goes straight down, in every frame in which that
    // costs the first choice no frame and takes no placement out of its reach: a second choice that
    // comes after the pair has appeared then finds every placement as near as it was. (Measured
    // without it, against the fixed Ama, 240 rounds: the second choice before the pair appears as good
    // as knowing three pairs from the start, 3 frames after it 174 wins for 189, 6 frames after 166;
    // no second choice 160.) false: the pair sets out for the first choice at once (the default:
    // Params::thinkHold 0).
    bool holdSecond = false;
    // The taps of the buttons it gives (InputPlanner::tapHold): pressed this many frames, let go as many before the
    // next. 1: a frame each (the fastest; the default).
    int tapHold = 1;
    // With tapHold above 1: the frames the turn and the move held in the last frame have been held so far, as told by
    // whoever sends the buttons (InputPlanner::heldFor; 0: as long as a tap is).
    int tapsHeldFor[2] = {0, 0};
    // The down button held only once the pair is turned and moved where it goes, and not while the choice is late
    // (InputPlanner::downWhenPlaced): for a pair whose field is not known for certain.
    bool downWhenPlaced = false;
    // The ways keep the pairs below the 13th row and turn none quickly (InputPlanner::keepLow), and with pathCheck only
    // placements reached so are chosen - when every one of those fills the 12th row of the third column, any reached.
    bool keepLow = false;
    // No turn is pressed while a quick turn's lock lasts (InputPlanner::waitQuickLock).
    bool waitQuickLock = false;
    // No way to the placement chosen is lost by a frame its sender is not called in, when one with the down button held
    // later is not (InputPlanner::spareFrame).
    bool spareFrame = false;
    // The cells of the 13th and 14th rows of the own field that may be the other way, and the frames after the pair is
    // shown elsewhere than its way has it by which a way from where it is then begins (InputPlanner::planSure: the ways
    // to the placement chosen).
    uint8_t unsure13 = 0, unsure14 = 0;
    int seenAfter = 0;
    const InputPlanner& ways() const { return planner; } // (for tools: InputPlanner::sureWays and the others)
    int64_t climbs = 0;    // pairs for which every placement reached below the 13th row filled it (for tools)
    std::vector<int> climbing; // ... and the pairs whose ways were let go up into the 13th row so, by their number (for tools)
    int64_t againHeld = 0; // frames in which a pair went straight down for that reason
    int64_t revisedChoices = 0; // choices discarded because their field or known pairs changed
    // The speed of the hands (Params::handWait; 0 = the fastest input): a pair is left alone this many frames
    // after it has appeared, and falls by itself, before the first button moves it. The wait ends earlier when
    // one more frame of it would lock the pair or take the placement chosen out of its reach. Frames a pair
    // waits for its choice (Free, lateScript) count as frames of this wait.
    int handWait = 0;
    int64_t handWaited = 0; // frames in which a pair was left alone for that reason (for tools)

    ~PlacementAgent() override { finish(); }

    // The pair in play is not where the buttons this agent gave would have brought it (an agent that plays a game
    // through another program, whose buttons reach that game late or not at all): the way to the placement chosen is
    // planned again from where `o` shows the pair. No way left to it: the pair goes to the placement nearest to it that
    // it can still reach (as a choice that came after the pair had fallen behind a column); none: down where it is.
    void steer(const Observation& o) {
        tune();
        if (o.self().phase != Phase::Control || late || target < 0) return;
        step = 0;
        reach(o, o.self().pair, target, ~0u);
    }
    // The pair in play taken up again where it is, after it had been taken as put (play/realgame_drive.h: the game stopped
    // and went on): a choice begun for the next pair is dropped, and the way to the placement chosen is planned again.
    void resume(const Observation& o) {
        finish();
        revising = false;
        steer(o);
    }
    // The placement the pair in play is brought to (-1: none yet).
    int targetPlacement() const { return target; }

    void startRound(int, uint64_t seed) override {
        finish();
        rng = Rng(seed, AGENT_STREAM);
        lateLog.clear();
        againLog.clear();
        climbing.clear();
        path.buttons.clear();
        step = 0, last = 0, target = -1, idle = 0;
        revising = false;
    }

    uint8_t frame(const Observation& o) override {
        tune();
        const Player& p = o.self();
        uint8_t b = 0;
        if (late && p.phase != Phase::Control) finish(); // the pair came to rest before its choice was made
        if (second && p.phase != Phase::Control && p.phase != Phase::Next) endSecond(-1); // ... before its second choice came
        if (p.phase == Phase::Next || p.phase == Phase::Control) {
            if (!revising && (thought || late || second) && !sameChoice(o)) {
                revising = true, ++revisedChoices;
                target = -1, step = 0;
                path.buttons.clear();
            }
            if (revising) {
                // A search owns the agent's working data until it returns. Keep sending input, then discard its result
                // before beginning a choice of the corrected field. No old result may become the new pair's target.
                if (!made() || (again_.valid() && again_.wait_for(std::chrono::seconds(0)) != std::future_status::ready))
                    return last = (p.phase == Phase::Control || p.wait == 1) && !downWhenPlaced && (!holdSecond || downKeepsWays(o)) ? BTN_DOWN : 0;
                finish();
                revising = false;
                target = -1, step = 0;
                path.buttons.clear();
                if (p.phase == Phase::Control) {
                    Observation fresh = o;
                    Player& q = fresh.player[fresh.me];
                    q.phase = Phase::Next, q.wait = 1, --q.nextPair;
                    think(fresh);
                    late = true;
                    if (!lateScript) lateLog.push_back(int(p.moves)), lateLog.push_back(0);
                }
            }
        }
        if (p.phase == Phase::Next && !p.field.dead()) {
            if (!thought && (p.wait == 1 || p.wait <= ahead)) {
                think(o);
            } else if (thought && !asked && o.mine().count() > shownAtThink) {
                // The screen shows a pair the choice did not know. The second choice is made with what
                // the screen shows in this frame, as soon as the first one is made.
                asked = pending = true, ++againAsked;
                seenAgain = sight(o);
            }
            if (pending) {
                if (thinking == Thinking::Thread && job.valid()) job.wait();
                if (made()) {
                    pending = false;
                    if (again(o)) thinkAgain(seenAgain);
                }
            }
            if (second && secondHere(false)) chosen = secondId, endSecond(0); // (it takes the place of the first before the pair appears)
            if (p.wait == 1) pending = false; // (the first choice is not made as the pair appears: no second one)
            if (p.wait == 1) {
                // The pair appears in this frame: the first button already moves it.
                PairControl pair = appearingPair(p, o.rules, o.timing);
                pair.held = last;
                born = pair;
                target = -1;
                step = 0;
                path.buttons.clear();
                idle = handWait;
                if (thinking == Thinking::Free && !made()) late = true, lateLog.push_back(int(p.moves)), lateLog.push_back(0);
                else if (lateScript && (lateLeft = scriptedLate(int(p.moves))) > 0) late = true;
                else route(o, pair);
            }
        } else if (late) {
            if (lateScript ? --lateLeft <= 0 : made()) late = false, route(o, p.pair, true);
        } else if (p.phase == Phase::Control) {
            if (second && (++secondFrames, secondHere(true))) {
                // The second choice has come while the pair is in the air: there, if a path still leads there.
                if (secondId >= 0 && secondId != target) {
                    InputPath other;
                    if (planner.planSure(p.field, p.pair, placement(secondId), o.timing, o.rules.speedPercent, other)) path = other, step = 0, target = secondId, ++againSwitched;
                    else ++againNoPath;
                }
                endSecond(secondFrames);
            }
            if (step >= path.frames() && target >= 0) {
                // The path is over and the pair is still in the air (not seen): from where it is now.
                step = 0;
                if (!planner.planSure(p.field, p.pair, placement(target), o.timing, o.rules.speedPercent, path)) path.buttons.clear();
            }
        }
        if (late) {
            if (!lateScript) ++lateLog.back();
            if (idle > 0) --idle; // (slow hands: the pair is left alone in this frame anyway)
            return ++lateFrames, last = holdSecond && !downWhenPlaced && downKeepsWays(o) ? BTN_DOWN : 0;
        }
        // (not while a second choice is being worked out: it works with the members of the class)
        if (p.phase == Phase::Control && target >= 0 && !second && !again_.valid()) airborne(o, coming, target, path.frames() - step);
        if (idle > 0 && (p.phase == Phase::Control || (p.phase == Phase::Next && p.wait == 1 && step == 0))) {
            // Slow hands (handWait): the pair is left alone in this frame, if a way still leads to its placement afterwards.
            if (target >= 0 && alone(o, p.phase == Phase::Control ? p.pair : born)) {
                --idle;
                path.buttons.clear(), step = 0; // (the way is planned again from where the pair is then)
                return ++handWaited, last = 0;
            }
            idle = 0;
        }
        if (second && holdSecond && target >= 0 && (p.phase == Phase::Control || (p.phase == Phase::Next && p.wait == 1 && step == 0))) {
            // A second choice is on its way: straight down, if nothing is lost by it (holdSecond).
            if (straight(o, p.phase == Phase::Control ? p.pair : born)) {
                path.buttons.clear(), step = 0; // (the way is planned again from where the pair is then)
                return ++againHeld, last = BTN_DOWN;
            }
        }
        if ((p.phase == Phase::Control || step == 0) && step < path.frames()) b = path.buttons[size_t(step++)];
        else if (p.phase == Phase::Control) b = BTN_DOWN;
        return last = b;
    }

protected:
    // The placement (0..21) for `pair`, the pair that appears next (with `ahead` 1: in the next frame).
    // `legal` = legalPlacements of the own field, not zero.
    virtual int choose(const Observation& o, Pair pair, unsigned legal) = 0;

    // The pair `colors` is in the air on its way to placement `id`, with `frames` frames of buttons
    // left: called in every such frame, when no choice is being worked out. For an agent that uses
    // the time the pair is in the air (X3Agent begins the search of its next choice).
    virtual void airborne(const Observation&, Pair, int, int) {}

    // The screen shows a pair that the choice of the pair that appears next did not know, and that
    // choice is made: is it to be made once more? (Asked once for a pair.) choose() is then called
    // again, with choosingAgain set.
    virtual bool again(const Observation&) { return false; }
    bool choosingAgain = false; // choose(): this is the second choice of the pair

    // What a choice is shown of the screen `o`: asked in the frame in which the choice is begun, on the thread
    // of frame(); what it returns is read (or copied) before the next call. An agent that sees the opponent
    // late returns another observation here (X3Agent, Params::seeLate). The own side must be that of `o`.
    virtual const Observation& sight(const Observation& o) { return o; }

    // Waits for a choice that is still being worked out, and drops it.
    void finish() {
        if (job.valid()) job.wait(), job = std::future<int>();
        if (again_.valid()) again_.wait(), again_ = std::future<int>();
        if (second) endSecond(-1); // (the round was over first)
        thought = late = pending = false;
    }

    Rng rng{0};

private:
    // A new observation may correct a field or a pair already used by a choice. Newly revealed pairs alone use
    // the ordinary second-choice policy; changed ones invalidate the choice even when the number known is unchanged.
    void rememberChoice(const Observation& o) {
        choiceField = o.self().field;
        choiceIndex = o.self().nextPair;
        choiceCount = 0;
        for (int i = choiceIndex; choiceCount < 3 && o.mine().known(i); ++i) choicePairs[choiceCount++] = o.mine().pair(i);
    }
    bool sameChoice(const Observation& o) const {
        const int index = o.self().nextPair - (o.self().phase == Phase::Control ? 1 : 0);
        if (index != choiceIndex || !(choiceField == o.self().field)) return false;
        for (int k = 0; k < choiceCount; ++k) {
            if (!o.mine().known(index + k)) return false;
            const Pair a = choicePairs[k], b = o.mine().pair(index + k);
            if (a.axis != b.axis || a.child != b.child) return false;
        }
        return true;
    }
    // The settings of the ways the planner makes.
    void tune() {
        planner.tapHold = tapHold, planner.heldFor[0] = tapsHeldFor[0], planner.heldFor[1] = tapsHeldFor[1], planner.downWhenPlaced = downWhenPlaced;
        planner.keepLow = keepLow && lowWays;
        planner.waitQuickLock = waitQuickLock, planner.spareFrame = spareFrame;
        planner.unsure13 = unsure13, planner.unsure14 = unsure14, planner.seenAfter = seenAfter;
    }
    // The placements choose() is offered for the pair `colors` (counted: a first choice, for climbs).
    unsigned offered(const Observation& o, Pair colors, bool counted) {
        const Player& p = o.self();
        unsigned legal = legalPlacements(p.field);
        lowWays = true;
        tune();
        if (legal && pathCheck) {
            // (only a field with a column eleven high or more can stand in the pair's way - kept low, a quick turn is
            //  needed between two higher columns anywhere)
            int h[W], top = 0;
            p.field.heights(h);
            for (int x = 0; x < W; ++x) top = h[x] > top ? h[x] : top;
            if (top >= 11 || keepLow) {
                const PairControl pair = spawnPair(o.timing);
                unsigned reached = reachedFrom(o, pair, legal);
                if (keepLow && !survives(p.field, reached, colors)) {
                    lowWays = false, climbs += counted;
                    letClimb(p.nextPair);
                    tune();
                    reached = reachedFrom(o, pair, legal);
                }
                if (reached) legal = reached;
            }
        }
        return legal;
    }
    // The placements of `legal` the planner finds a way to from `pair`.
    unsigned reachedFrom(const Observation& o, const PairControl& pair, unsigned legal) {
        unsigned reached = 0;
        InputPath there;
        for (int id = 0; id < PLACEMENTS; ++id)
            if ((legal >> id & 1) && planner.plan(o.self().field, pair, placement(id), o.timing, o.rules.speedPercent, there)) reached |= 1u << id;
        return reached;
    }
    // Does a placement of `ids` leave the 12th row of the third column empty once its chain has run?
    static bool survives(const GameField& field, unsigned ids, Pair colors) {
        for (int id = 0; id < PLACEMENTS; ++id) {
            if (!(ids >> id & 1)) continue;
            GameField g = field;
            place(g, placement(id), colors);
            runChain(g.f);
            if (!g.dead()) return true;
        }
        return false;
    }
    // Chooses the placement of the pair that appears next.
    void think(const Observation& o) {
        if (again_.valid()) again_.get(), again_ = std::future<int>(); // (a second choice whose pair had come to rest: its thread must have ended)
        thought = true, chosen = -1;
        rememberChoice(o);
        asked = pending = second = false, shownAtThink = o.mine().count();
        const Player& p = o.self();
        const Pair colors = o.mine().pair(p.nextPair);
        const unsigned legal = offered(o, colors, true);
        if (!legal) return;
        coming = colors;
        choosingAgain = false;
        const Observation& shown = sight(o);
        if (thinking == Thinking::Free || (thinking == Thinking::Thread && p.wait > 1)) {
            seen = shown;
            job = std::async(std::launch::async, [this, colors, legal] { return choose(seen, colors, legal); });
        } else {
            chosen = choose(shown, colors, legal);
        }
    }
    // The choice of that pair once more (again() said so). The first one stands until this one has come.
    void thinkAgain(const Observation& o) {
        if (job.valid()) chosen = job.get(), job = std::future<int>();
        const unsigned legal = offered(o, coming, false);
        if (!legal || chosen < 0) return;
        ++againBegun;
        rememberChoice(o);
        second = true, secondId = -1, secondFrames = 0, secondMove = int(o.self().moves);
        choosingAgain = true;
        if (thinking == Thinking::Free) {
            seen = o;
            again_ = std::async(std::launch::async, [this, legal] { return choose(seen, coming, legal); });
        } else {
            secondId = choose(o, coming, legal);
            secondAt = againScript ? scriptedAgain(secondMove) : againLate;
            if (secondAt < 0) second = false; // (in the round played again it did not come while the pair was in the air)
        }
    }
    // Has the second choice come? Free: its thread has ended. Otherwise: before the pair appears
    // when it comes at once, and in the air when its frame is here.
    bool secondHere(bool air) {
        if (again_.valid()) {
            if (again_.wait_for(std::chrono::seconds(0)) != std::future_status::ready) return false;
            secondId = again_.get(), again_ = std::future<int>();
            return true;
        }
        return air ? secondFrames >= secondAt : secondAt == 0;
    }
    // The second choice is over: it came `frames` frames after the pair appeared (-1 = it did not come in time).
    void endSecond(int frames) {
        second = false;
        againLog.push_back(secondMove), againLog.push_back(frames);
    }
    int scriptedAgain(int moves) const {
        for (size_t i = 0; i + 1 < againScript->size(); i += 2)
            if ((*againScript)[i] == moves) return (*againScript)[i + 1];
        return -1;
    }
    // holdSecond: may the pair, which the frame finds as `now`, go straight down in this frame? Yes
    // when the way to the first choice is then one frame shorter, and every placement that has a
    // way from here still has one from there.
    bool straight(const Observation& o, const PairControl& now) {
        const Player& p = o.self();
        const int left = path.frames() - step; // the frames of the way to the first choice from here
        if (left <= 1) return false;
        PairControl next = now;
        stepPair(next, p.field, BTN_DOWN, o.timing, o.rules.speedPercent);
        if (next.locked) return false;
        InputPath way;
        if (!planner.plan(p.field, next, placement(target), o.timing, o.rules.speedPercent, way) || way.frames() + 1 > left) return false;
        return keepsWays(o, now, next, target);
    }
    // holdSecond, no choice to bring the pair to yet (late, or a choice made again): may the pair go straight down in this
    // frame? Yes when every placement that has a way from where it is still has one from there. (Held down at once, a
    // pair to go up over a column twelve high lay on the column beside it on the real game.)
    bool downKeepsWays(const Observation& o) {
        const Player& p = o.self();
        const PairControl now = p.phase == Phase::Control ? p.pair : appearingPair(p, o.rules, o.timing);
        PairControl next = now;
        stepPair(next, p.field, BTN_DOWN, o.timing, o.rules.speedPercent);
        if (next.locked) return false;
        return keepsWays(o, now, next, -1);
    }
    // Has every placement but `except` that has a way from `now` still one from `next`?
    bool keepsWays(const Observation& o, const PairControl& now, const PairControl& next, int except) {
        const Player& p = o.self();
        InputPath way;
        const unsigned legal = legalPlacements(p.field);
        for (int id = 0; id < PLACEMENTS; ++id)
            if ((legal >> id & 1) && id != except && planner.plan(p.field, now, placement(id), o.timing, o.rules.speedPercent, way) &&
                !planner.plan(p.field, next, placement(id), o.timing, o.rules.speedPercent, way))
                return false;
        return true;
    }
    // handWait: may the pair, which the frame finds as `now`, be left alone in this frame? Yes when that does
    // not lock it and a way leads to the placement chosen from where it is then.
    bool alone(const Observation& o, const PairControl& now) {
        const Player& p = o.self();
        PairControl next = now;
        stepPair(next, p.field, 0, o.timing, o.rules.speedPercent);
        if (next.locked) return false;
        InputPath way;
        return planner.plan(p.field, next, placement(target), o.timing, o.rules.speedPercent, way);
    }
    bool made() const { return !job.valid() || job.wait_for(std::chrono::seconds(0)) == std::future_status::ready; }
    // The frames the pair after `moves` moves waited in the round of lateScript.
    int scriptedLate(int moves) const {
        for (size_t i = 0; i + 1 < lateScript->size(); i += 2)
            if ((*lateScript)[i] == moves) return (*lateScript)[i + 1];
        return 0;
    }
    // The buttons that bring `pair` to the placement chosen (waits for it).
    // fallen: the pair has been in the air without a choice (Free).
    void route(const Observation& o, const PairControl& pair, bool fallen = false) {
        int id = job.valid() ? job.get() : chosen;
        thought = false;
        const Player& p = o.self();
        const unsigned legal = legalPlacements(p.field);
        if (!fallen) {
            // (a placement that is legal and has no path from where a pair appears: the next legal one is taken)
            const int wanted = id;
            for (int tries = 0; id >= 0 && tries < PLACEMENTS; ++tries, id = (id + 1) % PLACEMENTS)
                if ((legal >> id & 1) && planner.planSure(p.field, pair, placement(id), o.timing, o.rules.speedPercent, path)) {
                    target = id;
                    break;
                }
            if (wanted >= 0 && target != wanted) ++rerouted, lastWanted = wanted, lastTaken = target;
            return;
        }
        if (id >= 0) reach(o, pair, id, legal);
    }
    // The way of `pair` to placement `id` (one of `legal`). No way leads there any more - the choice came, or the pair was
    // put right, when it had fallen behind a column -: to the placement nearest to it; none: down where it is. Kept below
    // the 13th row (keepLow) and finding no way at all: any way.
    void reach(const Observation& o, const PairControl& pair, int id, unsigned legal) {
        const Player& p = o.self();
        for (;;) {
            if ((legal >> id & 1) && planner.planSure(p.field, pair, placement(id), o.timing, o.rules.speedPercent, path)) {
                target = id;
                return;
            }
            const int best = nearest(o, pair, id, path);
            if (best >= 0) {
                target = best, ++steered;
                if (planner.keepLow && (legal >> id & 1)) {
                    // (for tools: a way through the 13th row leads there still)
                    InputPath high;
                    planner.keepLow = false;
                    steeredLow += planner.plan(p.field, pair, placement(id), o.timing, o.rules.speedPercent, high);
                    planner.keepLow = true;
                }
                return;
            }
            if (!planner.keepLow) break;
            lowWays = false, ++climbs;
            letClimb(p.nextPair - (p.phase == Phase::Control ? 1 : 0));
            tune();
        }
        path.buttons.clear();
    }
    void letClimb(int pair) {
        if (climbing.empty() || climbing.back() != pair) climbing.push_back(pair);
    }
    // The placement `pair` can still reach whose two pieces come down nearest to the columns of placement `id`; of equal
    // ones the one with the smaller number, and its way (`way`, left as it was when none). -1: none.
    int nearest(const Observation& o, const PairControl& pair, int id, InputPath& way) {
        const Player& p = o.self();
        const unsigned legal = legalPlacements(p.field);
        const Placement want = placement(id);
        int best = -1, bestApart = 0;
        InputPath there;
        for (int other = 0; other < PLACEMENTS; ++other) {
            if (!(legal >> other & 1)) continue;
            const Placement t = placement(other);
            const int apart = std::abs(int(t.x) - int(want.x)) + std::abs(t.childX() - want.childX());
            if (best >= 0 && apart >= bestApart) continue;
            if (planner.plan(p.field, pair, t, o.timing, o.rules.speedPercent, there)) best = other, bestApart = apart, std::swap(way, there);
        }
        return best;
    }

    InputPlanner planner;
    bool lowWays = true; // keepLow holds for the ways of the pair in play
    InputPath path;
    int step = 0, target = -1;
    int idle = 0; // handWait: the frames the pair in the air is still left alone
    uint8_t last = 0;
    bool thought = false; // the pair that appears next has its placement, or a thread at work on it
    bool revising = false;
    GameField choiceField;
    int choiceIndex = -1, choiceCount = 0;
    Pair choicePairs[3]{};
    bool late = false;    // Free: the pair is in the air and its placement is not made yet
    int lateLeft = 0;     // lateScript: the frames the pair still waits
    int chosen = -1;      // the placement, when it was worked out at once; -1 = none is legal
    Pair coming{};        // the pair the choice is for
    std::future<int> job; // ... or the thread
    Observation seen;     // what the thread looks at
    int shownAtThink = 0; // the pairs the screen had shown when that choice was begun
    bool asked = false;   // the screen has shown more since (once for a pair)
    bool pending = false; // ... and again() is asked as soon as the first choice is made
    Observation seenAgain; // ... what the screen showed in that frame
    bool second = false;  // a second choice has been begun and has not come yet
    int secondId = -1;    // ... the placement, when it was worked out at once
    std::future<int> again_; // ... or its thread (valid, with second false: its pair has come to rest and it still works)
    int secondAt = 0;     // the frames after the pair appears at which a second choice worked out at once comes
    int secondFrames = 0; // the frames the pair has been in the air
    int secondMove = 0;   // the moves the player had made when it was begun
    PairControl born;     // the pair as it appears (in the frame it appears)
};

// Puts every pair on a random legal placement, with the fastest input.
class RandomAgent : public PlacementAgent {
protected:
    int choose(const Observation&, Pair, unsigned legal) override {
        int id;
        do id = int(rng.below(PLACEMENTS));
        while (!(legal >> id & 1));
        return id;
    }
};

} // namespace juraic::ai
