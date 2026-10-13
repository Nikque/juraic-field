#pragma once
// Every weight and constant of the AI in one place. The defaults are here in
// the code; a text file of "name=value" lines replaces any of them (for
// tuning, and for rule settings that want other values). All values are
// integers: the search and the evaluation use no floating point.
// The numbers of rounds quoted with a value were measured when that value was
// chosen, with the other values as they were then.
#include <cstdint>
#include <istream>
#include <ostream>
#include <fstream>
#include <sstream>
#include <string>

namespace juraic::ai {

struct Params {
    // Trial drops (detect.h): the most pieces of one colour added on a column.
    int trialMaxAdd = 3;

    // The beam search for the main chain (build.h).
    int beamWidth = 250;      // nodes kept in a layer
    int beamDepth = 16;       // pairs placed, the known ones included
    // Eight queues of the table that holds pairs of one colour (beamFixedQueues 4). Measured against six fixed
    // sequences (beamQueues 6, beamFixedQueues 1): against the fixed Ama 1,087 of 1,440 rounds for 1,089 on the same
    // random numbers (250 lost rounds won, 252 won ones lost); against each other, a tsumo for each seat, 1,461 to
    // 1,419 of 2,880 rounds (50.7 %); alone on its field, 1,000 games, 759 reached the goal for 690, 80 lost on the way
    // for 157, the largest chain 93,034 points on average for 85,455. A move tries 18 % more placements. The levels
    // below the strongest keep the six fixed sequences.
    int beamQueues = 8;       // made-up sequences of pairs
    int beamShare = 1;      // 1: the layers of the known pairs are searched once for all queues (the same answer; 0: by every queue)
    int beamFixedQueues = 4;  // 4: rounds of eight pairs, six of two colours and two of one colour, each queue from another start, for any number of queues - meant for beamQueues=8 (build.h mixPair); 1: the six fixed sequences of four colours (Ama's); 0: random ones; 2: shuffled bags of every pair of two colours, the same for every seed (build.h bagPair); 3: the six pairs of two different colours in turn, each queue from another start (build.h turnPair)
    int beamEndPoints = 5000; // a chain of this many points ends a line of the search
    int beamSift = 0;         // lighter search: the fields of a layer are first valued without key pieces, and only the best beamSift x beamWidth of them in full (0 = every field in full)
    int beamThirdMax = 12;    // a placement that leaves the third column higher than this is not searched (12 = any height that does not lose)
    int beamLastValues = 1;   // 0: the fields of the last layer are not valued: nothing comes after them, and the chains found are the same. BuildResult::value is then that of the layer before (X3 asks for 0 unless it chooses by those values, tactics.h)

    // The value of a field while building (build.h buildValue).
    int evChain = 1000;     // per wave of the best chain a trial drop starts (two waves or more)
    // evKey, evValley and evHill are numbers of this AI (Ama's: evKey 200, evValley 100, evHill 100; evFireHeight 289
    // is Ama's, see below). Measured against the fixed Ama with evFireHeight 300, evValley 75, evHill 75 and evKey 250,
    // 225, 175, 145, 120, 100: 72.7, 74.4, 75.9, 77.0, 76.6, 74.3 % of 2,160, 2,160, 3,240, 1,080, 1,080 and 720
    // rounds, the four of Ama 74.1 % of 4,320; a parabola in log(evKey) has its top at 145 to 150 and lies within a
    // point of it from 110 to 191. 145 and 175 could not be told apart; 160, between them, was taken without a
    // measurement of its own.
    int evKey = 160;        // per piece that must be added to start it
    // evFireHeight 289 is Ama's number (licenses/Ama-NOTICE.txt). Against the fixed Ama (four sets of 120 rounds, the
    // same random numbers): 300 won 380 of 480, 290 378, 289 380, 288 380. The recorded attacks of the professional
    // (512 rounds) closed 43 fields with 300, 42 with 290, 38 with 289, 43 with 288 (kills 269, 287, 295, 284). None of
    // the four could be told from another; 289 was taken as the safe one. The valley and the hill are 75: against Ama
    // 100, 50 and 25 won 355, 363 and 356 of the same 480.
    int evFireHeight = 289; // per row of height of the place where it starts
    int evLink = 120;       // per pair of neighbours of one colour
    int evValley = 75;      // per row of depth of a column lower than both neighbours
    int evHill = 75;        // per row of height of a column higher than both neighbours
    int evCliff = 0;        // per row, squared, that two neighbouring columns differ by more than evCliffFrom (0 = off)
    int evCliffFrom = 2;    // (two rows are what one pair put upright fills)
    int evWaste = 1000;     // per wave cleared on the way
    int evThird = 0;        // per row, squared, of the third column above evThirdFrom (0 = off; in a match a high third column loses to a little garbage)
    int evThirdFrom = 8;
    // The base (a base that is high in the middle is hard to build on; high edges are fine, and it need not be flat).
    int evCentre = 300;     // per row a column is higher than its neighbour on the side of the nearer wall (0 = off)
    int evBase = 0;         // per piece that stands where one of the bases human players build on wants it (build.h baseCells; 0 = off)
    int evBaseForms = 0;    // only the first so many forms of the list (0 = all; 1 = the fold GTR alone)
    int evFire = 0;         // for a main chain the next pair can fire for certain; times the chance that a pair holds one of the colours of which one piece fires it (0 = off)
    int evStand = 0;        // off a field of evStandFrom cells or more on which no single piece fires a chain of evStandPoints or more (0 = off)
    int evStandFrom = 40;
    int evStandPoints = 1000;
    int evFireKeep = 85;    // "fires it": a chain with this many percent of the points of the largest
    // Time. The search counts pairs, but a match is played against the clock: a pair that is torn (one piece
    // falls further after the other has landed) takes 19 to 48 frames more (Timing::split), nearly half the
    // time of a pair. One wave of the chain (evChain) is about 4.7 pieces, 2.35 pairs, 110 frames of building.
    int evTearFrame = 7;    // per frame a torn pair takes longer, on the way (0 = off)
    // Key pieces (detect.h): the chain of a trial drop carried on with the piece that is missing where it stops.
    // One key piece at most for a chain.
    int evKeyed = 400;      // off the worth of such a chain for its key piece (0 = off: only chains that stand)
    int evKeyedWave = 1000; // per wave that the key piece adds to the chain (evChain: as much as a wave that stands)
    int keyNoneFrom = 0;    // from this many cells on the field no key piece is tried (0 = off)
    int keyRuns = 16;       // trials run again for one field, at most

