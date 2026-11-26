#pragma once
// The one door every AI goes through, in the game and in the tools: before each
// frame it is shown what is on the screen and answers with the buttons of that
// frame. A round played by agents is a list of buttons like any other round,
// so it can be saved and watched as a replay.
#include "input.h"
#include "observe.h"
#include "params.h"
#include "../core/diagnostic_trace.h"
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
    const uint64_t diagnosticAgent=++inputdiag::agentSerial;
    uint64_t diagnosticRound=0, diagnosticSerial=0, diagnosticMain=0, diagnosticSecond=0, diagnosticSelected=0;
    uint64_t diagnosticTargetDecision=0;
    int diagnosticTargetPair=-1,diagnosticMainPair=-1,diagnosticSecondPair=-1;
    struct Choice {
        int id = -1;
        bool setup = false;
    };
    // The settings of the ways a choice reads its own seat by, as they stand when the choice begins (taken on the thread
    // of frame(): a choice on a thread of its own reads these, not the members, which frame() may set meanwhile).
    struct WayRead {
        bool fastest = false;
        int states = 0, target = -1, soft = 0;
        int64_t check = -1, lock = -1;
    };
    virtual WayRead wayRead() const { return {fastestWays, fastestStates, wayTarget, waySoft, wayCheck, wayLock}; }
    Choice diagnosticChoose(const Observation& shown,Pair pair,unsigned legal,uint64_t decision,int reason,const WayRead& way){
        // (Observation::fastestRead: the reading takes this seat's pairs the way they go - the way over soonest with
        //  fastestWays - and, with the cancel, the other seat's the soonest way too. Without either, `shown` as it is.)
        thread_local Observation marked;
        const bool rival = shown.rules.cancel && shown.players == 2;
        const Observation* use = &shown;
        if (way.fastest || rival || way.target >= 0) {
            marked = shown;
            marked.fastestRead[shown.me] = way.fastest ? Observation::FASTEST_OWN : 0;
            marked.fastestReadStates[shown.me] = way.fastest ? way.states : 0;
            if (way.fastest || way.target >= 0) marked.ownWayTarget = way.target, marked.ownWayCheck = way.check, marked.ownWayLock = way.lock, marked.ownWaySoft = way.soft;
            if (shown.players == 2) marked.fastestRead[1 - shown.me] = rival ? Observation::FASTEST_BOUND : 0;
            use = &marked;
        }
        const Observation& o = *use;
        const auto begin=inputdiag::clock();const int result=choose(o,pair,legal);
        inputdiag::trace.emit(inputdiag::CHOSEN,{int64_t(diagnosticAgent),int64_t(diagnosticRound),int64_t(decision),o.frame,
            o.self().nextPair+1,reason,result,(inputdiag::clock()-begin)/10});
        return {result, equivalentSetupRoute && setupChoice()};
    }
    void diagnosticTarget(const Observation& o,int reason,uint64_t decision,int previous){
        const int observedPair=o.self().nextPair+(o.self().phase==Phase::Next?1:0);
        if(decision==diagnosticMain)diagnosticTargetPair=diagnosticMainPair;
        else if(decision==diagnosticSecond)diagnosticTargetPair=diagnosticSecondPair;
        diagnosticTargetDecision=decision;
        inputdiag::trace.emit(inputdiag::TARGET,{int64_t(diagnosticAgent),int64_t(diagnosticRound),int64_t(decision),o.frame,
            diagnosticTargetPair,target,reason,previous,observedPair});
    }
    int choicePairLimit = 3; // immutable for the configured agent
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
    // in the air; a second choice asked for in the air, askInAir, counts from the frame it was asked
    // in), one after the other (the game writes them into its replay).
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
    // The second choice is asked for after the pair has appeared too, when the screen shows a pair the first choice did not
    // know only then (a choice made ahead of the frame its pair appears in, for buttons that reach the game late): it is
    // made as for the pair before it appears, and taken in the air.
    bool askInAir = false;
    // A choice dropped because its field or known pairs changed has the searches of its thread stopped at once (abandon()),
    // so that the choice of the field as it is now is begun without waiting for their time to run out. false: they run
    // to their end and are dropped then (the default).
    bool cutStale = false;
    // fastestAgain with holdKeepsWay: the search of the way over soonest is begun, and its way taken, while a second
    // choice is on its way too. false: not while a second choice is on its way (the default).
    bool searchWhileSecond = false;
    // A first choice not made as its pair appears drops the second choice asked for; with this, that one is asked for
    // once more in the air (askInAir) once the first is made. false: no second choice for that pair (the default).
    bool retryInAir = false;
    // A second choice is asked for as the pair appears (or in the air, askInAir) when the screen shows no pair the first
    // choice did not know: the same pairs, with the fields as they are then. Once for a pair, and not when a second choice
    // has been begun for it already. false: only when the screen shows a pair the first choice did not know (the default).
    bool againFresh = false;
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
    // The way to the placement chosen is the one of the fewest frames among the buttons of every frame, when that is
    // shorter than the straight way (InputPlanner::fastest). false: the straight way (the default).
    bool fastestWays = false;
    bool exFlip = false;
    bool fastQuietInput = false; // Explicit Lv38, ordinary controls only.
    bool fastSoft = false, fastBusy = false;
    int fastStates = 0;
    int64_t fastStateLimitHits = 0;
    // Lv 37 considers exchanging the places of two pieces of the same colour.
    bool equivalentSetupRoute = false;
    // ... the states of a search made in the frame the way is needed in, and of one made ahead on a thread of its own
    // once the placement of the pair to come is chosen (InputPlanner::fastestStates, fastestAhead; 0: none made
    // ahead). fastestWaits: the frame a way is needed in waits for a search made ahead (InputPlanner::aheadWaits).
    int fastestStates = 50000, fastestAhead = 0;
    bool fastestWaits = false;
    // holdSecond: a frame in which the way to the first choice itself only holds the down button is a frame of that way.
    // The way is kept - not dropped to be planned again from where the pair is a frame later, which a way made ahead
    // (fastestAhead: a search too long for the frame its buttons are for) does not survive. false: as before.
    bool holdKeepsWay = false;
    // holdSecond: nor does the way to the first choice take the pair, in a frame, to where a placement that had a way has
    // none - no button is pressed in that frame, when that keeps them all (on the real game a pair gone down beside a
    // column on its way to the first choice went up again by turns for a second choice behind it, or could not).
    bool holdLevel = false;
    // holdSecond, keepsWays: when a pair may go down while the second choice is awaited. 0: whenever every placement
    // keeps a way (the pair may then have to go back up - over a column, by a kick or a quick turn - when the choice
    // comes). 1: not where a way would then be two frames longer, or a frame longer and go back up. 2: not where a way
    // would then be longer at all.
    int holdDown = 1;
    // The ways over soonest: a way planned for a pair in the air (the second choice taken there, a way planned again
    // after frames held straight down) is searched within the frame its buttons are for (fastestStates). Above 0: the
    // search of fastestAhead is begun then as well, on its thread, from where that way has the pair so many frames
    // on, and the way it finds is taken there when it has come by then. 0: none (the default).
    int fastestAgain = 0;
    // The cells of the 13th and 14th rows of the own field that may be the other way, and the frames after the pair is
    // shown elsewhere than its way has it by which a way from where it is then begins (InputPlanner::planSure: the ways
    // to the placement chosen).
    uint8_t unsure13 = 0, unsure14 = 0;
    int seenAfter = 0;
    const InputPlanner& ways() const { return planner; } // (for tools: InputPlanner::sureWays and the others)
    int64_t climbs = 0;    // pairs for which every placement reached below the 13th row filled it (for tools)
    std::vector<int> climbing; // ... and the pairs whose ways were let go up into the 13th row so, by their number (for tools)
    int64_t againHeld = 0; // frames in which a pair went straight down for that reason
    int64_t againLevel = 0; // frames in which a pair was left alone, waiting for its second choice (holdLevel)
    int64_t againWays = 0; // ways of pairs in the air planned once more where a search begun for them had them (fastestAgain; for tools)
    int64_t revisedChoices = 0; // choices discarded because their field or known pairs changed
    int64_t unknownFrames = 0;  // frames a pair in the air waited for its colours to be known (unknownPair; for tools)
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
        step = 0, againAt = -1;
        reach(o, o.self().pair, target, ~0u);
    }
    // The pair in play taken up again where it is, after it had been taken as put (play/realgame_drive.h: the game stopped
    // and went on): a choice begun for the next pair is dropped, and the way to the placement chosen is planned again.
    void resume(const Observation& o) {
        finish();
        revising = false;
        steer(o);
    }
    // Recover a still-public current pair, never carry a future pair's search
    // across a false predicted lock. Reuse its stored goal, or ask for a new
    // placement using only the already-visible current pair.
    bool recoveryReady() const {
        return !late && (!job.valid() || job.wait_for(std::chrono::seconds(0))==std::future_status::ready) &&
            (!again_.valid() || again_.wait_for(std::chrono::seconds(0))==std::future_status::ready);
    }
    void recoverKnownPair(const Observation& o,int goal,uint64_t goalDecision=0) {
        finish();revising=false;step=0;path.buttons.clear();idle=0;appearedPair=o.self().nextPair;
        targetSetup = false;
        if(goal>=0 && goal<PLACEMENTS){
            target=goal;diagnosticSelected=diagnosticTargetDecision=goalDecision;
            diagnosticTargetPair=o.self().nextPair;
            steer(o);
            if(path.frames()>0 || (o.self().pair.x==placement(target).x && o.self().pair.dir==placement(target).dir))return;
        }
        target=-1;unknownPair=false;
        lateLog.push_back(int(o.self().moves));lateLog.push_back(0);
        thinkInAir(o);
    }
    // The placement the pair in play is brought to (-1: none yet).
    int targetPlacement() const { return target; }

    void startRound(int seat, uint64_t seed) override {
        finish();
        ++diagnosticRound;diagnosticMain=diagnosticSecond=diagnosticSelected=diagnosticTargetDecision=0;diagnosticTargetPair=diagnosticMainPair=diagnosticSecondPair=-1;
        inputdiag::trace.emit(inputdiag::ROUND,{int64_t(diagnosticAgent),int64_t(diagnosticRound),seat,int64_t(seed)});
        rng = Rng(seed, AGENT_STREAM);
        lateLog.clear();
        againLog.clear();
        climbing.clear();
        path.buttons.clear();
        step = 0, last = 0, target = -1, idle = 0, appearedPair = -1, againAt = -1;
        heldRun[0] = heldRun[1] = 0;
        revising = targetSetup = false;
    }

    uint8_t frame(const Observation& o) override {
        if (tapHold > 1) countHeld();
        tune();
        const Player& p = o.self();
        uint8_t b = 0;
        if (late && p.phase != Phase::Control) finish(); // the pair came to rest before its choice was made
        if (second && p.phase != Phase::Control && p.phase != Phase::Next) endSecond(-1); // ... before its second choice came
        if (p.phase == Phase::Next || p.phase == Phase::Control) {
            if (!revising && !unknownPair && (thought || late || second) && !sameChoice(o)) {
                revising = true, ++revisedChoices;
                target = -1, dropWay();
                if (cutStale) abandon(); // (the searches of the dropped choice end now)
            }
            if (revising) {
                // A search owns the agent's working data until it returns. Keep sending input, then discard its result
                // before beginning a choice of the corrected field. No old result may become the new pair's target.
                if (!made() || (again_.valid() && again_.wait_for(std::chrono::seconds(0)) != std::future_status::ready))
                    return last = (p.phase == Phase::Control || p.wait == 1) && !downWhenPlaced && (!holdSecond || downKeepsWays(o)) ? BTN_DOWN : 0;
                finish();
                revising = false;
                target = -1, dropWay();
                if (p.phase == Phase::Control) {
                    thinkInAir(o);
                    if (!lateScript) logLate(p);
                }
            }
        }
        if (p.phase == Phase::Control && appearedPair != p.nextPair) {
            // The frame before the pair appeared was not shown (frames the caller could not read): the pair is taken up
            // where it is, with the choice made for it - never with the placement of the pair before it.
            appearedPair = p.nextPair;
            if (!late) {
                target = -1, dropWay(), idle = 0, pending = false;
                if (thought && sameChoice(o)) {
                    if (thinking == Thinking::Free && !made()) late = true, logLate(p);
                    else route(o, p.pair, true);
                }
            }
        }
        if (p.phase == Phase::Next && !p.field.dead()) {
            // (a pair whose colours are not known - not read on the screen of another program - is chosen once they are)
            if (!thought && (p.wait == 1 || p.wait <= ahead) && o.mine().known(p.nextPair)) {
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
            if (againFresh && p.wait == 1 && thought && !askedFresh && !secondBegun && !second && !pending && made()) {
                // (againFresh: the choice once more as the pair appears, with what the screen shows now)
                askedFresh = true, ++againAsked;
                againAsIs = true;
                if (again(o)) thinkAgain(sight(o));
                againAsIs = false;
            }
            if (second && secondHere(false)) {chosen = secondChoice;diagnosticMain=diagnosticSecond;diagnosticMainPair=diagnosticSecondPair;endSecond(0);} // (it takes the place of the first before the pair appears)
            // (the ways over soonest: the way of the pair to come is searched ahead once its placement is chosen)
            if (fastestWays && fastestAhead > 0 && p.wait > 1 && thought && !pending && !revising && !second && made()) {
                if (job.valid()) chosen = job.get();
                if (chosen.id >= 0 && (legalPlacements(p.field) >> chosen.id & 1)) {
                    PairControl pair = appearingPair(p, o.rules, o.timing);
                    pair.held = 0; // (as the frame before a pair appears leaves it: no button of the way before)
                    planner.prepareFastest(p.field, pair, placement(chosen.id), o.timing, o.rules.speedPercent);
                }
            }
            if (p.wait == 1) {
                // (the first choice is not made as the pair appears: no second one - retryInAir: asked for in the air)
                if (pending && retryInAir) asked = false;
                pending = false;
                // The pair appears in this frame: the first button already moves it.
                PairControl pair = appearingPair(p, o.rules, o.timing);
                pair.held = last;
                born = pair, appearedPair = p.nextPair + 1;
                target = -1, dropWay();
                idle = handWait;
                if (!thought) late = true, unknownPair = true, logLate(p);
                else if (thinking == Thinking::Free && !made()) late = true, logLate(p);
                else if (lateScript && (lateLeft = fromScript(*lateScript, int(p.moves), 0)) > 0) late = true; // (the frames it waited there)
                else route(o, pair);
            }
        } else if (late) {
            if (unknownPair && p.phase == Phase::Control && o.mine().known(p.nextPair - 1)) unknownPair = false, thinkInAir(o);
            if (!unknownPair && (lateScript ? --lateLeft <= 0 : made())) late = false, route(o, p.pair, true);
        } else if (p.phase == Phase::Control) {
            // (againFresh: once in the air as well, when no second choice was begun as the pair appeared)
            const bool newInAir = !asked && o.mine().count() > shownAtThink;
            const bool freshInAir = againFresh && !askedFresh && !secondBegun;
            if (askInAir && !second && target >= 0 && p.nextPair - 1 == choiceIndex && (newInAir || freshInAir)) {
                asked = true, ++againAsked;
                if (againFresh) askedFresh = true;
                againAsIs = !newInAir;
                const Observation before = beforeItAppears(o);
                if (made() && again(before)) thinkAgain(sight(before));
                againAsIs = false;
            }
            if (second && (++secondFrames, secondHere(true))) {
                // The second choice has come while the pair is in the air: there, if a path still leads there.
                int nextTarget = secondChoice.id;
                const bool same = nextTarget == target ||
                    (targetSetup && secondChoice.setup && nextTarget >= 0 && reversePlacement(nextTarget) == target &&
                     planner.canReverse(currentColors(o)));
                if (same) targetSetup = secondChoice.setup;
                else if (nextTarget >= 0) {
                    InputPath other;
                    if (planTarget(o, p.pair, nextTarget, other, secondChoice.setup)) {
                        const int previous=target;path = other, step = 0, target = nextTarget, ++againSwitched;
                        targetSetup = secondChoice.setup;
                        diagnosticSelected=diagnosticSecond;diagnosticTarget(o,2,diagnosticSecond,previous);
                    }
                    else ++againNoPath;
                }
                endSecond(secondFrames);
                if ((same || nextTarget == target) && step == 0) searchAgain(o);
            }
            if (step >= path.frames() && target >= 0) {
                // The path is over and the pair is still in the air (not seen): from where it is now.
                step = 0;
                const int previous = target;
                if (!planTarget(o, p.pair, target, path, targetSetup && !second)) path.buttons.clear();
                else {
                    if (target != previous) diagnosticTarget(o,3,diagnosticSelected,previous);
                    searchAgain(o);
                }
            }
            if (againAt >= 0 && step == againAt && againPair == p.nextPair && againTarget == target && (!second || searchesWhileSecond())) {
                // (fastestAgain) The pair is where the search begun for it has it: its way, when it has come.
                againAt = -1;
                InputPath sooner;
                // (not waited for, but by fastestWaits: a search still under way leaves the way as it is)
                const int64_t before = planner.aheadUsed;
                const auto ends = [&](const std::vector<uint8_t>& b) { return FastestSearch::ends(p.field, p.pair, b, o.timing, o.rules.speedPercent); };
                const std::vector<uint8_t> rest(path.buttons.begin() + std::min(step, path.frames()), path.buttons.end());
                if ((fastestWaits || !planner.preparing()) && planner.planSure(p.field, p.pair, placement(target), o.timing, o.rules.speedPercent, sooner) &&
                    (ends(rest) == 0 || (ends(sooner.buttons) > 0 && ends(sooner.buttons) <= ends(rest)))) {
                    path = sooner, step = 0;
                    againWays += planner.aheadUsed > before; // (the search's own answer: the pair was where it was begun for)
                }
            }
        }
        if (late) {
            unknownFrames += unknownPair;
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
                dropWay(); // (the way is planned again from where the pair is then)
                return ++handWaited, last = 0;
            }
            idle = 0;
        }
        if (second && holdSecond && target >= 0 && (p.phase == Phase::Control || (p.phase == Phase::Next && p.wait == 1 && step == 0))) {
            // A second choice is on its way: straight down, if nothing is lost by it (holdSecond). (holdKeepsWay: the way
            // itself goes straight down in this frame - it is followed.)
            const bool onWay = holdKeepsWay && step < path.frames() && path.buttons[size_t(step)] == BTN_DOWN;
            if (!onWay && straight(o, p.phase == Phase::Control ? p.pair : born)) {
                path.buttons.clear(), step = 0; // (the way is planned again from where the pair is then)
                return ++againHeld, last = BTN_DOWN;
            }
            if (holdLevel && p.phase == Phase::Control && step < path.frames()) {
                const PairControl next = stepped(o, p.pair, path.buttons[size_t(step)]);
                if (!next.locked && !keepsWays(o, p.pair, next, -1)) {
                    const PairControl rest = stepped(o, p.pair, 0);
                    if (!rest.locked && keepsWays(o, p.pair, rest, -1)) {
                        path.buttons.clear(), step = 0; // (the way is planned again from where the pair is then)
                        return ++againLevel, last = 0;
                    }
                }
            }
        }
        if ((p.phase == Phase::Control || (p.phase == Phase::Next && p.wait == 1 && step == 0)) && step < path.frames()) b = path.buttons[size_t(step++)];
        else if (p.phase == Phase::Control) b = BTN_DOWN;
        return last = b;
    }

