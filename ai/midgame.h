#pragma once
#include "exchange.h"

namespace juraic::ai {

// The last attack-size bucket is open-ended. A larger offset may be the
// only answer even when another plan is earlier or keeps a larger main chain.
struct MidgameAttacks {
    Plan fast, reserve, strong;

    void consider(const Plan& plan) {
        if (!fast.count || plan.fallen < fast.fallen || (plan.fallen == fast.fallen && plan.endFrame < fast.endFrame))
            fast = plan;
        if (!reserve.count || plan.mainAfter > reserve.mainAfter ||
            (plan.mainAfter == reserve.mainAfter && plan.sent > reserve.sent)) reserve = plan;
        if (!strong.count || plan.fallen < strong.fallen || (plan.fallen == strong.fallen &&
            (plan.sent > strong.sent || (plan.sent == strong.sent && plan.endFrame < strong.endFrame)))) strong = plan;
    }

    void appendTo(std::vector<Plan>& plans) const {
        if (!fast.count) return;
        plans.push_back(fast);
        if (!samePlacements(fast, reserve)) plans.push_back(reserve);
        if (!samePlacements(fast, strong) && !samePlacements(reserve, strong)) plans.push_back(strong);
    }
};

// Continue past the exhaustive search with observed pairs. One part of the beam
// builds the main chain. The other keeps fields with a separate, reachable short
// fire in one trial piece. A trial only selects a field; a returned fire always
// uses a full observed pair, and is replayed with input paths before it is used.
class MidgameBeam {
public:
    // Returns false when `stop` cuts the beam before its last layer: set.tactical then holds the layers made before.
    template<class Stop>
    static bool extend(const Observation& o, const Params& p, uint64_t seed, PlanSet& set, Stop stop) {
        const int limit = std::min(set.knownCount, MIDGAME_PLAN_PAIRS);
        if (!set.pairCount || limit < 2 || p.handWait > 0) return true;
        if (stop()) return false;
        const int who = set.who;
        const int other = 1 - who;
        const int64_t end = o.frame + 6000;
        const int rate = o.rules.rateAt(o.frame);
        if (limit <= set.pairCount && std::none_of(set.plans.begin(), set.plans.end(), [&](const Plan& plan) {
            return plan.fireDepth && plan.fireDepth < limit && plan.chains == 1 && !plan.allClear && plan.points < W * rate;
        })) return true;
        Node root{Duel(o, seed)};
        root.duel.tallPaths = p.x3TallPaths;
        // (Timing::landThenLock: Duel::place takes the buttons of placeFast on every field)
        if ((p.readMidgame && p.x3TallPaths > 0) || o.timing.landThenLock) {
            static thread_local SpawnInputCache inputs;
            inputs.configure(o.timing, o.rules.speedPercent);
            root.duel.spawnInputs = &inputs;
        }
        const DuelPlayer& initial = root.duel.player[who];
        // (what waits above him and what of it has fallen since: garbage that was waiting falls on him while he lands or his
        //  chain ends, and is no part of what these plans send)
        // (Params::x3SentAfterHis: nor is what his chain that runs offsets of it)
        const int baseSent = p.x3SentAfterHis ? aboveAfterHis(root.duel, who, end)
                                              : root.duel.player[other].fallen + root.duel.player[other].incoming.total();
        const int baseScore = initial.scoring.score;
        const int baseColors = colors(initial.field);
        Duel unopposed = root.duel;
        if (unopposed.player[other].phase == DuelPhase::Check || unopposed.player[other].phase == DuelPhase::Settle)
            toPairOf(unopposed, other, end);
        const int incoming = unopposed.player[who].fallen + unopposed.player[who].incoming.total();
        if (!set.midAir && !toPairOf(root.duel, who, end)) return true;
        std::vector<Node> current{std::move(root)}, next;
        for (int depth = 0; depth < limit && !current.empty(); ++depth) {
            next.clear();
            // For each first placement and sent row keep the earliest attack,
            // the strongest offset and the most main chain. More than four rows
            // share a slot: the exhaustive search covers the large early fires.
            constexpr int ROWS = 5;
            std::array<MidgameAttacks, PLACEMENTS * ROWS> attacks{};
            std::array<Plan, PLACEMENTS> mains{};
            for (const Node& parent : current) {
                if (stop()) return false;
                const auto& field = parent.duel.player[who].field;
                const unsigned legal = knownPlacements(field, set, depth, p);
                for (int id = 0; id < PLACEMENTS; ++id) {
                    if (!(legal >> id & 1)) continue;
                    Node child{parent};
                    Plan& plan = child.plan;
                    plan.count = int8_t(depth + 1);
                    plan.id[depth] = int8_t(id);
                    applyMove(child.duel, set, plan, depth);
                    if (!toPairOf(child.duel, who, end)) continue;
                    const auto& self = child.duel.player[who];
                    // A weak single can deliberately free the trigger for the short
                    // chain. Continue only when it spends less than a row and we
                    // have another observed pair to use the new shape.
                    const bool reshape = self.chain.chains == 1 && !self.scoring.allClear && depth + 1 < limit &&
                        self.scoring.score - parent.duel.player[who].scoring.score < W * rate;
                    if (self.chain.chains && !reshape) {
                        if ((depth < set.pairCount && !parent.reshaped) || self.scoring.allClear) continue;
                        // (Params::x3SentAfterHis: taken when his chain that runs is over too, as baseSent is)
                        const int sent = (p.x3SentAfterHis ? aboveAfterHis(child.duel, who, end)
                                                           : child.duel.player[other].fallen + child.duel.player[other].incoming.total()) - baseSent;
                        const int damage = self.fallen + self.incoming.total();
                        if (sent < W && incoming - damage < W) continue;
                        plan.fireDepth = plan.count;
                        plan.chains = int8_t(self.chain.chains);
                        plan.sent = sent;
                        plan.fallen = damage;
                        plan.points = self.scoring.score - baseScore;
                        plan.used = baseColors + 2 * plan.count - colors(self.field);
                        plan.lockFrame = self.lockFrame;
                        plan.endFrame = child.duel.frame;
                        plan.confirmFrame = self.settleFrame > 0 ? self.settleFrame : 0;
                        // A large known return must not evict the shorter side
                        // chain in the same garbage bucket. Keep the most
                        // effective main under each first placement separately.
                        if (plan.chains > 4) {
                            if (!p.readMidgame) continue;
                            auto& best = mains[plan.id[0]];
                            if (best.count && (best.fallen < damage || (best.fallen == damage &&
                                (best.sent > sent || (best.sent == sent && best.endFrame <= plan.endFrame))))) continue;
                            plan.value = buildValue(self.field, o.rules, o.timing, p, &plan.mainAfter);
                            best = plan;
                            continue;
                        }
                        plan.value = buildValue(self.field, o.rules, o.timing, p, &plan.mainAfter);
                        const size_t slot = size_t(plan.id[0]) * ROWS + size_t(std::clamp(sent / W, 0, ROWS - 1));
                        attacks[slot].consider(plan);
                        continue;
                    }
                    if (depth + 1 == limit || self.fallen >= W) continue;
                    child.reshaped |= reshape;
                    int main = 0;
                    child.value = buildValue(self.field, o.rules, o.timing, p, &main);
                    child.side = main >= W * rate ? sideFire(self.field, o, p, rate, child.reshaped) : Side{};
                    next.push_back(std::move(child));
                }
            }
            if (stop()) return false;
            for (const auto& slot : attacks) slot.appendTo(set.tactical);
            for (const Plan& main : mains) if (main.count) set.tactical.push_back(main);
            shrink(next, current, who);
        }
        return true;
    }

private:
    struct Side {
        int points = 0;
        bool keepsMain = false;
    };