    // Chance in the choice of the beam search (tactics.h pickBuild): a little chance from the agent's own seed among
    // placements that are nearly as good. Two of the same AI on the same tsumo would otherwise play the same moves in
    // the same frames and never finish. (b0: the name of an earlier AI, the first that chose so.)
    int b0Noise = 0;          // chance in the choice of the beam search, in thousandths of the best sum (0: the largest sum is taken, and sums exactly equal are told apart by chance). Against the fixed Ama 325 of 480 rounds with 0 for 333 with 5
    int b0OpeningNoise = 1000; // the same for the first b0OpeningMoves pairs of a round: two of the same AI part ways at once (1000: almost any placement)
    int b0OpeningMoves = 2;


    // V, the value of a position (value.h). The unit is about a tenth of a garbage piece.
    int vPotential = 10;     // per garbage piece of the best chain a trial drop starts
    int vQuick = 3;          // per garbage piece of the best chain one piece starts
    int vPotentialCap = 400; // garbage pieces beyond this count no more
    int vPiece = 3;           // per coloured piece on the field
    int vGarbage = 12;       // per garbage piece on the field (after what waits has fallen)
    int vWaiting = 30;       // per garbage piece that still waits after the first fall and would close the third column
    int vFirstDropOnly = 0;  // 1: only the first fall (thirty at most) can make a position lost; 0: everything that waits falls at once (measured better)
    int vDanger = 40;        // per row, squared, of the third column above vDangerFrom
    int vDangerFrom = 8;
    int vAllClear = 10;      // per garbage piece of an all clear bonus that waits

    // X3 chooses the placement of a pair this many frames before the pair appears (agent.h; 1: in the
    // frame in which it appears; 12 = Timing::nextFrames: as soon as the last pair has come to rest).
    // 0 with thinkBudget (the game's automatic setting): X3 sets it for every round from how fast the
    // machine searches, as late as its searches still get their full width (pace.h, X3Pace::chooseAhead).
    int thinkAhead = 1;
    // Threads of one beam search of X3: the queues are shared out among them. What is found does not depend on it.
    int searchThreads = 1;
    // The beam searches of one choice of X3 stop this many milliseconds after the choice was begun (0 = never:
    // they run to their depth). With it the choice depends on how fast the machine is, so the tools leave it
    // at 0; the game sets it for its AI seats, where a pair must not wait long for its choice.
    int thinkBudget = 0;
    int thinkRival = 50;    // ... and the search of the opponent's field, which comes first, after this many percent of that time
    int thinkFit = 1;       // with thinkBudget: 1 = a search that would not end in its time is made narrower (it keeps its depth), from what earlier searches took on this machine; 2 = the search of the opponent's field too; 0 = searches are only stopped
    int thinkWidthMin = 4;  // ... and not narrower than this
    int thinkWidthMax = 0;  // with thinkBudget and thinkFit: a search that would end well within its time is made wider, up to this (0 = never wider than beamWidth)
    int thinkPlans = 40;    // with thinkBudget: when the plans with three pairs take more than this many percent of the time, they are made with two (0 = always three)
    int thinkPonder = 1;    // with thinkBudget and thinkFit: 1 = the search of the own field for the next choice is begun while the pair of this one falls, and taken when the field is then the one foreseen (the same search with more time for it: ai/pace.h, X3Pace::airborne); 0 = every search in the time of its choice
    // A choice begun before the third pair is on the screen (Timing::nextShown: four frames before the
    // pair appears; or earlier, when the opponent's next pair appears) is made once more when it is,
    // and the pair goes to the second choice if it can still get there (agent.h, pace.h X3Pace::again).
    int thinkAgain = 1;       // 0 = the first choice stands
    int thinkAgainFrames = 6; // with thinkBudget: the second choice may come this many frames after the pair has appeared at the latest; it is not begun when its search would not get its width by then
    int thinkAgainWidth = 80; // with thinkBudget: the search of the own field of a second choice must be at least this many percent as wide as that of the first; otherwise the first choice stands
    int thinkLate = 0;        // without thinkBudget (the tools, for measuring): the second choice comes this many frames after the pair has appeared (0 = before it appears)
    // While a second choice is on its way: 0 = the pair sets out for the first choice at once (down and sideways
    // together); 1 = it goes straight down in the frames in which that loses nothing (PlacementAgent::holdSecond).
    // Measured, the second choice coming 3 and 6 frames after the pair: against X3 as it is, 240 rounds each, 51.8 %
    // and 52.5 % with 0, 43.6 % and 50.2 % with 1; small search, 40 rounds each, 22-9 and 21-8 with 0, 17-17 and 19-13
    // with 1; against the fixed Ama, 240 rounds each, 174 and 166 wins with 0, 181 and 169 with 1. The frames a pair is
    // in the hands differ by a tenth of a frame, and second choices no buttons led to are as rare with either
    // (0 to 1.3 % of them). Sideways first should make fewer mistakes, so the default is 0.
    int thinkHold = 0;
    // Another stream of X3's own random numbers (ties, the little chance in its choices, the first
    // pairs of a round): the same AI in every respect, playing other rounds on the same seeds. For
    // measuring how far one run of an AI lies from another run of the same AI ("twins"): a setting
    // that changes every round can only be told from the base by more than that.
    int x3Salt = 0;

