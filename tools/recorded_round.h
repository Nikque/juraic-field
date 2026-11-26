// Included by ai_match.cpp after playRound() and the legacy checksum mixer.
// The replay contains the settings, first tsumo and buttons. A separate record
// supplies the second tsumo for games with distinct left and right streams.

bool readNonnegative(const std::string& text, int& value) {
    if (text.empty()) return false;
    const auto reply = std::from_chars(text.data(), text.data() + text.size(), value);
    return reply.ec == std::errc{} && reply.ptr == text.data() + text.size() && value >= 0;
}

bool saveClosedText(const std::filesystem::path& path, const std::string& data) {
    auto temporary = path;
    temporary += ".partial";
    if (std::filesystem::exists(path) || std::filesystem::exists(temporary)) return false;
    std::ofstream out(temporary, std::ios::binary);
    out << data;
    out.close();
    if (!out) return false;
    std::filesystem::rename(temporary, path);
    return true;
}

bool verifyRecordedRound(const std::filesystem::path& tapePath, const std::filesystem::path& tsumoPath,
                         const std::string& terminal) {
    play::Replay replay;
    if (!play::loadReplay(tapePath, replay) || replay.players != 2 || replay.rounds.size() != 1) return false;
    int own = 0;
    uint64_t seed1 = 0;
    std::ifstream tsumo(tsumoPath);
    if (!(tsumo >> own >> seed1) || (own != 0 && own != 1)) return false;
    const auto& log = replay.rounds.front();
    Round round;
    round.start(replay.settings.rules, log.seed, 2, replay.timing());
    if (own) round.splitTsumo(seed1);
    for (uint16_t input : log.frames) {
        if (round.state != RoundState::Playing) return false;
        const uint8_t buttons[] = {uint8_t(input & 255), uint8_t(input >> 8)};
        PlayerEvents events[2];
        round.step(buttons, events);
    }
    return round.frame == int64_t(log.frames.size()) && play::roundSummary(round) == terminal;
}

