#pragma once
#include "../play/session.h"

static void testPictureFastest() {
    GameField field{};
    constexpr int heights[] = {6, 8, 8, 5, 9, 4};
    for (int x = 0; x < W; ++x)
        for (int y = 0; y < heights[x]; ++y) field.f.set(x, y, Cell(1 + (x + y) % 4));
    const Timing timing = realGameTiming();
    PairControl from = spawnPair(timing);
    for (int i = 0; i < 2; ++i) stepPair(from, field, BTN_RIGHT | BTN_DOWN, timing);
    const Placement target = placement(14);
    ai::InputPlanner planner;
    planner.tapHold = 2, planner.spareFrame = true, planner.waitQuickLock = true, planner.heldFor[1] = 2;
    ai::InputPath plain, fast;
    check(planner.planSure(field, from, target, timing, 100, plain), "the ordinary held path reaches its target");
    planner.fastest = true, planner.fastestStates = 4000;
    check(planner.planSure(field, from, target, timing, 100, fast), "the picture Ex path reaches its target");
    check(planner.fasterWays == 1 && planner.framesSaved == 5 &&
          ai::FastestSearch::ends(field, from, plain.buttons, timing, 100) == 32 &&
          ai::FastestSearch::ends(field, from, fast.buttons, timing, 100) == 27, "the safe picture route ends five frames earlier");
    for (uint8_t kind : {uint8_t(BTN_LEFT | BTN_RIGHT), uint8_t(BTN_CW | BTN_CCW)}) {
        int run = 2;
        uint8_t before = from.held & kind;
        for (uint8_t button : fast.buttons) {
            uint8_t next = button & kind;
            if (next == before) ++run;
            else {
                check(run >= 2 && !(next && before), "each press and release lasts at least two frames");
                before = next, run = 1;
            }
        }
        check(!before || run >= 2, "the last tap is held through two frames");
    }
    for (size_t skip = 0; skip < fast.buttons.size(); ++skip) {
        auto pair = from;
        for (size_t i = 0; i < fast.buttons.size() && !pair.locked; ++i)
            stepPair(pair, field, i == skip ? pair.held : fast.buttons[i], timing);
        if (!pair.locked) stepPair(pair, field, 0, timing);
        check(pair.locked && pair.x == target.x && pair.dir == target.dir, "one missed update must preserve the placement");
    }
    planner.fastestStates = 0;
    ai::InputPath limited;
    check(planner.planSure(field, from, target, timing, 100, limited) && limited.buttons == plain.buttons,
          "an exhausted picture search retains the ordinary protected route");
}