    // The levels below the strongest. Both 0: X3 as it is, the same moves.
    // handWait, the speed of the hands: a pair is left alone this many frames after it has appeared, and falls
    // by itself, before the first button moves it. The wait ends earlier when one more frame of it would lock the
    // pair or take the placement chosen out of its reach (PlacementAgent::handWait). X3 knows that it is that
    // slow: in its reading its own pairs lock later (Duel::wait), and a move of its own takes that much longer
    // where its moves are counted (movesIn).
    int handWait = 0;
    // seeLate, the delay of the reaction: a choice reads the opponent's side - his field, his pair, his phase,
    // what waits above his field - as it was this many frames ago, and of what waits above the own field only
    // what he had sent by then. A chain of his that this late view shows, running or about to (the pair that
    // fires it has locked), is noticed: from then on the present is read, to the end of that chain; after it the
    // view stands still until it is seeLate frames old again (pace.h, X3Pace::sight).
    int seeLate = 0;

    // X3, moves by kind (tactics.h). Garbage counts in pieces; chains in the garbage they are worth.
    int x3Fall = 100;       // per garbage piece that falls on the own field or is left waiting above it (one row = 600: more than a chain of five is worth)
    int x3FallCurve = 1;    // 1: fewer than six count n * n / 6 instead of n, while the own third column is below eleven (0: every piece counts alike)
    int x3Close = 30000;    // with x3FallCurve and the own third column eleven high: off an answer that lets fewer than six fall, times their number / 6 (the chance that one closes the field)
    int x3Main = 10;        // per garbage piece the main chain left on the own field is worth
    int x3Surplus = 6;      // per garbage piece left waiting above the opponent's field
    int x3Used = 8;         // per coloured piece spent
    int x3MoveFrames = 50;  // frames of one move, to count the moves left before a chain ends (movesIn below): on a field of fewer than x3MoveMidFrom cells,
    int x3MoveFramesMid = 46;   // ... on one of x3MoveMidFrom cells or more (0: as x3MoveFrames),
    int x3MoveFramesLate = 40;  // ... on one of x3MoveLateFrom cells or more (0: as the stage before). A move is quicker on a fuller field (the pair has less far to fall).
                                // The measured lengths. Against the fixed Ama: 559 of 720 rounds for 551 with one length of 50 (x3MoveFramesMid 0, x3MoveFramesLate 0; the same random numbers; 205 rounds changed, 24 lost ones won, 16 won ones lost).
                                // Counted in 720 rounds against Ama (moves that clear nothing and take no garbage), by twelve cells from the empty field on:
                                // X3 51.0, 47.5, 47.4, 45.0, 41.5, 39.4, 40.5 frames, Ama 52.1, 49.8, 47.1, 43.9, 40.5, 38.9, 39.3: 50, 46 and 40
    int x3MoveMidFrom = 24;
    int x3MoveLateFrom = 48;
    // How a poke is judged: x3PokeGain 0 (75), xWildChance 1 (0), xWildEvery 8 (2), x3PokeNet 3 (0), x3PokeFrom 60 (0),
    // x3KeepMain 50 (85), and x3Crowd 750 (1000); in brackets the values of the AI that hardly pokes (agents.h
    // quietPoke). Measured together: against the fixed Ama 379 of 480 rounds (with the values in brackets 379 on the
    // same random numbers; without the last three 376), against X3 without the last three 252 to 221 of 480, the
    // recorded attacks of the professional closed 43 fields of 512, 265 kills, 6.6 pieces fell a round (with the values
    // in brackets: 43, 269, 5.9). x3Crowd=750: 1 of 240 rounds against Ama changes, 29 of the 512.
    int x3KeepMain = 50;    // a chain fired without need must leave this many percent of the main chain
    int x3MinPoke = 4;      // ... and send at least this much garbage (against the fixed Ama 4 for 6 changed 6 of 120 rounds, one each way)
    int x3PokeGain = 0;     // ... and gain this much of V against the opponent's best answer
    int x3Poke = 1;         // 0: never fire without need (only answers and winning moves)
    int x3ThirdMax = 9;     // the beam search of X3 does not put the third column higher than this (a little garbage would lose)
    int x3WinCheck = 1;     // 1: a "win" must also beat the chain the opponent can build in the time the own chain takes
    int x3WinBuild = 1;     // 1: in a round that is won already - below some placement the plans that only build win against whatever the opponent does - the pair goes where the beam search puts it among those placements (with 0 the AI piles its pairs up anywhere once it has won, which a person watching minds); 0: the first winning placement, without a search
    int x3Extend = 1;       // 1: against a running chain with moves to spare, build on and fire in time, even when an answer is at hand
    int x3Danger = 300;     // an answer loses this much per row, squared, of the third column above x3ThirdMax - 1
    int x3AnswerShape = 1;  // 1: when the best answer does not fire, the beam search chooses among the placements that let as little fall
    int x3ExtendCheck = 0;  // 1: building on against a running chain only when the beam search finds a chain in time that offsets what is coming
    int x3FullPercent = 0;  // a chain that can be fired now is fired when it has this many percent of the largest the beam search still finds (0 = never)
    int x3OutlastSettled = 1; // 1: that chain is searched from his field as it stands when he next holds a pair, as deep as he has moves (0: from the field of the observation, beamDepth at most)
    int x3OutlastAnswer = 30; // with x3OutlastSettled: a chain of his that offsets this many pieces or more counts as an answer that empties his field ...
    int x3OutlastLeft = 66;   // ... and then this many must be left of mine to bury him (eleven rows)
    int x3OutlastMean = 1;  // 1: the chain the opponent can build while mine runs is the average over the queues (0: the largest in any queue). 1 is the more realistic of the two: 558 of 720 rounds against the fixed Ama for 551 with 0 (13 lost ones won, 6 won ones lost)
    int x3OutlastScale = 100; // percent of that chain taken as his answer (100 = as the search of his field finds it). Counted against the fixed Ama: of 101 rounds lost in 480, 41 were lost by firing eight waves or more first and being answered larger; in 31 of them his field showed no such chain when X3 fired - he built it while X3's chain ran, to 132 % of X3's points on average.
    int x3AnswerOutlast = 0; // 1: an answer that spends the main chain (keeps less than x3KeepMain percent of it) is credited with what it leaves above the opponent (x3Surplus) only when he cannot answer that, as a "win" is read (0: always). Counted against the fixed Ama: of 42 rounds lost by firing first and being answered larger, 31 fired the main chain as an answer, 19 of them to six pieces or fewer.
    int x3FallTries = 1;    // draws of the columns on which less than a row of garbage falls, in the reading of an answer: the score of a plan that lets such garbage fall and the main chain it keeps are the average of the draws it survives, and each draw that closes the field takes x3Close / n off (1 = one draw, as before: whether the main chain was buried hung on it and changed from move to move)
    int x3BuildHold = 0;    // 1: a placement that builds and fires the main chain with the pair in hand (x3ChokeKeep percent of the largest chain the pairs on the screen fire, worth x3ChokeMin garbage or more) is left out while another placement lives - the reading of a "win" has said no to it (0: the beam search may take it; of 42 rounds lost to the fixed Ama by firing first, 7 were fired so). The game: aiBuildHold in the AI settings
    int x3PokeRead = 0;     // 1: a poke is judged by reading on: when the opponent fires his main chain at it, the own main chain, built on while his runs, must offset it (0: by the gain of V)
    int x3ValueMix = 0;     // thousandths of the value of the fields ahead added to the largest chain found, in the choice of the placement that builds (0 = off)
    int x3PokeUnanswered = 0; // 1: a poke is a chain that keeps the main chain and that no plan of the opponent answers while keeping his (0: by the gain of V, or x3PokeRead)
    int x3PokeKeep = 70;    // ... "keeps": this many percent of the points of the main chain
    int x3PokeFull = 66;    // ... from this many cells on the own field a poke is fired even when he can answer it
    int x3ByValue = 0;      // 1: the placement that builds is the one with the best fields ahead (BuildResult::value), not the one with the largest chain found
    int x3NoPlan = 1;       // 1: when every plan ends dead, the placement of the beam search (0: the first legal placement)
    int x3ShapeSlack = -1;  // with x3AnswerShape: the beam search chooses among the placements whose building plan scores within this of the best answer (-1: among those that let as little fall, whatever they score)
    int x3RowStrict = 0;    // 1: when a row of garbage or more falls in a plan, the main chain left is what a single piece fires, without key pieces (0: as on any field)
    int x3ChokeFrom = 0;    // from this many cells on the own field: the largest chain the pairs on the screen can fire is fired when this pair fires it, and is not given up for pairs that are not on the screen (0 = off)
    int x3ChokeLast = 0;  // ... 1: fired with this pair only when no later pair on the screen fires such a chain too (the last chance the screen shows); a later pair that fires it is waited for, the placements that keep it chosen from
    int x3ChokeHis = 0;   // ... n: fired first only when the opponent's main chain, as it stands on his field or as his pairs on the screen fire it, is worth less than n percent of it (0 = whatever he holds). The same for x3AnswerFire.
    int x3ChokeKeep = 85;   // ... "it": a chain of this many percent of its points
    int x3ChokeMin = 300;   // ... when that chain is worth this many garbage pieces or more
    int x3AllClear = 3;     // (the rule: by a chain of two always, by a chain of three on fourteen pieces or fewer)
                            // an all clear that a chain of this many waves or fewer gets with the pairs on the screen is taken, when nothing is on its way (0 = off)
    int x3ClearThree = 14;  // ... by a chain of three or more only when the field holds this many pieces or fewer with the pair that fires it (seen by the fourth move, twelve pieces; from the sixth move on it loses)
    int x3ExtendKeep = 0;   // building on against a running chain: only with placements whose building plan keeps this many percent of the largest main chain any keeps (0 = any placement)
    int x3ExtendKeepMin = 100; // ... when that main chain is worth this many garbage pieces or more
    int x3ClearNow = 2;     // an all clear that the pair in hand gets by a chain of this many waves or fewer is taken, whatever is on its way (0 = off)
    int x3ThirdStrict = 0;  // 1: the limit x3ThirdMax is lifted only when the third column is already above it (0: also when it is just as high)
    // An answer that lets garbage fall: off its score for every cell the own field then holds (garbage included)
    // above x3CrowdFrom and up to x3CrowdTo, x3Crowd each; the same for three more stretches (2, 3, 4), each with
    // its own from, to and price (0 = that stretch is off). Stretches that lie over one another add up. 169-71 against Ama with 54 / 1000, 161-79 without.
    int x3Crowd = 750;
    int x3CrowdCurve = 0;   // 1: when fewer than six pieces fall (and the third column is below eleven, as with x3FallCurve) the prices of these stretches count by their number, n / 6 (0: in full for a single piece - on 64 cells 10,000, more than the main chain is worth as kept; counted against the fixed Ama: of 42 rounds lost by firing first, 19 fired the main chain as an answer to six pieces or fewer)
    int x3CrowdFrom = 54;
    int x3CrowdTo = 78;
    int x3Crowd2 = 0;
    int x3CrowdFrom2 = 66;
    int x3CrowdTo2 = 78;
    int x3Crowd3 = 0;
    int x3CrowdFrom3 = 72;
    int x3CrowdTo3 = 78;
    int x3Crowd4 = 0;
    int x3CrowdFrom4 = 36;
    int x3CrowdTo4 = 78;
    int x3ExtendRoom = 60;  // building on against a running chain: not from this many cells on the own field on, when an answer that lets nothing fall can be fired now (0 = off)
    int x3ExtendJoined = 1; // 1: the search that builds on against a running chain values fields by the chains that stand, without key pieces (the key pieces go in when an answer is needed)
    // Building on against a running chain, by whether the answer will be large enough (tactics.h enoughChoices). The
    // search of the moves his chain leaves takes the placement with the largest sum over the queues, that is the mean
    // of the largest chain each queue fires in time. But there what is needed is known - the garbage on its way - and
    // what a chain is worth does not grow with its size: of the rounds of X3 against itself the answer won none when it
    // was below 90 percent of the first chain, 54 percent from 90 to 110 and 88 to 91 percent above. Far more than is
    // needed in three queues and too little in five has the larger sum, and just enough in all eight is the better
    // answer. Measured at the top level, 1,440 rounds each: 100 alone won 751 to 689 (52.2 %) against the AI without
    // it, a tsumo for each seat, and 1,085 for 1,087 against the fixed Ama; 110 with the guard below (x3Guard 10,
    // x3GuardNear 20, x3GuardMost 50) 742 to 698 (51.5 %) and 1,094 for 1,087. 110 alone was not measured. (Against Ama
    // both sides played with x3WinBuild 0.)
    int x3Enough = 110;     // n: that search chooses among the placements below which the most queues fire a chain worth n percent or more of the garbage on its way; the sums tell those apart as before, and when no queue fires one they choose alone (0 = off: the sums alone). The levels below the top play with 0 (level.h)
    // In the reading of the plans (ai/duel.h) a pair locks in the frame the Timing tables give for the fastest input. They
    // hold for the straight way. Past or beside a column eleven high or more, or with the axis piece coming to rest in the
    // thirteenth row, the pair locks up to 14 frames later in the game, and an answer read as "placed one frame before his
    // chain ends" took the garbage (seen: rounds lost with a chain of sixteen unfired on a field of 70 cells).
    int x3TallPaths = 10; // n: with a column of n pieces or more from the column the pair appears in to its placement, or next to them, the frame of the lock is that of the buttons InputPlanner finds (0 = always the tables; 10 = wherever the tables can be wrong). Against the fixed Ama 555 of 720 rounds for 552 with 0, 48 of the 720 rounds changed (9 won, 6 lost); what a choice costs in time was not measured reliably.
    // A pair that is in the air when the plans are made (the opponent's, mostly; the own one at a second choice): the frames
    // it has lain on the ground before its lock are taken off the landing waited for after the lock, as the game does
    // (game/player.h; a pair that lay 14 frames or more waits 2 frames, not 16). With 0 the reading waits the whole
    // landing after such a pair: his chain is read as starting up to 14 frames later than it does.
    int x3AirRest = 1;    // 1: as the game; 0: the whole landing
    // A small attack on a field that is nearly full. Counted in 720 rounds against the fixed Ama (small attack = five
    // waves or fewer sending six or more, X3 at 50 cells or more): X3 fired its main chain within six seconds 143 times
    // and lost the round 40 times; it fired a smaller chain and let nothing fall 51 times, lost 17; a smaller chain and
    // garbage fell 62 times, lost 48; nothing and garbage fell 11 times, lost 10. In the replays of a professional
    // player against X3: 14 times the main chain, lost 1; 20 times garbage fell, lost 20. His small chains came when
    // X3's field was full: 42 of 63 at 50 cells or more.
    int x3AnswerFire = 0; // garbage on its way and this many cells or more on the own field: the largest chain the pairs on the screen can fire is fired when the pair in hand fires it (x3ChokeKeep percent of it, x3ChokeMin garbage or more) and it lets nothing fall (0 = off: the answer that scores best)
    int x3PokeNet = 3;    // how the gain of a poke is counted against each reply of the opponent (the worst reply decides, x3PokeGain is the threshold). 0: V after the poke and his reply, less V now (in the reading X3 places nothing after the poke, and "now" has nobody firing: as counted, the plans that keep the main chain lose the round by it at the median, and X3 pokes about once in 1,600 decisions). 1: ... less V after the same reply without the poke, X3 placing nothing either (what the poke itself changes). 2: ... less the best V any plan of X3 reaches against the same reply (never above 0: the poke is fired when it is the best plan against every reply but for x3PokeGain, then 0 or below). 3: as 0, but a reply in which he gives up his main chain (x3PokeKeep) to fire at the poke, or after which the reading ends lost, does not count when X3, building on while his chain runs and firing second, offsets it (the reading of x3PokeRead).
    int x3PokeFrom = 60;   // a poke only from this many cells on the own field (0 = always). Against the fixed Ama the ways of judging that poke more lost where the first poke came on fewer than 60 cells, and not from 60 on; near a full field one mostly pokes.
    int x3PokeMainFrom = 0; // ... and only when the own main chain is worth this many points or more (0 = always)
    int x3PokeMost = 0;   // a poke sends at most this many garbage pieces (0 = any number). "Keeps the main chain" compares what a trial drop starts before and after: when that is small before, any chain passes, the main chain too (counted: one poke in ten sent 85 pieces or more).
    int x3PokeAt = 0;     // from this many cells on the opponent's field: a chain fired now that sends x3MinPoke or more and keeps x3PokeKeep percent of the main chain is fired, whatever he can answer (0 = off). The opponent to measure answers against.
    // Guard: nothing is on its way, but the opponent holds an attack his pair in hand fires. Where the pair goes is then also a question of what can be answered with.
    int x3Guard = 10;       // n: when a chain one of his first x3GuardPairs pairs fires leaves n garbage pieces or more above this field, the placement that builds is chosen among those whose plans answer that chain about as well as the best of them (0 = off). The levels below the top play with 0 (level.h)
    int x3GuardMost = 50;   // ... and at most this many pieces (0 = however many: his main chain too)
    int x3GuardPairs = 1;   // ... fired by his next pair (1), or by one of his next two (2) or three (3)
    int x3GuardSlack = 300; // ... within this much of the best answer, scored as an answer to garbage on its way is (x3Fall for a piece that falls)
    // The guard only where it costs next to nothing (tactics.h guardChoices). With x3GuardNear 0 the placement is
    // chosen among the guarded ones whatever the beam search found below them, and the main chain becomes two to three
    // tenths smaller (against the fixed Ama 144 of 240 rounds for 189). Many positions have several placements whose
    // sums are nearly the same: with x3GuardNear the guard chooses among those alone. Measured at the top level, 1,440
    // rounds each: x3Guard 10, x3GuardNear 20 and x3GuardMost 50 without x3Enough won 728 to 712 (50.6 %) against the
    // AI without them, a tsumo for each seat, and 1,085 for 1,087 against the fixed Ama - neither a gain nor a harm
    // that could be told; x3Guard 12 with x3GuardNear 10, 721 to 719 and 1,089.
    int x3GuardNear = 20;   // ... n: and only among the guarded placements whose sum lies within n thousandths of the largest sum of a placement that builds; when none does, the guard narrows nothing (0 = among all the guarded ones)
    int x3PathCheck = 1;    // 1: only placements the pair has a path to are chosen from (agent.h pathCheck; 0: every legal one, and a pair without a path goes to the next legal placement)
    // The placement that builds, with an eye on when its chains can be fired (tactics.h pickBuild; off: x3Soon=500 won
    // 683 of 1,440 rounds against the default, 47.4 %, a tsumo for each seat). The beam search takes the placement
    // below which the largest chain was found, whichever of its placements fires it: a chain sixteen pairs away counts
    // as one six pairs away. More search changed nothing in matches (width 300, depth 20, eight and sixteen queues: 49
    // to 50.5 % against the default), and against the fixed Ama an answer fired within eight seconds of his chain won
    // 427 of 441 rounds, one fired sixteen seconds or more after it 9 of 63. With x3Soon the sum of a placement is
    // mixed with the mean of what the search had found below it by every placement from x3SoonFrom to x3SoonTo
    // (BuildResult::sumBy): a placement after which a large chain can be fired early counts for more. Only among the
    // placements that fire no chain of beamEndPoints or more with the pair in hand, and only when the placement the
    // sums choose fires none either: whether the main chain is fired now is decided as before (a rule that fired it
    // earlier, x3FullPercent, lost heavily against an opponent who builds).
    int x3Soon = 0;         // thousandths of that mean in the mix (0 = off: the sums alone; 1000: the mean alone)
    int x3SoonFrom = 2;     // ... the first placement counted (1 = the pair in hand)
    int x3SoonTo = 0;       // ... and the last (0 = the depth of the search)
    // The plans kept of all the ways to put the pairs on the screen (gaze.h Gaze::keep; the two switches below are off
    // and not measured). Of the plans with one first placement that fire with the same pair an attack of the same size,
    // one is kept: the one that leaves the best field (sends most, from five rows on). What falls on the own field is
    // not looked at there, and "sent" is what is left after offsetting: against eight garbage pieces on their way a
    // chain that offsets them all, one that lets two fall and one that sends five more are the same size, zero rows.
    int x3KeepFall = 0;     // 1: of two such plans below five rows the one that lets less garbage fall on the own field (or wait above it) stays, then as before; 2: both stay, that one and the one the rule before keeps
    int x3KeepFine = 0;     // 1: finer sizes - no garbage sent, one to five pieces, every row from one to nine by itself, ten to nineteen rows, twenty or more (0: 0 to 4 rows each, 5 to 9, 10 to 19, 20 or more)