protected:
    // The placement (0..21) for `pair`, the pair that appears next (with `ahead` 1: in the next frame).
    // `legal` = legalPlacements of the own field, not zero.
    virtual int choose(const Observation& o, Pair pair, unsigned legal) = 0;
    virtual bool setupChoice() const { return false; }
    virtual void noteEquivalentSetup(const Observation&, Pair, int, int) {}

    // The pair `colors` is in the air on its way to placement `id`, with `frames` frames of buttons
    // left: called in every such frame, when no choice is being worked out. For an agent that uses
    // the time the pair is in the air (X3Agent begins the search of its next choice).
    virtual void airborne(const Observation&, Pair, int, int) {}

    // The screen shows a pair that the choice of the pair that appears next did not know, and that
    // choice is made: is it to be made once more? (Asked once for a pair; retryInAir and againFresh ask
    // once more.) choose() is then called again, with choosingAgain set.
    virtual bool again(const Observation&) { return false; }
    bool choosingAgain = false; // choose(): this is the second choice of the pair
    bool againAsIs = false;     // again(): asked by againFresh, the screen showing no pair the first choice did not know

    // cutStale: the choice under way (and a second one) is dropped; the searches of its threads are to end at once.
    // Called on the thread of frame().
    virtual void abandon() {}
    // A choice (or a second one) is about to begin, on the thread of frame(), no thread of an earlier choice at work.
    virtual void choiceBegins() {}

    // What a choice is shown of the screen `o`: asked in the frame in which the choice is begun, on the thread
    // of frame(); what it returns is read (or copied) before the next call. An agent that sees the opponent
    // late returns another observation here (X3Agent, Params::seeLate). The own side must be that of `o`.
    virtual const Observation& sight(const Observation& o) { return o; }

    // Waits for a choice that is still being worked out, and drops it.
    void finish() {
        if (job.valid()) job.wait(), job = std::future<Choice>();
        if (again_.valid()) again_.wait(), again_ = std::future<Choice>();
        if (second) endSecond(-1); // (the round was over first)
        thought = late = pending = unknownPair = false;
    }

    Rng rng{0};