// A real picture driver holds a tap through two frames. Exercise the move search
// with the same controls and with the round and the stored mid-air observations.
static void testImageTail() {
    testPictureFastest();
    struct Probe : ai::X3Agent {
        using X3Agent::X3Agent;
        using X3Agent::choiceBegins;
        using X3Agent::choose;
        using X3Agent::tailInput;
    };
    const char* rows[] = {"....5.", "....1.", "...315", "...413", ".44355", ".33445", "141133", "314134", "114514", "331554"};
    for (bool ex : {false, true}) {
        Rules rules;
        rules.cancel = ex;
        Round round;
        round.start(rules, 770005);
        auto o = ai::observe(round, 0);
        o.timing = realGameTiming();
        auto& self = o.player[0];
        self.phase = Phase::Control;
        self.nextPair = 1;
        self.colors = o.mine().pair(0);
        for (int row = 0; row < 10; ++row)
            for (int x = 0; x < W; ++x)
                if (rows[row][x] != '.') self.field.f.set(x, 9 - row, Cell(rows[row][x] - '0'));
        self.pair = spawnPair(o.timing);
        for (int i = 0; i < 3; ++i) stepPair(self.pair, self.field, 0, o.timing);
        int sixth = -1;
        for (int id = 0; id < PLACEMENTS; ++id)
            if (placement(id).x == 5 && placement(id).dir == UP) sixth = id;
        check(sixth >= 0, "fixture placement");
        const unsigned target = 1u << sixth;
        play::Settings menu;
        check(menu.aiLevel == 38 && menu.aiLevel1 == 38, "new settings start at Lv38");
        menu.rules.cancel = !ex;
        menu.aiEx = !ex;
        play::Versus session;
        session.seat(1, play::aiOfLevel(menu.aiLevel, 0), menu, false, ex);
        auto* actual = dynamic_cast<ai::X3Agent*>(session.agent[1].get());
        check(actual && actual->namedLv38Ex == ex && actual->exFlip == ex &&
              session.seatLog[1].name == (ex ? "lv38ex" : "lv38"), "the real-game menu must select the named AI independently of the internal game rules");
        const auto configured = ai::namedDefaults(ex ? "lv38ex" : "lv38");
        check(configured.lv38TwoWaveReselect == 1 && configured.lv38CutAhead == (ex ? 2 : 0) &&
              configured.lv38TailMin == (ex ? 560 : 920) && configured.lv38CutNear == (ex ? 200 : 100), "measured ordinary and Ex tails");
        ai::Params settings = ai::agentParams(ex ? "lv38ex" : "lv38", configured);
        settings.searchThreads = 1;
        Probe agent(settings);
        agent.fastestWays = ex;
        Driver::prepare(&agent);
        agent.choiceBegins();
        const auto never = [] { return false; };
        check(agent.tapHold == 2 && agent.spareFrame && agent.waitQuickLock && agent.tailInput.supported(), "image controls must support tail ranking");
        check(agent.tailInput.reachable(o, target, never) == target, "the held route to column six must survive a missed tap");
        check(agent.tailInput.reachable(o, target, [] { return true; }) == 0, "an expired search must not use a partial mask");
        auto locked = o;
        locked.player[0].phase = Phase::Next;
        locked.ownAir = true;
        locked.ownAirSeat = 0;
        locked.ownAirPair = self.pair;
        locked.ownAirPair.locked = true;
        locked.ownAirPair.x = 0;
        check(!agent.tailInput.reachable(locked, target, never), "a saved mid-air pair locked elsewhere must not use spawn reachability");
        ai::InputPlanner inputs;
        inputs.tapHold = 2, inputs.spareFrame = true, inputs.waitQuickLock = true;
        check(inputs.canReverse({2,2}, true) && !inputs.canReverse({2,3}, true) && !inputs.canReverse({2,2}),
              "a held orientation comparison must opt into path validation");
        GameField split;
        constexpr int heights[] = {2,8,2,9,0,5};
        for (int x=0; x<W; ++x)
            for (int y=0; y<heights[x]; ++y) split.f.set(x,y,Cell(1+(x+y)%4));
        const Timing timing = realGameTiming();
        auto from = spawnPair(timing);
        stepPair(from, split, BTN_DOWN, timing);
        const unsigned splitLegal = legalPlacements(split);
        for (bool fastest : {false,true}) {
            inputs.fastest = fastest;
            for (bool e2 : {false,true}) {
                const auto choose = [&](int& choice, Pair pair, unsigned mask, ai::InputPath& path) {
                    return e2 ? inputs.planFastestEquivalent(split,from,choice,mask,pair,timing,rules,path)
                              : inputs.planEquivalent(split,from,choice,mask,pair,timing,rules,path);
                };
                int id=6;
                ai::InputPath ordinary, result;
                check(inputs.planSure(split,from,placement(id),timing,100,ordinary), "ordinary picture orientation");
                const Pair pair{2,2};
                check(choose(id,pair,splitLegal,result) && id==17, "the protected orientation is selected");
                GameField a=split,b=split;
                place(a,placement(6),pair);
                place(b,placement(id),pair);
                check(a==b && ordinary.softDropRows==5 && result.softDropRows==5 &&
                      ai::FastestSearch::ends(split,from,ordinary.buttons,timing,100)==26 &&
                      ai::FastestSearch::ends(split,from,result.buttons,timing,100)==24,
                      "the identical field and score settle two frames earlier");
                for (const auto* path : {&ordinary,&result}) {
                    const auto target=placement(path==&ordinary ? 6 : 17);
                    for (int skip=0; skip<path->frames(); ++skip) {
                        auto moving=from;
                        for(int frame=0; frame<path->frames() && !moving.locked; ++frame)
                            stepPair(moving, split, frame==skip ? moving.held : path->buttons[size_t(frame)], timing);
                        if(!moving.locked) stepPair(moving,split,0,timing);
                        check(moving.locked && moving.x==target.x && moving.dir==target.dir,
                              "both picture paths tolerate a missed input at every frame");
                    }
                }
                id=6;
                check(choose(id,{2,3},splitLegal,result) && id==6, "different colours keep the original orientation");
                check(choose(id,pair,splitLegal & ~(1u<<17),result) && id==6, "an illegal reverse is excluded");
                inputs.unsure13=1;
                check(choose(id,pair,splitLegal,result) && id==6, "an unobserved row retains the original orientation");
                inputs.unsure13=0;
                id=17;
                check(choose(id,pair,splitLegal,result) && id==17, "a slower reverse is excluded");
            }
        }
        agent.unsure13 = 1;
        agent.choiceBegins();
        check(!agent.tailInput.supported(), "uncertain hidden rows must keep the original move");
        agent.unsure13 = 0;
        agent.choiceBegins();
        // A quiet opening takes the build branch even before the tail scoring window.
        auto opening = ai::observe(round, 0);
        opening.timing = o.timing;
        const unsigned legal = legalPlacements(opening.self().field);
        ai::calledLv38Parts = 0;
        const int picked = agent.choose(opening, opening.mine().pair(0), legal);
        check(picked >= 0 && ((legal >> picked) & 1u), "image search must return a legal move");
        check((ai::calledLv38Parts & (1u << unsigned(ai::Lv38Part::TailRank))) != 0, "picture-driver choices must enter the actual tail ranker");
    }
}