    // The reading of one exchange (exchange.h).
    int x2Depth = 3;       // pairs of the plans of both players (at most the pairs on the screen)
    int xWildEvery = 8;    // after his plan a player under attack gets the pieces a trial drop needs with every n-th pair (0 = never: he stands still)
    int xWildChance = 1;   // 1: in the gain of a poke his answer with a pair that is not on the screen counts with the chance 1 in xWildEvery - each reply is read with his next pair holding it and with none (what waits falls), weighed 1 to xWildEvery - 1 (0: he always holds it, xWildEvery - 1 pairs later, and nothing falls on him while he waits; xWildEvery 4, 8, 12 changed 3 to 5 of 240 rounds against the fixed Ama)
    int xWildFrames = 50;  // frames a pair that does not help takes him: on a field of fewer than x3MoveMidFrom cells,
    int xWildFramesMid = 46;   // ... on one of x3MoveMidFrom cells or more (0: as xWildFrames),
    int xWildFramesLate = 40;  // ... on one of x3MoveLateFrom cells or more (0: as the stage before). The same lengths as x3MoveFramesMid and x3MoveFramesLate (wildFrames below): the same kind of number, treated the same.
    int xWildSelf = 0;     // 1: the player who decides is given such pairs too (0: he must answer with the pairs on the screen)
    int xWildExact = 1;    // 1: such an answer is the smallest chain that offsets everything waiting (0: the largest chain)