private:
    // A new observation may correct a field or a pair already used by a choice. Newly revealed pairs alone use
    // the ordinary second-choice policy; changed ones invalidate the choice even when the number known is unchanged.
    void rememberChoice(const Observation& o) {
        choiceField = o.self().field;
        choiceIndex = o.self().nextPair;
        choiceCount = 0;
        for (int i = choiceIndex; choiceCount < choicePairLimit && choiceCount < MAX_KNOWN && o.mine().known(i); ++i) choicePairs[choiceCount++] = o.mine().pair(i);
    }
    bool sameChoice(const Observation& o) const {
        const int index = o.self().nextPair - (o.self().phase == Phase::Control ? 1 : 0);
        if (index != choiceIndex || !(choiceField == o.self().field)) return false;
        for (int k = 0; k < choiceCount; ++k)
            if (!o.mine().known(index + k) || !samePair(choicePairs[k], o.mine().pair(index + k))) return false;
        return true;
    }
    // The settings of the ways the planner makes.
    void tune() {
        planner.tapHold = tapHold, planner.heldFor[0] = tapsHeldFor[0], planner.heldFor[1] = tapsHeldFor[1], planner.downWhenPlaced = downWhenPlaced;
        planner.keepLow = keepLow && lowWays;
        planner.waitQuickLock = waitQuickLock, planner.spareFrame = spareFrame;
        planner.fastest = fastestWays, planner.fastestStates = fastestStates, planner.fastestAhead = fastestAhead, planner.aheadWaits = fastestWaits;
        planner.unsure13 = unsure13, planner.unsure14 = unsure14, planner.seenAfter = seenAfter;
        for (WaysFrom& w : waysFrom) w.frame = -1;
    }
    // A way of the pair `from` on the own field to placement `id` (InputPlanner::plan), and one that holds where the
    // field may be the other way (InputPlanner::planSure).
    bool wayTo(const Observation& o, const PairControl& from, int id, InputPath& way) {
        return planner.plan(o.self().field, from, placement(id), o.timing, o.rules.speedPercent, way);
    }
    bool sureWayTo(const Observation& o, const PairControl& from, int id, InputPath& way) {
        return planner.planSure(o.self().field, from, placement(id), o.timing, o.rules.speedPercent, way);
    }
    static Pair currentColors(const Observation& o) {
        const Player& p = o.self();
        const bool inAir = p.phase == Phase::Control;
        const int index = p.nextPair - int(inAir);
        if (!o.mine().known(index)) return {};
        const Pair colors = o.mine().pair(index);
        return !inAir || samePair(colors, p.colors) ? colors : Pair{};
    }
    static bool quietForSetup(const Observation& o) {
        if (o.self().incoming.total() || o.busyChain[o.me]) return false;
        if (o.players < 2) return true;
        const Player& other = o.opponent();
        if (other.incoming.total() || o.busyChain[1 - o.me] || other.phase == Phase::Wave ||
            other.phase == Phase::Decide || other.phase == Phase::Garbage) return false;
        if (other.phase != Phase::Split && other.phase != Phase::Land) return true;
        Wave wave;
        return !other.chain.chains && !findWaveFor(other.field.f, wave, o.rules.clearCount, false);
    }
