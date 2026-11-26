    // Re-read an AnswerBuild only when the usual first move forces a clear on the
    // next visible pair and the ordinary beam's full offset relies on those frames.
    int correctKnownPrefix(const Observation& o, const Duel& afterChain, const BuildResult& beam,
                           unsigned legal, int chosen, int arriving, int rate, uint64_t seed,
                           const auto& expired) {
        const int me = o.me, him = 1 - me;
        const auto& forecast = afterChain.player[him];
        const int64_t confirm = forecast.spawnFrame - o.timing.nextFrames;
        // Before appearance, require the actual three pairs already shown.
        // Duel still schedules the first placement after the remaining wait.
        const auto& self = o.self();
        const bool ready = self.phase == Phase::Next && self.wait >= 1 && !o.busyLeft[me] &&
                           o.mine().known(self.nextPair + 2);
        if (!p.readMidgame || p.handWait || mine.pairCount != 3 || mine.knownCount < mine.pairCount ||
            !ready ||
            roomPairs(o.self().field) >= mine.pairCount || !chainShown(o.opponent()) ||
            o.self().incoming.confirmed || o.opponent().incoming.total() || o.self().scoring.allClear ||
            o.opponent().scoring.allClear || afterChain.state != RoundState::Playing ||
            forecast.phase != DuelPhase::Control || arriving <= o.rules.maxGarbagePerDrop ||
            confirm <= o.frame || beam.depth <= 2 || beam.cut || beam.told != p.queues()) {
            return -1;
        }
        if (expired()) return -1;

        const int64_t needed = int64_t(arriving) * rate - o.self().scoring.pool;
        if (needed <= 0 || !beam.enough(chosen, needed)) {
            return -1;
        }
        for (int64_t f = o.frame; f <= confirm; f = (f / FPS + 1) * FPS)
            if (o.rules.rateAt(f) != rate) return -1;
        if (o.rules.rateAt(confirm) != rate) return -1;

        const auto settled = [&](const Duel& d, int count) {
            const auto& own = d.player[me];
            return d.state == RoundState::Playing && own.phase == DuelPhase::Control &&
                   own.moves == o.self().moves + count && own.spawnFrame >= 0 &&
                   d.player[him].spawnFrame == forecast.spawnFrame && !own.scoring.allClear;
        };
        const auto exact = [&](const Plan& line) { return playOut(o, mine, &line, theirs, nullptr, p, seed, true); };
        const auto full = [](const DuelPlayer& own) { return !own.fallen && !own.incoming.total(); };

        // Protect any existing concrete answer, in particular an immediate full offset.
        // Only a visible, physically completed route can veto the change this way.
        int usualCost = INT32_MAX, partialCost[PLACEMENTS];
        std::fill(std::begin(partialCost), std::end(partialCost), INT32_MAX);
        for (const auto* group : {&mine.plans, &mine.tactical}) {
            for (const Plan& line : *group) {
                if (expired()) return -1;
                if (!line.count || line.count > mine.knownCount || !(legal >> line.id[0] & 1u)) continue;
                // An all-clear plan has its own tactical value and should be
                // left to the ordinary selection even if another plan offsets more.
                if (line.allClear) return -1;
                const auto d = exact(line);
                if (d.player[me].moves == o.self().moves + line.count && d.player[me].scoring.allClear) return -1;
                if (!settled(d, line.count)) continue;
                const auto& own = d.player[me];
                if (full(own)) {
                    return -1;
                }
                const int cost = own.fallen + own.incoming.total();
                const int first = line.id[0];
                if (first == chosen) usualCost = std::min(usualCost, cost);
                else if (line.fireDepth == line.count && line.chains >= 5 && own.chain.chains == line.chains)
                    partialCost[first] = std::min(partialCost[first], cost);
            }
        }
        if (usualCost == INT32_MAX) return -1;

        const int originalTail = beam.depth - 2;
        const uint64_t beamSeed = roundSeed * 1000 + uint64_t(o.self().moves);
        struct Prefix {
            GameField field;
            int64_t need;
            int depth, extra;
        };
        SpawnInputCache nextInputs;
        nextInputs.configure(o.timing, o.rules.speedPercent);
        const auto prefixes = [&](int firstId, bool requireDelay, std::vector<Prefix>& tails) {
            if (expired()) return false;
            Plan line;
            line.count = 1;
            line.id[0] = int8_t(firstId);
            const auto d = exact(line);
            const auto& start = d.player[me];
            if (!settled(d, 1) || full(start) || (requireDelay && (start.chain.chains || start.fallen))) {
                return false;
            }
            line.count = 2;
            int checked = 0;
            for (unsigned ids = legalPlacements(start.field); ids; ids &= ids - 1) {
                if (expired()) return false;
                line.id[1] = int8_t(std::countr_zero(ids));
                // A geometric placement alone does not prove that a pair can lock there.
                const auto target = placement(line.id[1]);
                if (!nextInputs.get(start.field, target).frames) continue;
                const auto continuation = exact(line);
                const auto& own = continuation.player[me];
                if (!settled(continuation, 2) || full(own)) return false;
                ++checked;
                const auto left = confirm - own.spawnFrame;
                const int remaining = left > 0 && !own.fallen ? p.movesIn(left, own.field.f.count(), 0) : 0;
                // The absolute spawn time already includes both locks, all chain
                // waves and any drop. Subtracting those durations again would
                // count the same frames twice. Cap by the original tail length.
                const int depth = std::min(originalTail, remaining);
                // A pair already in hand may ignite after confirmation, before
                // its own turn settles. Keep that generous extra layer as a veto.
                const int extra = std::min(originalTail, remaining + (left > 0 && !own.fallen ? 1 : 0));
                if (requireDelay) {
                    PlayerCore landed = start;
                    juraic::place(landed.field, target, mine.pair[1]);
                    Incoming rivalIncoming;
                    TurnResult waves;
                    resolveChain(landed, rivalIncoming, o.rules, o.timing, own.lockFrame, waves);
                    // With only the wave time removed, there must be a usable
                    // layer that disappears when those waves are restored.
                    const auto beforeWaves = left + waves.frames;
                    const int withoutWaves = beforeWaves > 0 && !own.fallen
                        ? std::min(originalTail, p.movesIn(beforeWaves, own.field.f.count(), 0) + 1) : 0;
                    if (!waves.chains || waves.chains != own.chain.chains || withoutWaves <= extra) {
                        return false;
                    }
                }
                const int64_t need = int64_t(own.incoming.total()) * rate - own.scoring.pool;
                if (!own.fallen && need > 0 && extra > 0) tails.push_back({own.field, need, depth, extra});
            }
            return checked > 0;
        };

        struct TailScores {
            GameField field;
            int depth;
            std::array<int, QUEUES_TOLD> peaks;
        };
        std::vector<TailScores> cached;
        const auto countSuccessful = [&](const Prefix& tail, int depth) {
            if (depth <= 0) return 0;
            if (expired()) return -1;
            // Every tail below uses the same remaining shown pairs, beam width and
            // queue seeds. The garbage threshold is applied after the search, so
            // equivalent fields reached with different pools share its scores.
            const auto count = [&](const TailScores& s) {
                return int(std::count_if(s.peaks.begin(), s.peaks.begin() + beam.told,
                                         [&](int points) { return points >= tail.need; }));
            };
            for (const TailScores& s : cached)
                if (s.depth == depth && s.field == tail.field) return count(s);

            Params q = p;
            q.beamDepth = depth;
            if (ownWidth > 0) q.beamWidth = ownWidth;
            q.beamThirdMax = thirdLimit(tail.field); // (of the field searched on)
            q.beamLastValues = 0;
            if (p.x3ExtendJoined) q.evKeyed = 0;
            // Leave the ordinary decision's width, clock accounting and RNG
            // intact, including when this additional search is interrupted.
            if (p.thinkBudget > 0 && budgetNow > 0)
                builder.until(began + std::chrono::microseconds(int64_t(budgetNow) * 750));
            else builder.untimed();
            searching.store(&builder);
            if (expired()) builder.until(Builder::Clock::now());
            const auto corrected = builder.search(tail.field, mine.pair + 2, mine.knownCount - 2,
                                                  o.rules, o.timing, q, beamSeed, p.searchThreads);
            searching.store(nullptr);
            if (expired() || corrected.cut || corrected.told != beam.told) return -1;
            TailScores scores{tail.field, depth, {}};
            for (int queue = 0; queue < corrected.told; ++queue)
                scores.peaks[size_t(queue)] = *std::max_element(std::begin(corrected.each[queue]), std::end(corrected.each[queue]));
            cached.push_back(scores);
            return count(scores);
        };

        std::vector<Prefix> tails;
        if (!prefixes(chosen, true, tails)) return -1;
        for (const Prefix& tail : tails)
            if (countSuccessful(tail, tail.extra) != 0) return -1;

        // The first pair is the only decision we change. Judge each second move
        // on all queues together, without picking a different second move per queue.
        std::vector<int> candidates;
        for (unsigned ids = legal; ids; ids &= ids - 1) {
            const int id = std::countr_zero(ids);
            if (id != chosen && partialCost[id] <= usualCost / 2) candidates.push_back(id);
        }
        std::stable_sort(candidates.begin(), candidates.end(), [&](int a, int b) { return partialCost[a] < partialCost[b]; });
        int best = -1, quality = 0;
        for (int id : candidates) {
            tails.clear();
            if (!prefixes(id, false, tails)) continue;
            std::sort(tails.begin(), tails.end(), [](const Prefix& a, const Prefix& b) { return a.need < b.need; });
            int support = 0;
            for (const Prefix& tail : tails) {
                const int passed = countSuccessful(tail, tail.depth);
                if (passed < 0) return -1;
                support = std::max(support, passed);
                if (support == beam.told) break;
            }
            if (support > quality) {
                best = id;
                quality = support;
            }
            // All later roots have equal or greater verified damage and cannot
            // exceed success on every queue. The winner and tie order are fixed.
            if (quality == beam.told) break;
        }
        if (expired()) return -1;
        return best;
    }