bool runRecordedRound(const Options& opt, const RoundRequest& request) {
    using namespace std::filesystem;
    try {
        if (request.attempt.empty() || request.attempt.find_first_not_of("abcdefghijklmnopqrstuvwxyz0123456789-") != std::string::npos ||
            !is_directory(request.directory) || !is_regular_file(request.ledger) || !is_empty(request.directory)) {
            std::cerr << "An empty attempt directory and an existing lane ledger are required\n";
            return false;
        }
        std::vector<ai::lv38_fire_or_lose::Decision> fireDecisions;
        std::vector<ai::X3Agent::TakeSmallNote> takeSmallDecisions;
        std::vector<ai::X3Agent::PartNote> partDecisions;
        std::vector<ai::X3Agent::CutRankRecord> cutRanks;
        std::vector<ai::X3Agent::CutAnswerRecord> cutAnswers;
        std::vector<ai::X3Agent::CutDecisionRecord> cutDecisions;
        std::array<std::vector<ai::X3Agent::PartNote>, 2> audited;
        auto a = ai::makeConfiguredAgent(opt.a, opt.paramsA), b = ai::makeConfiguredAgent(opt.b, opt.paramsB);
        if (!a || !b) return false;
        const auto config = ai::configuredParams(opt.a, opt.paramsA);
        if (ai::configuredParams(opt.a, opt.paramsA).lv38FireOrLose > 0) {
            auto* candidate = dynamic_cast<ai::X3Agent*>(a.get());
            if (!candidate || opt.paramsA.thinkBudget > 0) return false;
            candidate->fireOrLoseLog = &fireDecisions;
        }
        if (ai::configuredParams(opt.a, opt.paramsA).lv38TakeSmall > 0) {
            auto* candidate = dynamic_cast<ai::X3Agent*>(a.get());
            if (!candidate || opt.paramsA.thinkBudget > 0) return false;
            candidate->takeSmallLog = &takeSmallDecisions;
        }
        if (ai::configuredParams(opt.a, opt.paramsA).lv38Midgame) {
            auto* candidate = dynamic_cast<ai::X3Agent*>(a.get());
            if (!candidate || opt.paramsA.thinkBudget > 0) return false;
            candidate->partLog = &partDecisions;
            candidate->cutRanks = &cutRanks;
            candidate->cutAnswers = &cutAnswers;
            candidate->cutDecisions = &cutDecisions;
        }
        if (auto* p = dynamic_cast<ai::PlacementAgent*>(a.get())) p->fastestWays = opt.exA;
        if (auto* p = dynamic_cast<ai::PlacementAgent*>(b.get())) p->fastestWays = opt.exB;

        Rng rng(opt.seed, MATCH_STREAM + uint64_t(request.pair));
        const uint64_t seed0 = rng.next() % 1000000000ull;
        const uint64_t seed1 = Rng(opt.seed, APART_STREAM + uint64_t(request.pair)).next() % 1000000000ull;
        if (request.aSeat == 1) rng.next(), rng.next();
        const uint64_t seedA = rng.next(), drawnB = rng.next();
        const uint64_t seedB = opt.sameSeeds ? seedA : drawnB;
        ai::Agent* agents[] = {request.aSeat ? b.get() : a.get(), request.aSeat ? a.get() : b.get()};
        const uint64_t agentSeeds[] = {request.aSeat ? seedB : seedA, request.aSeat ? seedA : seedB};
        const std::string& nameA = opt.a;
        const std::string& nameB = opt.b;
        for (int seat = 0; seat < 2; ++seat) {
            auto* x3 = dynamic_cast<ai::X3Agent*>(agents[seat]);
            if (!x3) continue;
            const int level = ai::levelOf(seat == request.aSeat ? nameA : nameB);
            if (level >= 37) x3->lv37PartLog = &audited[seat];
            if (level == 36) x3->legacyPokeLog = &audited[seat];
        }
        if (!saveClosedText(request.directory / "started.txt", request.attempt + '\n')) return false;

        play::Replay replay{opt.settings, 2, play::TIMING_VERSION, {}};
        replay.rounds.emplace_back();
        const auto result = playRound(opt.settings.rules, seed0, agents, agentSeeds, opt.maxFrames, &replay.rounds.back(),
                                      opt.ownTsumo ? &seed1 : nullptr, opt.traceCrush, opt.traceAnswers);
        const auto tape = request.directory / "inputs.jfreplay";
        const auto temporaryTape = request.directory / "inputs.partial";
        if (!play::saveReplay(temporaryTape, replay)) return false;
        rename(temporaryTape, tape);
        const auto tsumo = request.directory / "tsumo.txt";
        const auto terminal = request.directory / "terminal.txt";
        if (!saveClosedText(tsumo, std::to_string(int(opt.ownTsumo)) + ' ' + std::to_string(seed1) + '\n') ||
            !saveClosedText(terminal, result.summary + '\n') || !verifyRecordedRound(tape, tsumo, result.summary)) {
            std::cerr << "The recorded buttons did not reproduce the terminal state\n";
            return false;
        }
        std::ostringstream fireLog;
        fireLog << "frame,move,ordinary,selected,colours,points,waves,main\n";
        for (const auto& d : fireDecisions)
            fireLog << d.frame << ',' << d.move << ',' << d.ordinary << ',' << d.selected << ',' << d.colours << ','
                    << d.points << ',' << d.waves << ',' << d.main << '\n';
        if (!saveClosedText(request.directory / "fire_or_lose.csv", fireLog.str())) return false;
        std::ostringstream takeSmallLog;
        takeSmallLog << "frame,move,phase,ordinary,selected,incoming,colours,turns,expected\n";
        for (const auto& d : takeSmallDecisions)
            takeSmallLog << d.frame << ',' << d.move << ',' << d.phase << ',' << d.ordinary << ',' << d.selected << ','
                         << d.incoming << ',' << d.colours << ',' << d.turns << ',' << d.expected << '\n';
        if (!saveClosedText(request.directory / "take_small.csv", takeSmallLog.str())) return false;

        std::ostringstream partLog;
        partLog << "frame,move,phase,incoming,arriving,colours,part,before,after,kind,waves\n";
        for (const auto& d : partDecisions)
            partLog << d.frame << ',' << d.move << ',' << d.phase << ',' << d.incoming << ','
                    << d.arriving << ',' << d.colours << ',' << d.part << ',' << d.before << ','
                    << d.after << ',' << d.kind << ',' << d.waves << '\n';
        if (!saveClosedText(request.directory / "lv38_parts.csv", partLog.str())) return false;

        std::ostringstream times, holdings, ranks, answers;
        times << "frame,move,nanoseconds\n";
        holdings << "frame,move,colours,main,tail_points,tail_waves,tail_560_points,tail_560_waves,head_points,head_waves\n";
        for (const auto& d : cutDecisions) {
            times << d.frame << ',' << d.move << ',' << d.nanoseconds << '\n';
            if (!d.quiet) continue;
            const auto cut = ai::lv38_cut_build::measure(d.field, opt.settings.rules, replay.timing(), config);
            holdings << d.frame << ',' << d.move << ',' << d.colours << ',' << cut.main << ','
                     << cut.tail.points << ',' << cut.tail.waves << ',' << cut.lowerTail.points << ',' << cut.lowerTail.waves
                     << ',' << (cut.head.valid() ? cut.head.fire.points : 0) << ','
                     << (cut.head.valid() ? cut.head.fire.chains : 0) << '\n';
        }
        ranks << "frame,move,ordinary,selected,depth,queues,preserved\n";
        for (const auto& r : cutRanks)
            ranks << r.frame << ',' << r.move << ',' << r.ordinary << ',' << r.selected << ',' << r.depth
                  << ',' << r.queues << ',' << r.preserved << '\n';
        answers << "frame,move,selected,depth,head,key_column,key_row,key_colour,points,reserved_points\n";
        for (const auto& answer : cutAnswers) {
            const auto& n = answer.note;
            answers << answer.frame << ',' << answer.move << ',' << n.selected << ',' << n.depth << ',' << n.head << ','
                    << n.keyColumn << ',' << n.keyRow << ',' << n.keyColor << ',' << n.points << ',' << n.reservePoints << '\n';
        }
        if (!saveClosedText(request.directory / "decision_time.csv", times.str()) ||
            !saveClosedText(request.directory / "cut_holdings.csv", holdings.str()) ||
            !saveClosedText(request.directory / "cut_ranks.csv", ranks.str()) ||
            !saveClosedText(request.directory / "cut_answers.csv", answers.str())) return false;

        std::ostringstream calls;
        calls << "seat,known_poke_calls\n";
        for (int seat = 0; seat < 2; ++seat) {
            std::ostringstream audit;
            audit << "frame,move,phase,incoming,arriving,colours,part,before,after,kind,waves\n";
            for (const auto& d : audited[seat])
                audit << d.frame << ',' << d.move << ',' << d.phase << ',' << d.incoming << ','
                      << d.arriving << ',' << d.colours << ',' << d.part << ',' << d.before << ','
                      << d.after << ',' << d.kind << ',' << d.waves << '\n';
            if (!saveClosedText(request.directory / ("lv37_parts-" + std::to_string(seat) + ".csv"), audit.str()))
                return false;
            const auto* x3 = dynamic_cast<const ai::X3Agent*>(agents[seat]);
            calls << seat << ',' << (x3 ? x3->knownPokeCalls : 0) << '\n';
        }
        if (!saveClosedText(request.directory / "lv37_calls.csv", calls.str())) return false;

#ifdef JURAIC_COUNT_COMPONENTS
        std::ostringstream counts;
        counts << "two_wave,reselect,tail_answer,tail_rank,tail_value,cut_ahead,preserve,ex_flip\n";
        for (size_t i = 0; i < ai::componentCalls.size(); ++i)
            counts << (i ? "," : "") << ai::componentCalls[i].load(std::memory_order_relaxed);
        counts << '\n';
        if (!saveClosedText(request.directory / "component_calls.csv", counts.str())) return false;
#endif
        uint64_t checksum = 0x9E3779B97F4A7C15ull;
        for (const char c : result.summary) checksum = mix(checksum, uint8_t(c));
        const int winner = result.state == RoundState::Won0 ? 0 : result.state == RoundState::Won1 ? 1 : -1;
        std::ofstream ledger(request.ledger, std::ios::binary | std::ios::app);
        ledger << request.attempt << ',' << opt.seed << ',' << request.pair << ',' << request.aSeat << ',' << winner << ','
               << result.frames << ',' << int(result.state) << ',' << seed0 << ',' << (opt.ownTsumo ? seed1 : seed0) << ','
               << std::hex << checksum << '\n';
        ledger.close();
        if (!ledger) return false;
        std::cout << result.summary << "\nchecksum " << std::hex << checksum << '\n';
        return true;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return false;
    }
}