    // The three numbers of the beam search as the code uses them: never fewer than one.
    int queues() const { return beamQueues < 1 ? 1 : beamQueues; }
    // The frames that `pairs` pairs that do not help take on a field of `cells` cells (each adds a pair to it).
    int64_t wildFrames(int pairs, int cells) const {
        const int mid = xWildFramesMid > 0 ? xWildFramesMid : xWildFrames, late = xWildFramesLate > 0 ? xWildFramesLate : mid;
        int64_t frames = 0;
        for (int i = 0; i < pairs; ++i, cells += 2) frames += cells >= x3MoveLateFrom ? late : cells >= x3MoveMidFrom ? mid : xWildFrames;
        return frames;
    }
    // The moves that fit into `frames` frames on a field of `cells` cells: a move takes x3MoveFrames,
    // x3MoveFramesMid or x3MoveFramesLate frames by the cells on the field, and every move adds a pair to it.
    // extra: frames every one of these moves takes longer (the own moves of an X3 with slow hands: handWait).
    int movesIn(int64_t frames, int cells, int extra = 0) const {
        const int early = (x3MoveFrames > 0 ? x3MoveFrames : 1) + extra, mid = x3MoveFramesMid > 0 ? x3MoveFramesMid + extra : early,
                  late = x3MoveFramesLate > 0 ? x3MoveFramesLate + extra : mid;
        if (mid == early && late == early) return int(frames / early);
        for (int moves = 0;; ++moves, cells += 2) {
            if (cells >= x3MoveLateFrom && cells >= x3MoveMidFrom) return moves + int(frames / late);
            const int one = cells >= x3MoveLateFrom ? late : cells >= x3MoveMidFrom ? mid : early;
            if (frames < one) return moves;
            frames -= one;
        }
    }
    int depth() const { return beamDepth < 1 ? 1 : beamDepth; }
    int width() const { return beamWidth < 1 ? 1 : beamWidth; }
};

struct ParamItem {
    const char* name;
    int Params::*member;
};
// One line per member of Params. A member without a line cannot be read from a
// file (tests/ai_tests.cpp counts them).
inline constexpr ParamItem PARAM_ITEMS[] = {
    {"trialMaxAdd", &Params::trialMaxAdd},
    {"beamWidth", &Params::beamWidth},
    {"beamDepth", &Params::beamDepth},
    {"beamQueues", &Params::beamQueues},
    {"beamShare", &Params::beamShare},
    {"beamFixedQueues", &Params::beamFixedQueues},
    {"beamEndPoints", &Params::beamEndPoints},
    {"beamSift", &Params::beamSift},
    {"beamThirdMax", &Params::beamThirdMax},
    {"beamLastValues", &Params::beamLastValues},
    {"evChain", &Params::evChain},
    {"evKey", &Params::evKey},
    {"evFireHeight", &Params::evFireHeight},
    {"evLink", &Params::evLink},
    {"evValley", &Params::evValley},
    {"evHill", &Params::evHill},
    {"evCliff", &Params::evCliff},
    {"evCliffFrom", &Params::evCliffFrom},
    {"evWaste", &Params::evWaste},
    {"evThird", &Params::evThird},
    {"evThirdFrom", &Params::evThirdFrom},
    {"evCentre", &Params::evCentre},
    {"evBase", &Params::evBase},
    {"evBaseForms", &Params::evBaseForms},
    {"evFire", &Params::evFire},
    {"evFireKeep", &Params::evFireKeep},
    {"evStand", &Params::evStand},
    {"evStandFrom", &Params::evStandFrom},
    {"evStandPoints", &Params::evStandPoints},
    {"evTearFrame", &Params::evTearFrame},
    {"evKeyed", &Params::evKeyed},
    {"evKeyedWave", &Params::evKeyedWave},
    {"keyNoneFrom", &Params::keyNoneFrom},
    {"keyRuns", &Params::keyRuns},
    {"b0Noise", &Params::b0Noise},
    {"b0OpeningNoise", &Params::b0OpeningNoise},
    {"b0OpeningMoves", &Params::b0OpeningMoves},
    {"vPotential", &Params::vPotential},
    {"vQuick", &Params::vQuick},
    {"vPotentialCap", &Params::vPotentialCap},
    {"vPiece", &Params::vPiece},
    {"vGarbage", &Params::vGarbage},
    {"vWaiting", &Params::vWaiting},
    {"vFirstDropOnly", &Params::vFirstDropOnly},
    {"vDanger", &Params::vDanger},
    {"vDangerFrom", &Params::vDangerFrom},
    {"vAllClear", &Params::vAllClear},
    {"thinkAhead", &Params::thinkAhead},
    {"searchThreads", &Params::searchThreads},
    {"thinkBudget", &Params::thinkBudget},
    {"thinkRival", &Params::thinkRival},
    {"thinkFit", &Params::thinkFit},
    {"thinkWidthMin", &Params::thinkWidthMin},
    {"thinkWidthMax", &Params::thinkWidthMax},
    {"thinkPlans", &Params::thinkPlans},
    {"thinkPonder", &Params::thinkPonder},
    {"thinkAgain", &Params::thinkAgain},
    {"thinkAgainFrames", &Params::thinkAgainFrames},
    {"thinkAgainWidth", &Params::thinkAgainWidth},
    {"thinkLate", &Params::thinkLate},
    {"thinkHold", &Params::thinkHold},
    {"x3Salt", &Params::x3Salt},
    {"handWait", &Params::handWait},
    {"seeLate", &Params::seeLate},
    {"x3Fall", &Params::x3Fall},
    {"x3FallCurve", &Params::x3FallCurve},
    {"x3Close", &Params::x3Close},
    {"x3Main", &Params::x3Main},
    {"x3Surplus", &Params::x3Surplus},
    {"x3Used", &Params::x3Used},
    {"x3MoveFrames", &Params::x3MoveFrames},
    {"x3MoveFramesMid", &Params::x3MoveFramesMid},
    {"x3MoveFramesLate", &Params::x3MoveFramesLate},
    {"x3MoveMidFrom", &Params::x3MoveMidFrom},
    {"x3MoveLateFrom", &Params::x3MoveLateFrom},
    {"x3KeepMain", &Params::x3KeepMain},
    {"x3MinPoke", &Params::x3MinPoke},
    {"x3PokeGain", &Params::x3PokeGain},
    {"x3Poke", &Params::x3Poke},
    {"x3ThirdMax", &Params::x3ThirdMax},
    {"x3WinCheck", &Params::x3WinCheck},
    {"x3WinBuild", &Params::x3WinBuild},
    {"x3Extend", &Params::x3Extend},
    {"x3Danger", &Params::x3Danger},
    {"x3AnswerShape", &Params::x3AnswerShape},
    {"x3ExtendCheck", &Params::x3ExtendCheck},
    {"x3FullPercent", &Params::x3FullPercent},
    {"x3OutlastMean", &Params::x3OutlastMean},
    {"x3OutlastScale", &Params::x3OutlastScale},
    {"x3AnswerOutlast", &Params::x3AnswerOutlast},
    {"x3FallTries", &Params::x3FallTries},
    {"x3BuildHold", &Params::x3BuildHold},
    {"x3OutlastSettled", &Params::x3OutlastSettled},
    {"x3OutlastAnswer", &Params::x3OutlastAnswer},
    {"x3OutlastLeft", &Params::x3OutlastLeft},
    {"x3PokeUnanswered", &Params::x3PokeUnanswered},
    {"x3PokeKeep", &Params::x3PokeKeep},
    {"x3PokeFull", &Params::x3PokeFull},
    {"x3ByValue", &Params::x3ByValue},
    {"x3NoPlan", &Params::x3NoPlan},
    {"x3ShapeSlack", &Params::x3ShapeSlack},
    {"x3RowStrict", &Params::x3RowStrict},
    {"x3ChokeFrom", &Params::x3ChokeFrom},
    {"x3ChokeKeep", &Params::x3ChokeKeep},
    {"x3ChokeHis", &Params::x3ChokeHis},
    {"x3ChokeLast", &Params::x3ChokeLast},
    {"x3ChokeMin", &Params::x3ChokeMin},
    {"x3AllClear", &Params::x3AllClear},
    {"x3ClearThree", &Params::x3ClearThree},
    {"x3ExtendKeep", &Params::x3ExtendKeep},
    {"x3ExtendKeepMin", &Params::x3ExtendKeepMin},
    {"x3ClearNow", &Params::x3ClearNow},
    {"x3ThirdStrict", &Params::x3ThirdStrict},
    {"x3PathCheck", &Params::x3PathCheck},
    {"x3Soon", &Params::x3Soon},
    {"x3SoonFrom", &Params::x3SoonFrom},
    {"x3SoonTo", &Params::x3SoonTo},
    {"x3KeepFall", &Params::x3KeepFall},
    {"x3KeepFine", &Params::x3KeepFine},
    {"x3Crowd", &Params::x3Crowd},
    {"x3CrowdCurve", &Params::x3CrowdCurve},
    {"x3CrowdFrom", &Params::x3CrowdFrom},
    {"x3CrowdTo", &Params::x3CrowdTo},
    {"x3Crowd2", &Params::x3Crowd2},
    {"x3CrowdFrom2", &Params::x3CrowdFrom2},
    {"x3CrowdTo2", &Params::x3CrowdTo2},
    {"x3Crowd3", &Params::x3Crowd3},
    {"x3CrowdFrom3", &Params::x3CrowdFrom3},
    {"x3CrowdTo3", &Params::x3CrowdTo3},
    {"x3Crowd4", &Params::x3Crowd4},
    {"x3CrowdFrom4", &Params::x3CrowdFrom4},
    {"x3CrowdTo4", &Params::x3CrowdTo4},
    {"x3ExtendRoom", &Params::x3ExtendRoom},
    {"x3ExtendJoined", &Params::x3ExtendJoined},
    {"x3Enough", &Params::x3Enough},
    {"x3TallPaths", &Params::x3TallPaths},
    {"x3AirRest", &Params::x3AirRest},
    {"x3AnswerFire", &Params::x3AnswerFire},
    {"x3PokeAt", &Params::x3PokeAt},
    {"x3PokeNet", &Params::x3PokeNet},
    {"x3PokeFrom", &Params::x3PokeFrom},
    {"x3PokeMainFrom", &Params::x3PokeMainFrom},
    {"x3PokeMost", &Params::x3PokeMost},
    {"x3Guard", &Params::x3Guard},
    {"x3GuardMost", &Params::x3GuardMost},
    {"x3GuardPairs", &Params::x3GuardPairs},
    {"x3GuardSlack", &Params::x3GuardSlack},
    {"x3GuardNear", &Params::x3GuardNear},
    {"x3ValueMix", &Params::x3ValueMix},
    {"x3PokeRead", &Params::x3PokeRead},
    {"x2Depth", &Params::x2Depth},
    {"xWildEvery", &Params::xWildEvery},
    {"xWildChance", &Params::xWildChance},
    {"xWildFrames", &Params::xWildFrames},
    {"xWildFramesMid", &Params::xWildFramesMid},
    {"xWildFramesLate", &Params::xWildFramesLate},
    {"xWildSelf", &Params::xWildSelf},
    {"xWildExact", &Params::xWildExact},
};

inline void saveParams(std::ostream& out, const Params& p) {
    for (const ParamItem& item : PARAM_ITEMS) out << item.name << '=' << p.*item.member << '\n';
}

// Reads "name=value" lines into p; names that are not in the text keep their
// value. Empty lines and lines that start with # are skipped. A name that is
// not known or a value that is not a whole number is an error: `error` says
// which line, and p is left as it was.
inline bool loadParams(std::istream& in, Params& p, std::string& error) {
    Params next = p;
    std::string line;
    for (int number = 1; std::getline(in, line); ++number) {
        while (!line.empty() && (line.back() == '\r' || line.back() == ' ' || line.back() == '\t')) line.pop_back();
        if (line.empty() || line[0] == '#') continue;
        const size_t eq = line.find('=');
        const ParamItem* found = nullptr;
        for (const ParamItem& item : PARAM_ITEMS)
            if (eq != std::string::npos && line.compare(0, eq, item.name) == 0) found = &item;
        size_t used = 0;
        int value = 0;
        bool ok = found != nullptr;
        if (ok) {
            try {
                value = std::stoi(line.substr(eq + 1), &used);
            } catch (...) {
                ok = false;
            }
            ok = ok && used == line.size() - eq - 1;
        }
        if (!ok) {
            error = "line " + std::to_string(number) + ": " + line;
            return false;
        }
        next.*found->member = value;
    }
    p = next;
    return true;
}

// For the tools' arguments --param and --params: `text` is one name=value pair or the name of a file of
// such lines; what it holds is added to `lines`, which loadParams reads afterwards. False when the file
// cannot be read.
inline bool addParamText(std::string& lines, const std::string& text) {
    if (text.find('=') != std::string::npos) return lines += text + '\n', true;
    std::ifstream file(text);
    std::stringstream all;
    all << file.rdbuf();
    if (!file) return false;
    lines += all.str() + '\n';
    return true;
}

} // namespace juraic::ai