    struct Node {
        Duel duel;
        Plan plan{};
        int value = 0;
        Side side;
        bool reshaped = false;
    };

    static int colors(const GameField& field) { return colouredPieces(field); }

    static Side sideFire(const GameField& field, const Observation& o, const Params& p, int rate, bool reshaped) {
        Trials trials;
        BitField remaining[W * MAX_COLORS];
        detect(field, o.rules, o.timing, 1, trials, remaining, false);
        Side best;
        for (int k = 0; k < trials.count; ++k) {
            const Trial& t = trials.item[k];
            if (t.chains > 4 || t.chains < o.rules.chainRequirement || t.points < W * rate) continue;
            GameField after = field;
            after.f = remaining[k];
            int main = 0;
            buildValue(after, o.rules, o.timing, p, &main);
            const bool kept = main > t.points;
            if ((kept || reshaped) && (kept > best.keepsMain || (kept == best.keepsMain && t.points > best.points)))
                best = {t.points, kept};
        }
        return best;
    }

    static void shrink(std::vector<Node>& found, std::vector<Node>& kept, int who) {
        constexpr size_t MAIN_LIMIT = 12, SIDE_LIMIT = 8;
        kept.clear();
        const auto keep = [&](const Node& n) {
            const auto& field = n.duel.player[who].field;
            if (std::any_of(kept.begin(), kept.end(), [&](const Node& old) {
                return old.plan.id[0] == n.plan.id[0] && old.duel.player[who].field == field;
            })) return false;
            kept.push_back(n);
            return true;
        };
        std::vector<const Node*> ranked;
        ranked.reserve(found.size());
        for (const Node& n : found) ranked.push_back(&n);
        std::stable_sort(ranked.begin(), ranked.end(), [](const Node* a, const Node* b) {
            if (a->value != b->value) return a->value > b->value;
            return a->duel.frame < b->duel.frame;
        });
        for (const Node* n : ranked) {
            keep(*n);
            if (kept.size() == MAIN_LIMIT) break;
        }
        const size_t total = kept.size() + SIDE_LIMIT;
        std::array<int, PLACEMENTS> byFirst{};
        std::stable_sort(ranked.begin(), ranked.end(), [](const Node* a, const Node* b) {
            if (a->side.keepsMain != b->side.keepsMain) return a->side.keepsMain;
            return a->side.points > b->side.points;
        });
        for (const Node* n : ranked) {
            if (!n->side.points) break;
            const int first = n->plan.id[0];
            if (byFirst[first] < 2 && keep(*n)) ++byFirst[first];
            if (kept.size() == total) break;
        }
    }
};

struct MidgameShelter {
    unsigned buried = 0; // a known attack can drop two rows before a viable reply
    unsigned safe = 0;   // a quiet first placement supports a clean reply to each threat
};

template<class Stop>
MidgameShelter findMidgameShelter(const Observation& o, const PlanSet& mine, const PlanSet& theirs, unsigned legal,
                                const Params& p, uint64_t seed, Stop stop, int ahead = 1) {
    if (p.handWait > 0 || !mine.pairCount || !theirs.pairCount || o.self().field.f.count() < 36 || stop()) return {};
    unsigned starts = legal;
    for (const Plan& plan : mine.plans) if (plan.fireDepth == 1) starts &= ~(1u << plan.id[0]);
    MidgameShelter result{0, starts};
    bool threatened = false;
    Duel before(o, seed);
    if (before.player[theirs.who].phase != DuelPhase::Control && !toPairOf(before, theirs.who, o.frame + 6000)) return {};
    for (const auto* threats : {&theirs.plans, &theirs.tactical}) for (const Plan& threat : *threats) {
        if (threat.fireDepth != 1 || threat.chains > 4 || threat.sent < 2 * W || threat.allClear) continue;
        if (stop()) return {};
        const Duel isolated = playOut(o, mine, nullptr, theirs, &threat, p, seed, true);
        const auto& fire = isolated.player[theirs.who];
        if (fire.moves != o.opponent().moves + 1 || !fire.chain.chains || isolated.player[mine.who].incoming.total() < 2 * W) continue;
        threatened = true;
        const Placement at = placement(threat.id[0]);
        GameField field = before.player[theirs.who].field;
        const PlaceResult placed = juraic::place(field, at, theirs.pair[0]);
        const int64_t landed = fire.lockFrame + o.timing.landFrames + o.timing.splitFrames(placed.split, at.x, placed.lockY);
        // (FASTEST_BOUND: no sooner than the straight way shows the chain; a chain the straight way does not fire is not
        //  counted on as seen)
        const int64_t straight = rivalSeenCheck(o, mine, nullptr, theirs, &threat, p, seed, landed, stop);
        const int64_t reveals = straight < 0 ? INT64_MAX : straight;
        std::array<int64_t, PLACEMENTS> nextOwn{};
        std::array<std::optional<Duel>, PLACEMENTS> afterHold;
        for (unsigned pending = starts; pending; pending &= pending - 1) {
            if (stop()) return {};
            const int id = std::countr_zero(pending);
            Plan hold;
            hold.count = 1;
            hold.id[0] = int8_t(id);
            afterHold[id] = playOut(o, mine, &hold, theirs, &threat, p, seed, true);
            const auto& own = afterHold[id]->player[mine.who];
            if (own.moves == o.self().moves + 1 && own.phase == DuelPhase::Control) nextOwn[id] = own.spawnFrame;
        }
        unsigned answered = 0;
        for (const auto* replies : {&mine.plans, &mine.tactical}) for (const Plan& reply : *replies) {
            if (!reply.fireDepth || reply.allClear || reply.mainAfter < threat.points) continue;
            const unsigned bit = 1u << reply.id[0];
            if (!(bit & result.safe) || (bit & answered) || !seenBeforeChoice(reveals, nextOwn[reply.id[0]], ahead, p.seeLate)) continue;
            if (stop()) return {};
            const Duel exchange = playOut(o, mine, &reply, theirs, &threat, p, seed, true);
            const auto& own = exchange.player[mine.who];
            if (own.moves == o.self().moves + reply.count && own.chain.chains && own.phase != DuelPhase::Dead &&
                !own.fallen && !own.incoming.total()) answered |= bit;
        }
        result.safe &= answered;
        if (!result.safe) return {};
        for (unsigned pending = starts & ~answered & ~result.buried; pending; pending &= pending - 1) {
            if (stop()) return {};
            const int id = std::countr_zero(pending);
            Plan hold;
            hold.id[0] = int8_t(id);
            const auto& own = afterHold[id]->player[mine.who];
            if (own.moves != o.self().moves + 1) continue;
            if (own.fallen >= 2 * W) {
                result.buried |= 1u << id;
                continue;
            }
            if (own.phase != DuelPhase::Control || own.fallen || !own.incoming.total() || mine.knownCount < 2) continue;
            // The next pair is visible. Rule out every reachable placement below
            // this first move before calling it buried. A later warning alone is
            // insufficient: the next pair may still offset it or buy another turn.
            unsigned next = legalPlacements(own.field);
            bool tested = false, escapes = false;
            hold.count = 2;
            for (; next && !escapes; next &= next - 1) {
                if (stop()) return {};
                hold.id[1] = int8_t(std::countr_zero(next));
                const Duel further = playOut(o, mine, &hold, theirs, &threat, p, seed, true);
                const auto& after = further.player[mine.who];
                if (after.moves != o.self().moves + 2) continue;
                tested = true;
                escapes = after.fallen < 2 * W;
            }
            if (tested && !escapes) result.buried |= 1u << id;
        }
    }
    return threatened && result.buried && !stop() ? result : MidgameShelter{};
}

} // namespace juraic::ai