protected:
    static bool fastInputAllowed(const Observation& o, bool allowBusy) {
        return allowBusy ? !o.self().incoming.total() && !o.busyChain[o.me] : quietForSetup(o);
    }
#ifdef JURAIC_LV38_REPLAY_AUDIT
    virtual void auditInput(const Observation&, const PairControl&, int, unsigned, Pair, const InputPath&, bool) {}
#endif
private:
    bool planTarget(const Observation& o, const PairControl& from, int& id, InputPath& way, bool setup, unsigned legal = ~0u) {
        const GameField& field = o.self().field;
        const Pair colors = setup || exFlip ? currentColors(o) : Pair{};
        if (exFlip && planner.canReverse(colors, true)) {
            const unsigned allowed = legal == ~0u ? legalPlacements(field) | (1u << id) : legal;
            return planner.planFastestEquivalent(field, from, id, allowed, colors, o.timing, o.rules, way);
        }
        if (!planner.canReverse(colors, true) || !quietForSetup(o)) return (legal >> id & 1) && sureWayTo(o, from, id, way);
        // Replanning an airborne pair may still reach its previous target even when the spawn reach mask excludes it.
        if (legal == ~0u) legal = legalPlacements(field) | (1u << id);
        const int before = id;
        const bool found = planner.planEquivalent(field, from, id, legal, colors, o.timing, o.rules, way);
        if (found && id != before) noteEquivalentSetup(o, colors, before, id);
        return found;
    }
    // The pair one frame on, with `buttons` held, on the own field.
    static PairControl stepped(const Observation& o, PairControl pair, uint8_t buttons) {
        stepPair(pair, o.self().field, buttons, o.timing, o.rules.speedPercent);
        return pair;
    }
    // The way of the pair in play is to be planned again (from where it is then).
    void dropWay() { path.buttons.clear(), step = 0; }
    // The pair of the player `p` begins to wait for its choice (lateLog).
    void logLate(const Player& p) { lateLog.push_back(int(p.moves)), lateLog.push_back(0); }
    // `o` with the pair in the air taken for the pair that is about to appear: a choice made for it now.
    static Observation beforeItAppears(const Observation& o) {
        Observation before = o;
        Player& q = before.player[before.me];
        // (Observation::ownAir: the reading takes the pair from where it is in mid-air)
        before.ownAir = q.phase == Phase::Control && !q.pair.locked;
        before.ownAirPair = q.pair;
        before.ownAirSeat = before.me;
        q.phase = Phase::Next, q.wait = 1, --q.nextPair;
        before.busyLeft[before.me] = 0;
        return before;
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
            // (a choice made in mid-air, ownPairInAir - a second choice, or the pair chosen anew: on any field, the pair
            //  may be below a column it would have to pass)
            const PairControl* air = ownPairInAir(o);
            if (top >= 11 || keepLow || air) {
                const PairControl pair = air ? *air : spawnPair(o.timing);
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
            if ((legal >> id & 1) && wayTo(o, pair, id, there)) reached |= 1u << id;
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
    // Observation::ownWayTarget, ownWayCheck: the pair in mid-air and its buttons, as they stand when a choice begins
    // (taken on the thread of frame(), read by the choice).
    int wayTarget = -1;
    int64_t wayCheck = -1, wayLock = -1; // (wayLock, waySoft: Observation::ownWayLock, ownWaySoft)
    int waySoft = 0;
    void noteWay(const Observation& o) {
        wayTarget = -1, wayCheck = -1, wayLock = -1, waySoft = 0;
        const Player& p = o.self();
        // (the pair in mid-air: as observed, or as beforeItAppears keeps it - Observation::ownAir; without fastestWays only
        //  the latter, the choice made in mid-air, whose reading goes on with the buttons the pair is played with too)
        const PairControl* air = ownPairInAir(o);
        if (!(fastestWays || o.ownAir) || !air || air->locked || target < 0 || step >= path.frames()) return;
        const PairControl& pair = *air;
        const std::vector<uint8_t> rest(path.buttons.begin() + step, path.buttons.end());
        const int end = FastestSearch::ends(p.field, pair, rest, o.timing, o.rules.speedPercent);
        if (end <= 0) return;
        // (the buttons lock the pair with the last of them: ends() gives 0 otherwise)
        PairControl q = pair;
        int soft = 0;
        for (const uint8_t b : rest) soft += stepPair(q, p.field, b, o.timing, o.rules.speedPercent).dropBonus;
        wayTarget = target, wayCheck = o.frame + end - 1, wayLock = o.frame + int64_t(rest.size()) - 1, waySoft = soft;
    }
    // Chooses the placement of the pair that appears next.
    void think(const Observation& o) {
        if (again_.valid()) again_.get(); // (a second choice whose pair had come to rest: its thread must have ended)
        thought = true, chosen = {};
        rememberChoice(o);
        asked = pending = second = false, shownAtThink = o.mine().count();
        askedFresh = secondBegun = false;
        const Player& p = o.self();
        const Pair colors = o.mine().pair(p.nextPair);
        const unsigned legal = offered(o, colors, true);
        const auto diagnosticRequest=diagnosticMain=++diagnosticSerial;
        diagnosticMainPair=p.nextPair+1;
        inputdiag::trace.emit(inputdiag::THINK,{int64_t(diagnosticAgent),int64_t(diagnosticRound),int64_t(diagnosticRequest),
            o.frame,p.nextPair+1,0,legal,colors.axis,colors.child});
        if (!legal) return;
        coming = colors;
        choosingAgain = false;
        choiceBegins();
        const Observation& shown = sight(o);
        noteWay(shown);
        if (thinking == Thinking::Free || (thinking == Thinking::Thread && p.wait > 1)) {
            seen = shown;
            job = std::async(std::launch::async, [this, colors, legal, diagnosticRequest, way = wayRead()] { return diagnosticChoose(seen, colors, legal, diagnosticRequest,0,way); });
        } else {
            chosen = diagnosticChoose(shown, colors, legal,diagnosticRequest,0,wayRead());
        }
    }
    // The pair in the air chosen now (its choice dropped, or its colours known only now), as the pair about to appear;
    // not while its colours are not known (unknownPair: the pair waits for them, late).
    void thinkInAir(const Observation& o) {
        late = true;
        if (!o.mine().known(o.self().nextPair - 1)) {
            unknownPair = true;
            return;
        }
        const Observation fresh = beforeItAppears(o);
        think(fresh);
    }
    // The choice of that pair once more (again() said so). The first one stands until this one has come.
    void thinkAgain(const Observation& o) {
        if (job.valid()) chosen = job.get();
        const unsigned legal = offered(o, coming, false);
        if (!legal || chosen.id < 0) return;
        ++againBegun;
        const auto diagnosticRequest=diagnosticSecond=++diagnosticSerial;
        diagnosticSecondPair=o.self().nextPair+1;
        inputdiag::trace.emit(inputdiag::THINK,{int64_t(diagnosticAgent),int64_t(diagnosticRound),int64_t(diagnosticRequest),
            o.frame,o.self().nextPair+1,1,legal,coming.axis,coming.child});
        rememberChoice(o);
        second = true, secondChoice = {}, secondFrames = 0, secondMove = int(o.self().moves);
        secondBegun = true;
        choosingAgain = true;
        choiceBegins();
        noteWay(o);
        if (thinking == Thinking::Free) {
            seen = o;
            again_ = std::async(std::launch::async, [this, legal, diagnosticRequest, way = wayRead()] { return diagnosticChoose(seen, coming, legal,diagnosticRequest,1,way); });
        } else {
            secondChoice = diagnosticChoose(o, coming, legal,diagnosticRequest,1,wayRead());
            secondAt = againScript ? fromScript(*againScript, secondMove, -1) : againLate;
            if (secondAt < 0) second = false; // (in the round played again it did not come while the pair was in the air)
        }
    }
    // Has the second choice come? Free: its thread has ended. Otherwise: before the pair appears
    // when it comes at once, and in the air when its frame is here.
    bool secondHere(bool air) {
        if (again_.valid()) {
            if (again_.wait_for(std::chrono::seconds(0)) != std::future_status::ready) return false;
            secondChoice = again_.get(); // (get() leaves the future empty: valid() is false from here on)
            return true;
        }
        return air ? secondFrames >= secondAt : secondAt == 0;
    }
    // The second choice is over: it came `frames` frames after the pair appeared (-1 = it did not come in time).
    void endSecond(int frames) {
        second = false;
        againLog.push_back(secondMove), againLog.push_back(frames);
    }
    // What a log of a round played again (lateLog, againLog: the moves, then a number, pair after pair) holds for the
    // pair after `moves` moves; `none` when it holds nothing for it.
    static int fromScript(const std::vector<int>& script, int moves, int none) {
        for (size_t i = 0; i + 1 < script.size(); i += 2)
            if (script[i] == moves) return script[i + 1];
        return none;
    }
    // fastestAgain: the way just planned for the pair in the air (path, from its step `step` on) was searched within the
    // frame. The search of fastestAhead from where that way has the pair fastestAgain frames on is begun on its thread;
    // frame() takes its way when the pair is there. Not while a second choice is on its way (the way may be dropped),
    // unless searchesWhileSecond().
    void searchAgain(const Observation& o) {
        againAt = -1;
        const Player& p = o.self();
        if (!fastestWays || fastestAgain <= 0 || fastestAhead <= 0 || (second && !searchesWhileSecond()) || target < 0 || p.phase != Phase::Control) return;
        if (step + fastestAgain + 1 >= path.frames()) return; // (the pair is put before a search could come)
        PairControl then = p.pair;
        for (int i = 0; i < fastestAgain; ++i) {
            stepPair(then, p.field, path.buttons[size_t(step + i)], o.timing, o.rules.speedPercent);
            if (then.locked) return;
        }
        planner.prepareFastest(p.field, then, placement(target), o.timing, o.rules.speedPercent);
        againAt = step + fastestAgain, againPair = p.nextPair, againTarget = target;
    }
    // holdSecond: may the pair, which the frame finds as `now`, go straight down in this frame? Yes
    // when the way to the first choice is then one frame shorter, and every placement that has a
    // way from here still has one from there.
    bool straight(const Observation& o, const PairControl& now) {
        const int left = path.frames() - step; // the frames of the way to the first choice from here
        if (left <= 1) return false;
        const PairControl next = stepped(o, now, BTN_DOWN);
        if (next.locked) return false;
        InputPath way;
        if (!wayTo(o, next, target, way) || way.frames() + 1 > left) return false;
        // (fastestWays: the way to the first choice may be over sooner than the straight way; going down must not end the
        //  landing animation later than the rest of that way does)
        if (planner.fastest) {
            const std::vector<uint8_t> rest(path.buttons.begin() + step, path.buttons.end());
            const int restEnd = FastestSearch::ends(o.self().field, now, rest, o.timing, o.rules.speedPercent);
            if (restEnd > 0 && FastestSearch::ends(o.self().field, next, way.buttons, o.timing, o.rules.speedPercent) + 1 > restEnd) return false;
        }
        // (spareFrame: nor so far down that a frame its sender is not called in takes the placement out of reach - held
        //  down to the last frame from which a way still led there, the next one, left out, sent the pair elsewhere)
        if (spareFrame && tapHold > 1) {
            InputPath sure;
            if (!sureWayTo(o, next, target, sure) || planner.lostBySkip(o.self().field, next, placement(target), o.timing, o.rules.speedPercent, sure)) return false;
        }
        return keepsWays(o, now, next, target);
    }
    // holdSecond, no choice to bring the pair to yet (late, or a choice made again): may the pair go straight down in this
    // frame? Yes when every placement that has a way from where it is still has one from there. (Held down at once, a
    // pair to go up over a column twelve high lay on the column beside it on the real game.)
    bool downKeepsWays(const Observation& o) {
        const Player& p = o.self();
        const PairControl now = p.phase == Phase::Control ? p.pair : appearingPair(p, o.rules, o.timing);
        const PairControl next = stepped(o, now, BTN_DOWN);
        return !next.locked && keepsWays(o, now, next, -1);
    }
    // Has every placement but `except` that has a way from `now` still one from `next`, and not one that goes back up? A
    // way from a frame further on that is longer by two frames or more does - over a column, by a kick or a quick turn:
    // the pair came down while the second choice was awaited and climbed again when it came. One a single frame longer
    // (the taps of a way fall on other frames from there) counts only when the pair rises on it and not on the way from
    // `now`.
    bool keepsWays(const Observation& o, const PairControl& now, const PairControl& next, int except) {
        int way = 0, after = 0;
        const unsigned legal = legalPlacements(o.self().field);
        for (int id = 0; id < PLACEMENTS; ++id) {
            bool up = false, upThen = false;
            if (!(legal >> id & 1) || id == except || !wayFrom(o, now, id, way, &up)) continue;
            if (!wayFrom(o, next, id, after, &upThen)) return false;
            if (holdDown == 0) continue;                 // (every way kept: enough)
            if (holdDown == 2 ? after > way : after > way + 1) return false;
            if (holdDown == 1 && after > way && upThen && !up) return false;
        }
        return true;
    }
    // Does the pair, played on `way` from `p`, go up in some frame (a kick from the floor, a quick turn)?
    bool rises(const Observation& o, PairControl p, const InputPath& way) {
        for (const uint8_t b : way.buttons) {
            if (p.locked) break;
            const int before = 2 * p.y + (p.upper ? 1 : 0);
            p = stepped(o, p, b);
            if (2 * p.y + (p.upper ? 1 : 0) > before) return true;
        }
        return false;
    }
    // keepsWays: does a way (wayTo) lead from `from` to placement `id`, and in how many frames? Worked out once
    // for the frame (Observation::frame), the own field, `from` and the planner's settings (tune() forgets them all).
    struct WaysFrom {
        int64_t frame = -1;
        GameField field;
        PairControl from{};
        bool low = false;
        unsigned asked = 0, has = 0, rise = 0; // (rise: the pair goes up at some frame of the way - rises())
        int frames[PLACEMENTS]{};
    };
    WaysFrom waysFrom[4];
    int waysNext = 0;
    bool wayFrom(const Observation& o, const PairControl& from, int id, int& frames, bool* up = nullptr) {
        WaysFrom* w = nullptr;
        for (WaysFrom& c : waysFrom)
            if (c.frame == o.frame && c.low == planner.keepLow && c.from == from && c.field == o.self().field) {
                w = &c;
                break;
            }
        if (!w) {
            w = &waysFrom[waysNext], waysNext = (waysNext + 1) % 4;
            w->frame = o.frame, w->field = o.self().field, w->from = from, w->low = planner.keepLow, w->asked = w->has = w->rise = 0;
        }
        if (!(w->asked >> id & 1)) {
            InputPath way;
            w->asked |= 1u << id;
            w->frames[id] = 0;
            if (wayTo(o, from, id, way)) {
                w->has |= 1u << id, w->frames[id] = way.frames();
                if (rises(o, from, way)) w->rise |= 1u << id;
            }
        }
        frames = w->frames[id];
        if (up) *up = (w->rise >> id & 1) != 0;
        return (w->has >> id & 1) != 0;
    }
    // searchAgain: the search of the way over soonest goes on while a second choice is on its way.
    bool searchesWhileSecond() const { return searchWhileSecond && holdKeepsWay; }
    // tapHold above 1: the frames the turn and the move of the buttons this agent gave last (`last`) have been held in a
    // row so far, counted from those buttons (tapsHeldFor).
    void countHeld() {
        constexpr uint8_t KIND[2] = {BTN_CW | BTN_CCW, BTN_LEFT | BTN_RIGHT};
        for (int k = 0; k < 2; ++k) heldRun[k] = (last & KIND[k]) ? heldRun[k] + 1 : 0, tapsHeldFor[k] = heldRun[k];
    }
    // handWait: may the pair, which the frame finds as `now`, be left alone in this frame? Yes when that does
    // not lock it and a way leads to the placement chosen from where it is then.
    bool alone(const Observation& o, const PairControl& now) {
        const PairControl next = stepped(o, now, 0);
        InputPath way;
        return !next.locked && wayTo(o, next, target, way);
    }
    bool made() const { return !job.valid() || job.wait_for(std::chrono::seconds(0)) == std::future_status::ready; }
    // The buttons that bring `pair` to the placement chosen (waits for it).
    // fallen: the pair has been in the air without a choice (Free).
    void route(const Observation& o, const PairControl& pair, bool fallen = false) {
        if (job.valid()) chosen = job.get(); // (kept: a second choice in the air stands on it)
        int id = chosen.id;
        targetSetup = chosen.setup;
        diagnosticSelected=diagnosticMain;
        thought = false;
        const Player& p = o.self();
        const unsigned legal = legalPlacements(p.field);
        if (!fallen) {
            // (a placement that is legal and has no path from where a pair appears: the one nearest to it that has, as
            //  reach() takes it)
            const int wanted = id;
            if (id >= 0) {
                int reached = id;
                if (planTarget(o, pair, reached, path, targetSetup && !second, legal)) {
                    // Once, at the actual spawn; never speed up a provisional
                    // route that must keep other placements available.
                    const bool committed = id == wanted && !fastestWays && holdDown == 1 && !idle && !second && !askInAir;
                    const Pair colors = committed ? currentColors(o) : Pair{};
#ifdef JURAIC_LV38_REPLAY_AUDIT
                    auditInput(o, pair, reached, legal, colors, path, committed);
#endif
                    if (fastQuietInput && committed && colors.axis && colors.child && fastInputAllowed(o, fastBusy)) {
                        int cuts = 0;
                        planner.accelerateQuiet(p.field, pair, reached, legal, colors, o.timing, o.rules, path,
                                                fastSoft, fastStates, &cuts);
                        fastStateLimitHits += cuts;
                    }
                    const int previous=target;target = reached;
                    diagnosticTarget(o,0,diagnosticSelected,previous);
                    return;
                }
                ++rerouted;
                targetSetup = false;
                const int best = nearest(o, pair, wanted, path);
                if (best >= 0) {
                    const int previous=target;target = best;
                    diagnosticTarget(o,0,diagnosticSelected,previous);
                }
            }
            return;
        }
        if (id >= 0) reach(o, pair, id, legal,1);
    }
    // The way of `pair` to placement `id` (one of `legal`). No way leads there any more - the choice came, or the pair was
    // put right, when it had fallen behind a column -: to the placement nearest to it; none: down where it is. Kept below
    // the 13th row (keepLow) and finding no way at all: any way.
    void reach(const Observation& o, const PairControl& pair, int id, unsigned legal,int diagnosticReason=3) {
        const Player& p = o.self();
        for (;;) {
            int reached = id;
            if (planTarget(o, pair, reached, path, targetSetup && !second, legal)) {
                const int previous=target;target = reached;
                diagnosticTarget(o,diagnosticReason,diagnosticSelected,previous);
                return;
            }
            const int best = nearest(o, pair, id, path);
            if (best >= 0) {
                const int previous=target;target = best, ++steered;
                targetSetup = false;
                diagnosticTarget(o,4,diagnosticSelected,previous);
                if (planner.keepLow && (legal >> id & 1)) {
                    // (for tools: a way through the 13th row leads there still)
                    InputPath high;
                    planner.keepLow = false;
                    steeredLow += wayTo(o, pair, id, high);
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
    // ones the one with the smaller number, and its way (`way`, left as it was when none) as a chosen placement has it
    // (planSure: the cells not sure of, the way over soonest). -1: none.
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
            if (wayTo(o, pair, other, there)) best = other, bestApart = apart;
        }
        if (best >= 0 && !sureWayTo(o, pair, best, way)) wayTo(o, pair, best, way);
        return best;
    }

    InputPlanner planner;
    bool lowWays = true; // keepLow holds for the ways of the pair in play
    InputPath path;
    int step = 0, target = -1;
    bool targetSetup = false;
    int idle = 0; // handWait: the frames the pair in the air is still left alone
    uint8_t last = 0;
    bool thought = false; // the pair that appears next has its placement, or a thread at work on it
    bool revising = false;
    GameField choiceField;
    int choiceIndex = -1, choiceCount = 0;
    Pair choicePairs[MAX_KNOWN]{};
    bool late = false;    // Free: the pair is in the air and its placement is not made yet
    bool unknownPair = false; // ... nor begun: its colours are not known (Observation: the own pairs known end before it)
    int lateLeft = 0;     // lateScript: the frames the pair still waits
    Choice chosen;       // the placement, when it was worked out at once
    Pair coming{};        // the pair the choice is for
    std::future<Choice> job; // ... or the thread
    Observation seen;     // what the thread looks at
    int shownAtThink = 0; // the pairs the screen had shown when that choice was begun
    bool asked = false;   // the screen has shown more since (once for a pair; retryInAir: once more in the air)
    bool askedFresh = false; // againFresh: a second choice was asked for with nothing new shown (once for a pair)
    bool secondBegun = false; // a second choice has been begun for the pair
    bool pending = false; // ... and again() is asked as soon as the first choice is made
    Observation seenAgain; // ... what the screen showed in that frame
    bool second = false;  // a second choice has been begun and has not come yet
    Choice secondChoice; // ... the placement, when it was worked out at once
    std::future<Choice> again_; // ... or its thread (valid, with second false: its pair has come to rest and it still works)
    int secondAt = 0;     // the frames after the pair appears at which a second choice worked out at once comes
    int secondFrames = 0; // the frames the pair has been in the air
    int secondMove = 0;   // the moves the player had made when it was begun
    int againAt = -1;     // fastestAgain: the step of the way at which the search begun for the pair in the air has it (-1: none)
    int againPair = -1, againTarget = -1; // ... the pair (Player::nextPair then) and the placement it was begun for
    PairControl born;     // the pair as it appears (in the frame it appears)
    int appearedPair = -1; // Player::nextPair of the pair in the air whose appearance was seen (or that was taken up)
    int heldRun[2] = {0, 0}; // countHeld: the frames the turn and the move have been held in a row
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
