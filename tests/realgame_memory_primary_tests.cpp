// play/memory/primary14.h on a made-up memory: two seats found from their boards with no picture, read with their
// fields and the pairs shown, and what must not bind or be read.
#include "../play/memory/primary14.h"
#include <cstdio>
#include <set>
using namespace play::realgame;

struct Fixture {
    FieldProcess process;
    std::vector<uint8_t> ram = std::vector<uint8_t>(0x200000);
    std::set<uint64_t> touched; // every byte address a read covered
    int reads = 0, tearAt = 0;  // (tearAt: the frame counter of seat 0 moves on at this read)
    std::set<uint64_t> gone;    // pages (their first address) that cannot be read: a read that covers one fails whole
    template <class T> void put(uint64_t at, T value) { std::memcpy(ram.data() + at, &value, sizeof value); }
    static uint64_t player(int s) { return 0x10000 + uint64_t(s) * 0x80000; }
    static uint64_t operate(int s) { return player(s) + 0x10000; }
    static uint64_t field(int s) { return player(s) + 0x20000; }
    static uint64_t top(int s) { return operate(s) + 0x400; } // (the board beside the object that operates the pair)
    static uint64_t root(int s) { return player(s) + 0x30000; }
    static uint64_t tray(int s) { return player(s) + 0x40000; } // (the object with the garbage waiting above the field)
    void board(uint64_t at, const juraic::GameField& g) {
        for (int y = 0; y < 14; ++y)
            for (int x = 0; x < 6; ++x) {
                const int r = 13 - y;
                const uint32_t cell = uint32_t(y == 13 ? g.row14[size_t(x)] : g.f.get(x, y));
                put(at - 64 + uint64_t(r) * 32 + uint64_t(x) * 4, uint32_t(9 + r * 8 + x) << 16 | cell);
            }
    }
    void record(int s, int index, uint32_t kind, uint32_t axis, uint32_t child) {
        static const uint64_t at[3] = {0x88, 0xc0, 0xe8};
        put(root(s) + at[index], kind), put(root(s) + at[index] + 4, axis), put(root(s) + at[index] + 8, child);
    }
    void seat(int s, const juraic::GameField& g) {
        const uint64_t image = process.imageBase;
        put(player(s), image + 0x2ac500), put(player(s) + 64, operate(s)), put(player(s) + 152, operate(s)), put(player(s) + 128, field(s));
        put(operate(s), image + 0x2ad2c0), put(operate(s) + 40, player(s)), put(operate(s) + 48, field(s));
        put(field(s), image + 0x2b1470), put(field(s) + 80, player(s));
        put(player(s) + 0x50, uint32_t(1)), put(player(s) + 0x5e, uint8_t(s));
        // (the pair: column 5 of 6, the upper half of the cell six rows up, the child to the right)
        put(operate(s) + 0xcc, 4.f), put(operate(s) + 0xd0, 5.f), put(operate(s) + 0xdc, 4.f), put(operate(s) + 0xe0, 4.5f);
        put(operate(s) + 0xe8, uint32_t(1)), put(operate(s) + 0xec, uint8_t(5)), put(operate(s) + 0xed, uint8_t(8));
        put(operate(s) + 0x9d, uint8_t(1)), put(operate(s) + 0xa0, uint32_t(19 + s)), put(operate(s) + 0xa8, uint32_t(2)), put(operate(s) + 0xac, uint32_t(5));
        put(field(s) + 0x108, uint32_t(500 + s)), put(field(s) + 0x10c, uint32_t(20)), put(field(s) + 0x2cc, uint32_t(0)), put(field(s) + 0x2d0, uint32_t(1));
        // (the pieces the game counts in the field, those of them that are garbage, the height of the sixth column, the
        //  garbage of the chain the field is busy with)
        put(field(s) + 0x188, uint16_t(7 + s)), put(field(s) + 0x18a, uint16_t(3)), put(field(s) + 0x238, uint8_t(2 + s)), put(field(s) + 0x254, uint32_t(40 + s));
        board(top(s), g);
        // (the garbage waiting above the field: the object the player names, which names it back)
        put(player(s) + 0xb0, tray(s)), put(tray(s), image + 0x2ac998), put(tray(s) + 0x20, player(s)), put(tray(s) + 0x130, uint32_t(30 + s));
        // (the game's account of it, kept for the seat it comes from: that seat's chain runs, on its way, not to fall yet, to fall)
        put(tray(s) + 0x30 + 32 * uint64_t(1 - s), uint32_t(1)), put(tray(s) + 0x34 + 32 * uint64_t(1 - s), uint32_t(5 + s));
        put(tray(s) + 0x38 + 32 * uint64_t(1 - s), uint32_t(7 + s)), put(tray(s) + 0x3c + 32 * uint64_t(1 - s), uint32_t(9 + s));
        // (the window of the next pairs: the pair in play, then the two shown)
        put(player(s) + 0x90, root(s) + 0x20);
        put(root(s), image + 0x2ac288), put(root(s) + 0x20, image + 0x2ac300), put(root(s) + 0x28, player(s));
        record(s, 0, 0x201, 2, 5), record(s, 1, 0x201, 1, 3), record(s, 2, 0x201, 4, 4);
        // (what lies beyond the window and where the pairs come from: must never be read)
        for (uint64_t a = 0x44; a < 0x84; ++a) put(root(s) + a, uint8_t(0xee));
        for (uint64_t a = 0x110; a < 0x11c; ++a) put(root(s) + a, uint8_t(0xee));
    }
    juraic::GameField fields[2];
    Fixture() {
        process = {1, 10, 0x140000000};
        auto& h = process.image;
        h.ok = true, h.machine = 0x8664, h.timeStamp = 0x66a8c684, h.imageSize = 0x6fd000;
        fields[0].f.set(0, 0, juraic::Cell(1)), fields[0].f.set(1, 0, juraic::Cell(3)), fields[0].f.set(1, 1, juraic::Cell(6));
        fields[1].f.set(5, 0, juraic::Cell(2)), fields[1].row14[2] = juraic::Cell(4);
        seat(0, fields[0]), seat(1, fields[1]);
    }
    FieldRead14 read = [this](uint64_t a, void* out, size_t n) {
        ++reads;
        if (a > ram.size() || n > ram.size() - size_t(a)) return false;
        for (const uint64_t page : gone)
            if (a < page + 4096 && a + n > page) return false;
        if (tearAt && reads == tearAt) put(field(0) + 0x108, uint32_t(777));
        for (size_t i = 0; i < n; ++i) touched.insert(a + i);
        std::memcpy(out, ram.data() + a, n);
        return true;
    };
    std::vector<uint64_t> boards() const { return {top(0), top(1)}; }
    PrimaryBinding14 bind(std::vector<uint64_t> extra = {}) {
        std::vector<uint64_t> all = boards();
        all.insert(all.end(), extra.begin(), extra.end());
        return bindPrimary14(process, all, read);
    }
};

int main() {
    int checks = 0, failed = 0;
    const auto check = [&](bool b, const char* s) {
        ++checks;
        if (!b) ++failed, std::printf("FAIL %s\n", s);
    };
    {
        Fixture f;
        const auto b = f.bind();
        check(b.both() && b.seat[0].player == Fixture::player(0) && b.seat[1].player == Fixture::player(1) && b.seat[0].top == Fixture::top(0) &&
                  b.seat[1].field == Fixture::field(1),
              "both seats bound from their boards, by the seat the player objects name");
        // (the binding copies what lies beside each board to find its owner's mark; what is read frame by frame from
        //  then on is counted here)
        f.touched.clear();
        const auto s0 = readPrimarySeat14(b, 0, f.read), s1 = readPrimarySeat14(b, 1, f.read);
        check(s0 && s1 && s0->counted && s1->counted && s0->pieces == 7 && s1->pieces == 8 && s0->garbagePieces == 3 && s0->sixthHeight == 2 && s1->sixthHeight == 3 &&
                  s0->chainGarbage == 40 && s1->chainGarbage == 41,
              "the counts of the field object are read with the seat");
        check(s0 && s1 && s0->noticeKnown && s0->notice == 30 && s1->noticeKnown && s1->notice == 31, "the garbage waiting above each field is read from the object the player names");
        check(s0 && s1 && s0->trayKnown && s0->trayChain && s0->trayFlying == 5 && s0->trayPending == 7 && s0->trayConfirmed == 9 && s1->trayKnown && s1->trayFlying == 6 &&
                  s1->trayPending == 8 && s1->trayConfirmed == 10,
              "the game's account of the garbage waiting is read for the seat it comes from");
        {
            // (a state that is none of the two: the account is not taken, the number shown is)
            Fixture g;
            const auto c = g.bind();
            g.put(Fixture::tray(0) + 0x50, uint32_t(2));
            g.put(Fixture::tray(1) + 0x30, uint32_t(0));
            const auto t0 = readPrimarySeat14(c, 0, g.read), t1 = readPrimarySeat14(c, 1, g.read);
            check(t0 && t0->noticeKnown && !t0->trayKnown && t1 && t1->trayKnown && !t1->trayChain, "an account whose state is no state is not taken; one of a seat with no chain is");
        }
        {
            // (an object of another class, one that names another player back, an address inside the object that operates
            //  the pair: no number is taken, the seat is read all the same)
            Fixture g;
            const auto c = g.bind();
            g.put(Fixture::tray(0), g.process.imageBase + 0x2ac990);
            g.put(Fixture::tray(1) + 0x20, Fixture::player(0));
            auto t0 = readPrimarySeat14(c, 0, g.read), t1 = readPrimarySeat14(c, 1, g.read);
            check(t0 && !t0->noticeKnown && t0->counted && t1 && !t1->noticeKnown, "an object of another class, or one that names another player, gives no garbage waiting");
            Fixture h;
            const auto d = h.bind();
            h.put(Fixture::player(0) + 0xb0, Fixture::operate(0) + 0x40);
            h.touched.clear();
            t0 = readPrimarySeat14(d, 0, h.read);
            bool buttons = false;
            for (uint64_t a = 0x98; a < 0x9c; ++a) buttons = buttons || h.touched.count(Fixture::operate(0) + a);
            check(t0 && !t0->noticeKnown && !buttons, "an address inside the object that operates the pair is not read for the garbage waiting");
        }
        check(s0 && s1 && s0->pose.seat == 0 && s1->pose.seat == 1 && s0->pose.x == 4 && s0->pose.y == 6 && s0->pose.upper && s0->pose.dir == 1 && s0->pose.operating &&
                  s0->pose.lockedPairs == 19 && s1->pose.lockedPairs == 20 && s0->pose.count108 == 500,
              "the pair in play of each seat");
        check(s0 && s1 && s0->field == f.fields[0] && s1->field == f.fields[1] && s1->field.row14[2] == juraic::Cell(4), "the field of each seat, the 14th row with it");
        check(s0 && s0->pairsKnown && s0->pairs[0].axis == juraic::Cell(2) && s0->pairs[0].child == juraic::Cell(5) && s0->pairs[1].axis == juraic::Cell(1) &&
                  s0->pairs[1].child == juraic::Cell(3) && s0->pairs[2].axis == juraic::Cell(4) && s0->pairs[2].child == juraic::Cell(4),
              "the pair in play and the two shown as next");
        bool beyond = false;
        for (int s = 0; s < 2; ++s) {
            for (uint64_t a = 0x44; a < 0x84; ++a) beyond = beyond || f.touched.count(Fixture::root(s) + a);
            for (uint64_t a = 0x110; a < 0x11c; ++a) beyond = beyond || f.touched.count(Fixture::root(s) + a);
            for (uint64_t a = 0x98; a < 0x9c; ++a) beyond = beyond || f.touched.count(Fixture::operate(s) + a);
        }
        check(!beyond, "reading a bound seat touches nothing beyond the window of the next pairs, nor the buttons");
    }
    {
        // The pieces the game counts in each column: the eight bytes the field object names, taken when the two beside
        // the six are 0 and the six make the pieces counted.
        Fixture f;
        const uint64_t columns = 0x1a0000;
        const uint8_t eight[8] = {0, 1, 0, 2, 0, 3, 1, 0}; // (seven pieces: those of seat 0)
        for (uint64_t i = 0; i < 8; ++i) f.put(columns + i, eight[i]);
        f.put(Fixture::field(0) + 0x140, columns);
        const auto b = f.bind();
        auto s0 = readPrimarySeat14(b, 0, f.read);
        const auto s1 = readPrimarySeat14(b, 1, f.read);
        check(s0 && s0->columnsKnown && s0->columnPieces == std::array<uint8_t, 6>{1, 0, 2, 0, 3, 1} && s1 && s1->counted && !s1->columnsKnown,
              "the pieces of each column are read with the seat whose field object names them");
        f.put(columns + 3, uint8_t(3));
        s0 = readPrimarySeat14(b, 0, f.read);
        check(s0 && s0->counted && !s0->columnsKnown, "six numbers that do not make the pieces counted are not taken for the columns");
        f.put(columns + 3, uint8_t(2)), f.put(columns + 7, uint8_t(1));
        s0 = readPrimarySeat14(b, 0, f.read);
        check(s0 && !s0->columnsKnown, "... nor six with another number than 0 beside them");
        f.put(columns + 7, uint8_t(0));
        s0 = readPrimarySeat14(b, 0, f.read);
        check(s0 && s0->columnsKnown, "... and taken again once they fit");
        // (an address inside the object that operates the pair: not read)
        f.put(Fixture::field(0) + 0x140, Fixture::operate(0) + 0x94);
        f.touched.clear();
        s0 = readPrimarySeat14(b, 0, f.read);
        bool buttons = false;
        for (uint64_t a = 0x98; a < 0x9c; ++a) buttons = buttons || f.touched.count(Fixture::operate(0) + a);
        check(s0 && !s0->columnsKnown && !buttons, "an address inside the object that operates the pair is not read for the columns");
    }
    {
        // A board left over from a round that is over: the same cells elsewhere, no live owner beside it.
        Fixture f;
        f.board(0x180000, f.fields[0]);
        const auto b = f.bind({0x180000});
        check(b.both() && b.seat[0].top == Fixture::top(0), "a board without a live owner beside it binds nothing and takes no seat");
    }
    {
        for (int cpu : {0, 1}) {
            Fixture f;
            f.put(Fixture::operate(cpu), f.process.imageBase + PRIMARY_CPU_OPERATE14);
            f.put(Fixture::player(cpu) + 64, uint64_t(0)); // the CPU controller is installed at player + 0x98
            const auto binding = f.bind();
            const auto opponent = readPrimarySeat14(binding, cpu, f.read);
            const auto own = readPrimarySeat14(binding, 1 - cpu, f.read);
            check(binding.both() && opponent && opponent->pose.cpuControlled && own && !own->pose.cpuControlled,
                  "the local CPU and human bind to their own boards in either seat order");
            f.put(Fixture::operate(cpu) + 40, Fixture::player(1 - cpu));
            const auto broken = bindPrimary14(f.process, f.boards(), f.read, nullptr, {Fixture::player(cpu)});
            check(!broken.seat[cpu].bound() && !readPrimarySeat14(binding, cpu, f.read), "a CPU controller with a broken player link is rejected");
        }
        for (uint64_t mark : {uint64_t(0x2acd20), uint64_t(0x2acb50)}) {
            Fixture f;
            const auto previous = f.bind();
            f.put(Fixture::operate(1), f.process.imageBase + mark);
            const auto b = bindPrimary14(f.process, f.boards(), f.read, nullptr, {Fixture::player(1)});
            check(!b.seat[1].bound() && !readPrimarySeat14(previous, 1, f.read), "network and unknown controller types do not bind or read");
        }
    }
    {
        // A seat whose board has no owner beside it (the game did not make its objects side by side): not bound by the
        // boards alone; bound from its player object, by the board its links name.
        const uint64_t away = 0x180000;
        const auto apart = [&](Fixture& f) {
            for (uint64_t a = Fixture::top(1) - 64; a < Fixture::top(1) + 376; ++a) f.put(a, uint8_t(0));
            f.board(away, f.fields[1]);
        };
        const std::vector<uint64_t> boards{Fixture::top(0), away}, players{Fixture::player(0), Fixture::player(1)};
        {
            Fixture f;
            apart(f);
            f.put(Fixture::player(1) + 0xc8, away - 0x2a4);
            PrimaryBindNote14 note;
            auto b = bindPrimary14(f.process, boards, f.read, &note);
            check(b.seat[0].bound() && !b.seat[1].bound() && note.players.empty(), "a board with no owner beside it: its seat is not bound by the boards alone");
            b = bindPrimary14(f.process, boards, f.read, &note, players);
            check(b.both() && b.seat[1].player == Fixture::player(1) && b.seat[1].top == away && b.seat[1].field == Fixture::field(1) && b.seat[1].operate == Fixture::operate(1) &&
                      b.seat[0].top == Fixture::top(0),
                  "... and is bound from its player object, whose link names the board");
            check(note.players.size() == 1 && note.players[0].at == Fixture::player(1) && note.players[0].seat == 1 && note.players[0].top == away &&
                      note.text().find(":0:-1 p") != std::string::npos,
                  "... and what became of the player object is told, after the boards");
            const auto s1 = readPrimarySeat14(b, 1, f.read);
            check(s1 && s1->field == f.fields[1] && s1->pose.seat == 1 && s1->pose.lockedPairs == 20, "... and the seat is read, with that board");
        }
        {
            Fixture f;
            apart(f);
            f.put(Fixture::operate(1), f.process.imageBase + PRIMARY_CPU_OPERATE14);
            f.put(Fixture::player(1) + 0xc8, away - 0x2a4);
            const auto b = bindPrimary14(f.process, boards, f.read, nullptr, players);
            const auto cpu = readPrimarySeat14(b, 1, f.read);
            check(b.both() && b.seat[1].top == away && cpu && cpu->pose.cpuControlled, "a distant CPU board binds by its player's link");
        }
        // (each of the places a player object has been seen to name its board by)
        for (const PrimaryPlayerLink14& link : PRIMARY_PLAYER_LINKS14) {
            Fixture f;
            apart(f);
            f.put(Fixture::player(1) + link.at, uint64_t(int64_t(away) + link.less));
            const auto b = bindPrimary14(f.process, boards, f.read, nullptr, players);
            check(b.both() && b.seat[1].top == away, "a seat bound from its player object by each of the links a player object has");
        }
        {
            // The player object's own cells (primaryBoardOf14): the board is where they are, with nothing of the
            // player's beside it and none of the links, whether the scan found that board or not.
            const uint64_t view = 0x1b0000;
            const auto cells = [&](Fixture& f, uint64_t board, uint64_t back) {
                f.put(Fixture::player(1) + 0xc8, view), f.put(view, f.process.imageBase + 0x2a6460), f.put(view + 0x20, back), f.put(view + 0x58, board - 0x64);
            };
            Fixture f;
            apart(f);
            cells(f, away, Fixture::player(1));
            check(primaryBoardOf14(f.process, Fixture::player(1), f.read) == away && !primaryBoardOf14(f.process, Fixture::player(0), f.read),
                  "the board of a player object is where the object it names has its cells");
            PrimaryBindNote14 note;
            auto b = bindPrimary14(f.process, {Fixture::top(0)}, f.read, &note, players);
            check(b.both() && b.seat[1].top == away && b.seat[1].player == Fixture::player(1) && b.seat[1].field == Fixture::field(1) && b.seat[0].top == Fixture::top(0),
                  "a seat with no owner beside its board is bound by its player's own cells, a board the scan did not find too");
            check(note.players.size() == 1 && note.players[0].cells && note.text().find("c", note.text().find(" p")) != std::string::npos, "... and it is told that the cells named it");
            {
                // (the seat bound by its player's own cells is marked so: its board is gone when the player names them no more)
                check(b.seat[1].ownCells && !b.seat[0].ownCells, "... the seat bound by its player's cells is marked so, the other is not");
                uint64_t named = 0;
                std::memcpy(&named, f.ram.data() + Fixture::player(1) + 0xc8, sizeof named);
                f.put(Fixture::player(1) + 0xc8, uint64_t(0));
                PrimaryFault14 why;
                const bool read = bool(readPrimarySeat14(b, 1, f.read, &why));
                f.put(Fixture::player(1) + 0xc8, named);
                check(!read && why.code == PrimaryFault14::Board, "a seat bound by its player's cells whose player names them no more: its board is taken as gone");
            }
            const auto s1 = readPrimarySeat14(b, 1, f.read);
            check(s1 && s1->field == f.fields[1] && s1->pose.seat == 1, "... and the seat is read, with that board");
            // (the cells go before a link that names another board)
            f.board(0x190000, f.fields[1]);
            f.put(Fixture::player(1) + 0x88, uint64_t(0x190000) + 0x1cc);
            b = bindPrimary14(f.process, {Fixture::top(0), away, 0x190000}, f.read, nullptr, players);
            check(b.both() && b.seat[1].top == away, "the player's own cells go before the things it names beside a board");
            Fixture g;
            apart(g);
            cells(g, away, Fixture::player(0));
            check(!primaryBoardOf14(g.process, Fixture::player(1), g.read), "an object that names another player back gives no board");
            b = bindPrimary14(g.process, boards, g.read, nullptr, players);
            check(b.seat[0].bound() && !b.seat[1].bound(), "... and the seat is not bound by it");
            Fixture h;
            apart(h);
            cells(h, away, Fixture::player(1));
            h.put(view, h.process.imageBase + 0x2a6468);
            check(!primaryBoardOf14(h.process, Fixture::player(1), h.read), "an object of another class gives no board");
            Fixture k;
            apart(k);
            cells(k, 0x1c0000, Fixture::player(1));
            check(!primaryBoardOf14(k.process, Fixture::player(1), k.read), "cells that do not read as a board give no board");
            Fixture m;
            apart(m);
            cells(m, Fixture::top(0), Fixture::player(1));
            b = bindPrimary14(m.process, boards, m.read, nullptr, players);
            check(b.seat[0].bound() && b.seat[0].top == Fixture::top(0) && !b.seat[1].bound(), "cells that are the other seat's board: the seat is not bound");
        }
        {
            // Two links that name two boards, a link that names no board found, a link that names the other seat's
            // board, a player object that is no live player: the seat stays unbound.
            Fixture f;
            apart(f);
            f.board(0x190000, f.fields[1]);
            f.put(Fixture::player(1) + 0xc8, away - 0x2a4), f.put(Fixture::player(1) + 0x88, uint64_t(0x190000) + 0x1cc);
            auto b = bindPrimary14(f.process, {Fixture::top(0), away, 0x190000}, f.read, nullptr, players);
            check(b.seat[0].bound() && !b.seat[1].bound(), "a player object whose links name two boards: the seat is not bound");
            Fixture g;
            apart(g);
            g.put(Fixture::player(1) + 0xc8, away - 0x2a4 + 8);
            b = bindPrimary14(g.process, boards, g.read, nullptr, players);
            check(b.seat[0].bound() && !b.seat[1].bound(), "a player object whose link names no board found: the seat is not bound");
            Fixture h;
            apart(h);
            h.put(Fixture::player(1) + 0xc8, Fixture::top(0) - 0x2a4);
            b = bindPrimary14(h.process, boards, h.read, nullptr, players);
            check(b.seat[0].bound() && b.seat[0].top == Fixture::top(0) && !b.seat[1].bound(), "a player object whose link names the other seat's board: the seat is not bound");
            Fixture k;
            apart(k);
            k.put(Fixture::player(1) + 0xc8, away - 0x2a4), k.put(Fixture::field(1), uint64_t(0));
            PrimaryBindNote14 note;
            b = bindPrimary14(k.process, boards, k.read, &note, players);
            check(b.seat[0].bound() && !b.seat[1].bound() && note.players.size() == 1 && note.players[0].seat == -10 - int(PrimaryFault14::Objects),
                  "a player object whose field is gone: the seat is not bound, and it is told with what did not read");
        }
        {
            // Two boards close together: the object that operates seat 0's pair lies beside both. Without the cells of
            // its player the seat has two boards and is not bound (as before); with them the board is the one they are in.
            const uint64_t close = Fixture::top(0) + 0x800, view = 0x1b0000, view0 = 0x1b8000;
            Fixture f;
            for (uint64_t a = Fixture::top(1) - 64; a < Fixture::top(1) + 376; ++a) f.put(a, uint8_t(0));
            f.board(close, f.fields[1]);
            f.put(Fixture::player(1) + 0xc8, view), f.put(view, f.process.imageBase + 0x2a6460), f.put(view + 0x20, Fixture::player(1)), f.put(view + 0x58, close - 0x64);
            auto b = bindPrimary14(f.process, {Fixture::top(0), close}, f.read, nullptr, players);
            check(!b.seat[0].bound() && b.seat[1].bound() && b.seat[1].top == close, "boards close together, the player of seat 0 names no cells: the seat has two boards and is not bound");
            f.put(Fixture::player(0) + 0xc8, view0), f.put(view0, f.process.imageBase + 0x2a6460), f.put(view0 + 0x20, Fixture::player(0)), f.put(view0 + 0x58, Fixture::top(0) - 0x64);
            PrimaryBindNote14 note;
            b = bindPrimary14(f.process, {Fixture::top(0), close}, f.read, &note, players);
            check(b.both() && b.seat[0].top == Fixture::top(0) && b.seat[0].player == Fixture::player(0) && b.seat[1].top == close && b.seat[1].player == Fixture::player(1),
                  "boards close together: the object beside both is of the board its player's own cells are in, and both seats are bound");
            const auto s0 = readPrimarySeat14(b, 0, f.read), s1 = readPrimarySeat14(b, 1, f.read);
            check(s0 && s1 && s0->field == f.fields[0] && s1->field == f.fields[1], "... and both seats are read, each with its board");
        }
        {
            // Both seats bound by their boards: the player objects are not looked at.
            Fixture f;
            f.put(Fixture::player(1) + 0xc8, away - 0x2a4);
            f.board(away, f.fields[1]);
            PrimaryBindNote14 note;
            const auto b = bindPrimary14(f.process, {Fixture::top(0), Fixture::top(1), away}, f.read, &note, players);
            check(b.both() && b.seat[1].top == Fixture::top(1) && note.players.empty(), "seats bound by their boards stay as they are, whatever the player objects name");
        }
    }
    {
        // The owner of seat 1 gone (its class replaced): seat 1 is not bound, seat 0 is.
        Fixture f;
        f.put(Fixture::operate(1), uint64_t(0));
        const auto b = f.bind();
        check(b.seat[0].bound() && !b.seat[1].bound() && !b.both(), "a seat whose objects are gone is not bound");
        check(!readPrimarySeat14(b, 1, f.read), "... and not read");
    }
    {
        // A page beside a board that cannot be read (the game's objects are large; a page of one not used yet is not
        // there): the object that operates the pair is found all the same, and the seat is bound.
        for (const uint64_t page : {uint64_t(0x1f000), uint64_t(0x21000)}) {
            Fixture f;
            f.gone.insert(page);
            PrimaryBindNote14 note;
            const auto b = bindPrimary14(f.process, f.boards(), f.read, &note);
            check(b.both() && b.seat[0].top == Fixture::top(0) && b.seat[0].operate == Fixture::operate(0) && b.seat[1].operate == Fixture::operate(1),
                  page == 0x1f000 ? "a page before a board that cannot be read: both seats bound all the same" : "a page after a board that cannot be read: both seats bound all the same");
            check(note.boards.size() == 2 && note.boards[0].board && note.boards[0].pages == 3 && note.boards[0].got == 2 && note.boards[0].owners == 1 && note.boards[0].seat == 0 &&
                      note.boards[1].got == 3 && note.boards[1].seat == 1 && note.text().find(":1:2/3:1:0") != std::string::npos,
                  "... and what became of each board is told");
            check(readPrimarySeat14(b, 0, f.read) && readPrimarySeat14(b, 0, f.read)->field == f.fields[0], "... and the seat is read");
        }
        Fixture g;
        g.gone.insert(0x20000);
        PrimaryBindNote14 note;
        const auto b = bindPrimary14(g.process, g.boards(), g.read, &note);
        check(!b.seat[0].bound() && b.seat[1].bound() && !note.boards[0].board, "the page of a board itself not readable: that seat is not bound, and it is told that the board did not read");
    }
    {
        // Two live players that both say seat 0: that seat stays unbound.
        Fixture f;
        f.put(Fixture::player(1) + 0x5e, uint8_t(0));
        const auto b = f.bind();
        check(!b.seat[0].bound() && !b.seat[1].bound(), "two players for one seat: the seat is not bound");
    }
    {
        Fixture f;
        const auto b = f.bind();
        f.put(Fixture::operate(0) + 0x9d, uint8_t(0));
        const auto s = readPrimarySeat14(b, 0, f.read);
        check(s && !s->pose.operating && !s->pairsKnown && s->field == f.fields[0], "no pair in the player's hands: the field is read, the pairs are not");
        check(s && s->windowKnown && s->window[0].axis == juraic::Cell(2) && s->window[1].child == juraic::Cell(3) && s->window[2].axis == juraic::Cell(4),
              "... and what the window's records read as is told apart from them");
    }
    {
        Fixture f;
        const auto b = f.bind();
        f.put(Fixture::root(0) + 0x38, uint32_t(3));
        const auto s = readPrimarySeat14(b, 0, f.read);
        check(s && s->pose.operating && !s->pairsKnown, "the window on its way: the seat is read, the pairs are not");
    }
    {
        Fixture f;
        const auto b = f.bind();
        f.record(0, 2, 0x202, 4, 4);
        const auto s = readPrimarySeat14(b, 0, f.read);
        check(s && !s->pairsKnown, "a record of another kind: the pairs are not known");
    }
    {
        Fixture f;
        const auto b = f.bind();
        f.record(0, 0, 0x201, 3, 5);
        const auto s = readPrimarySeat14(b, 0, f.read);
        check(s && !s->pairsKnown, "the window's first pair is not the pair in play: the pairs are not known");
    }
    {
        Fixture f;
        const auto b = f.bind();
        f.record(0, 1, 0x201, 6, 3);
        check(readPrimarySeat14(b, 0, f.read) && !readPrimarySeat14(b, 0, f.read)->pairsKnown, "a colour that is none of the five: the pairs are not known");
    }
    {
        // The game's frame goes by between the reads of one seat: nothing is given.
        Fixture f;
        const auto b = f.bind();
        f.reads = 0, f.tearAt = 14;
        check(!readPrimarySeat14(b, 0, f.read), "a frame of the game between the reads of a seat: the seat is not given");
        f.tearAt = 0;
        check(bool(readPrimarySeat14(b, 0, f.read)), "... and is given again in the next read");
    }
    {
        // A seat is given whenever its objects are there: with no pair in its hands whatever the place of the last one
        // reads as, with a pair whose place does not read as a place without that place.
        Fixture f;
        const auto b = f.bind();
        f.put(Fixture::operate(0) + 0xe0, 3.25f); // (drawn somewhere between)
        PrimaryFault14 fault;
        auto s = readPrimarySeat14(b, 0, f.read, &fault);
        check(s && s->pose.operating && !s->placed && !s->pairsKnown && s->pose.lockedPairs == 19 && s->field == f.fields[0] && fault.code == PrimaryFault14::Place &&
                  fault.operating == 1 && fault.tick == 500,
              "a pair in the hands whose place does not read as one: the seat is given, the place is not, and why is told");
        f.put(Fixture::operate(0) + 0x9d, uint8_t(0)), f.put(Fixture::operate(0) + 0xa8, uint32_t(0)), f.put(Fixture::operate(0) + 0xd0, 77.f);
        s = readPrimarySeat14(b, 0, f.read, &fault);
        check(s && !s->pose.operating && !s->placed && s->field == f.fields[0] && fault.code == PrimaryFault14::None,
              "no pair in the hands, the place and the colours of the last one reading as none: the seat is given");
        f.put(Fixture::field(0) + 0x2cc, uint32_t(40)), f.put(Fixture::field(0) + 0x2d0, uint32_t(50));
        s = readPrimarySeat14(b, 0, f.read, &fault);
        check(s && s->pose.phase == 40 && s->pose.fieldState == 50, "numbers of the field's state not seen before: the seat is given, with them");
        f.put(Fixture::operate(0), uint64_t(0));
        check(!readPrimarySeat14(b, 0, f.read, &fault) && fault.code == PrimaryFault14::Objects, "the objects gone: no seat, and why is told");
        // (bound at such a time too)
        Fixture g;
        g.put(Fixture::operate(1) + 0x9d, uint8_t(0)), g.put(Fixture::operate(1) + 0xd0, 77.f);
        check(g.bind().both(), "a seat with no pair in its hands and no place to read is bound all the same");
    }
    {
        // Following frame by frame: the counters alone are read while they stand.
        Fixture f;
        const auto b = f.bind();
        uint64_t clock = 1000000;
        const auto now = [&] { return clock; };
        PrimaryFollow14 follow;
        auto look = follow.look(b, 7, f.read, now);
        check(look.whole && look.fresh && look.read == 2 && follow.last() && follow.last()->serial == 1 && follow.last()->generation == 7 &&
                  follow.last()->observedUs == 1000000 && follow.last()->seat[1]->pose.count108 == 501,
              "following: the first look reads both seats, the first sample");
        clock += 1000, look = follow.look(b, 7, f.read, now);
        check(look.whole && !look.fresh && follow.last()->serial == 1, "following: the look after a new frame reads the seats once more; nothing new, no new sample");
        clock += 1000, f.reads = 0, f.touched.clear(), look = follow.look(b, 7, f.read, now);
        check(!look.whole && !look.fresh && f.reads == 2 && f.touched.size() == 8 && f.touched.count(Fixture::field(0) + 0x108) && f.touched.count(Fixture::field(1) + 0x10b),
              "following: while the counters stand, a look reads the two counters and nothing else");
        check(readPrimaryTick14(b, 1, f.read) == std::optional<uint32_t>(501) && !readPrimaryTick14(b, 2, f.read), "the frame counter of a seat alone");
        // (the frame of seat 1 goes by, its pair a half cell lower)
        f.put(Fixture::field(1) + 0x108, uint32_t(502)), f.put(Fixture::operate(1) + 0xe0, 5.f);
        clock += 1000, look = follow.look(b, 7, f.read, now);
        check(look.whole && look.fresh && look.step == 0 && follow.last()->serial == 2 && follow.last()->seat[1]->pose.count108 == 502 && !follow.last()->seat[1]->pose.upper &&
                  follow.last()->seat[0]->pose.count108 == 500,
              "following: a counter that moved on gives the next sample");
        // (seat 0 follows a moment later: the look after the new frame sees it)
        f.put(Fixture::field(0) + 0x108, uint32_t(501));
        clock += 1000, look = follow.look(b, 7, f.read, now);
        check(look.whole && look.fresh && look.step == 1 && follow.last()->serial == 3, "following: the other seat's frame a moment later is the next sample, one frame on");
        clock += 1000, look = follow.look(b, 7, f.read, now);
        clock += 1000, look = follow.look(b, 7, f.read, now);
        check(!look.whole, "following: then the counters stand again");
        // (something changes while the counters stand: seen by the whole read every 8 ms)
        f.put(Fixture::operate(0) + 0xe8, uint32_t(2));
        clock += 1000, look = follow.look(b, 7, f.read, now);
        check(!look.whole, "following: a change without a frame is not looked for every millisecond");
        clock += 8000, look = follow.look(b, 7, f.read, now);
        check(look.whole && look.fresh && look.step == 0 && follow.last()->serial == 4 && follow.last()->seat[0]->pose.dir == 2, "following: ... but by the whole read every 8 ms");
        // (three frames go by unseen)
        f.put(Fixture::field(0) + 0x108, uint32_t(504));
        clock += 1000, look = follow.look(b, 7, f.read, now);
        check(look.fresh && look.step == 3, "following: frames that went by unseen are counted");
        // (a seat that cannot be read: the sample has the other one, and the seats are read whole in every look)
        f.put(Fixture::operate(1), uint64_t(0)), f.put(Fixture::field(0) + 0x108, uint32_t(505));
        clock += 1000, look = follow.look(b, 7, f.read, now);
        check(look.whole && look.fresh && look.read == 1 && follow.last()->seat[0] && !follow.last()->seat[1], "following: a seat that cannot be read is missing in the sample");
        clock += 1000, look = follow.look(b, 7, f.read, now);
        clock += 1000, look = follow.look(b, 7, f.read, now);
        check(look.whole && !look.fresh, "following: ... and looked for in every look");
        f.put(Fixture::operate(0), uint64_t(0));
        clock += 1000, look = follow.look(b, 7, f.read, now);
        check(look.whole && !look.read && !follow.last(), "following: neither seat readable: no sample");
        f.put(Fixture::operate(0), f.process.imageBase + 0x2ad2c0), f.put(Fixture::operate(1), f.process.imageBase + 0x2ad2c0);
        clock += 1000, look = follow.look(b, 7, f.read, now);
        check(look.fresh && look.read == 2 && follow.last()->serial == 7, "following: readable again: the next sample, the serial numbers going on");
    }
    {
        Fixture f;
        auto b = f.bind();
        b.seat[0].field += 8;
        check(!readPrimarySeat14(b, 0, f.read), "another field object than the one bound: the seat is not given");
    }
    {
        Fixture f;
        f.process.image.timeStamp++;
        f.reads = 0;
        check(!f.bind().seat[0].bound() && !f.reads, "another program version: nothing is read");
    }
    {
        // A pair pushed off a wall by a turn: it is drawn on its way for some frames, its column bytes say where it is.
        Fixture f;
        const auto b = f.bind();
        f.put(Fixture::operate(0) + 0xcc, 2.695f), f.put(Fixture::operate(0) + 0xdc, 2.695f), f.put(Fixture::operate(0) + 0xe8, uint32_t(3));
        PrimaryFault14 fault;
        auto s = readPrimarySeat14(b, 0, f.read, &fault);
        check(s && s->placed && s->pose.x == 4 && s->pose.y == 6 && s->pose.upper && s->pose.dir == 3 && s->pose.horizontalMoving && s->pairsKnown && fault.code == PrimaryFault14::None,
              "a pair drawn on its way to its column (a turn pushed it): its place is read from its column and row");
        f.put(Fixture::operate(0) + 0xcc, 5.207f), f.put(Fixture::operate(0) + 0xdc, 5.207f), f.put(Fixture::operate(0) + 0xec, uint8_t(6));
        s = readPrimarySeat14(b, 0, f.read, &fault);
        check(s && s->placed && s->pose.x == 5, "... also when it is drawn beyond the last column for a moment");
        f.put(Fixture::operate(0) + 0xcc, .5f), f.put(Fixture::operate(0) + 0xdc, .5f);
        s = readPrimarySeat14(b, 0, f.read, &fault);
        check(s && !s->placed && fault.code == PrimaryFault14::Place, "a pair drawn far from the column it is said to be in: its place is not read");
        f.put(Fixture::operate(0) + 0xcc, 4.5f), f.put(Fixture::operate(0) + 0xdc, 4.f), f.put(Fixture::operate(0) + 0xec, uint8_t(5));
        s = readPrimarySeat14(b, 0, f.read, &fault);
        check(s && !s->placed, "the two numbers of where it is drawn not the same: its place is not read");
        f.put(Fixture::operate(0) + 0xcc, 4.f), f.put(Fixture::operate(0) + 0xed, uint8_t(9));
        s = readPrimarySeat14(b, 0, f.read, &fault);
        check(s && !s->placed, "the row byte not that of its height: its place is not read");
    }
    {
        // What became of the read of the window is told with the seat (for the records).
        Fixture f;
        const auto b = f.bind();
        auto s = readPrimarySeat14(b, 0, f.read);
        check(s && s->windowRead.stage == PrimaryWindow14::Read && s->windowRead.records[1][1] == 1 && s->windowRead.records[1][2] == 3, "the window read: told so, with its records");
        f.put(Fixture::operate(0) + 0x9d, uint8_t(0)), f.put(Fixture::root(0) + 0x38, uint32_t(3)), f.put(Fixture::root(0) + 0x40, uint32_t(9));
        f.touched.clear();
        s = readPrimarySeat14(b, 0, f.read);
        check(s && !s->windowKnown && s->windowRead.stage == PrimaryWindow14::Sliding && s->windowRead.slide[0] == 3 && s->windowRead.slide[2] == 9 &&
                  s->windowRead.records[0][0] == 0x201 && s->windowRead.records[2][1] == 4,
              "the window on its way: told so, with the numbers that say it and its records as they are");
        bool beyond = false;
        for (uint64_t a = 0x44; a < 0x84; ++a) beyond = beyond || f.touched.count(Fixture::root(0) + a);
        for (uint64_t a = 0x110; a < 0x11c; ++a) beyond = beyond || f.touched.count(Fixture::root(0) + a);
        check(!beyond, "... and nothing beyond the window is touched for it");
        const auto before = s;
        f.put(Fixture::root(0) + 0x40, uint32_t(8));
        check(!samePrimarySeat14(before, readPrimarySeat14(b, 0, f.read)), "a window further on its way is another sample");
        f.put(Fixture::root(0) + 0x38, uint32_t(0)), f.put(Fixture::root(0) + 0x40, uint32_t(0)), f.record(0, 0, 0x202, 7, 7);
        s = readPrimarySeat14(b, 0, f.read);
        check(s && !s->windowKnown && s->windowRead.stage == PrimaryWindow14::Kind && s->windowRead.records[0][0] == 0x202, "a record of another kind: told so");
        f.put(Fixture::operate(0) + 0x9d, uint8_t(1)), f.put(Fixture::operate(0) + 0xe0, 3.25f);
        s = readPrimarySeat14(b, 0, f.read);
        check(s && s->windowRead.stage == PrimaryWindow14::None, "a pair in the hands whose place is not read: the window is not asked");
    }
    {
        // When the seats are bound anew.
        const auto fault = [](uint8_t a, uint8_t b) {
            std::array<PrimaryFault14, 2> f;
            f[0].code = a, f[1].code = b;
            return f;
        };
        const uint8_t N = PrimaryFault14::None, O = PrimaryFault14::Objects, C = PrimaryFault14::Changing, P = PrimaryFault14::Place, B = PrimaryFault14::Board;
        {
            PrimaryRebind14 r;
            bool early = false;
            for (uint64_t t = 1000000; t <= 1150000; t += 1000) early = early || r.after(fault(O, O), t);
            check(!early && r.after(fault(O, O), 1151000), "both seats' objects gone: bound anew after 150 ms, not sooner");
        }
        {
            // (the real game, two rounds of eight: seat 0's objects made anew where they were, its board elsewhere)
            PrimaryRebind14 r;
            bool early = r.after(fault(O, O), 1000000);
            for (uint64_t t = 1050000; t <= 1150000; t += 1000) early = early || r.after(fault(B, O), t);
            check(!early && r.after(fault(B, O), 1151000), "one seat's board gone, the other's objects: bound anew after 150 ms all the same");
        }
        {
            PrimaryRebind14 r;
            bool any = false;
            for (uint64_t t = 1000000; t <= 7000000; t += 1000) any = any || r.after(fault(t % 3000 ? N : C, t % 5000 ? P : N), t);
            check(!any, "seats that are read, a seat changing as it is read now and then, the place of a pair not read: never");
            for (uint64_t t = 7001000; t <= 15000000; t += 1000) any = any || r.after(fault(C, C), t);
            check(!any && r.after(fault(C, C), 15002000), "neither seat read for another reason: after eight seconds");
        }
        {
            PrimaryRebind14 r;
            bool early = false;
            for (uint64_t t = 1000000; t <= 1100000; t += 1000) early = early || r.after(fault(B, O), t);
            early = early || r.after(fault(N, N), 1101000);
            for (uint64_t t = 1102000; t <= 1250000; t += 1000) early = early || r.after(fault(B, O), t);
            check(!early && r.after(fault(B, O), 1253000), "seats read again in between: the 150 ms begin anew");
        }
        {
            PrimaryRebind14 r;
            bool early = false;
            for (uint64_t t = 1000000; t <= 1150000; t += 1000) early = early || r.after(fault(N, B), t);
            check(!early && r.after(fault(N, B), 1151000), "one seat read, the other's board gone: bound anew after 150 ms");
            r.reset();
            bool again = false;
            for (uint64_t t = 1200000; t <= 9150000; t += 1000) again = again || r.after(fault(N, B), t);
            check(!again && r.after(fault(N, B), 9152000), "... and when that does not help, not again for eight seconds");
            r.reset();
            for (uint64_t t = 9200000; t <= 9350000; t += 1000) again = again || r.after(fault(O, C), t);
            check(!again && r.after(fault(O, C), 9351000), "... unless neither seat is read then");
        }
        {
            // On the made-up memory: a new round's objects, seat 0's where they were with its board elsewhere, seat 1's elsewhere.
            Fixture f;
            const auto b = f.bind();
            uint64_t clock = 1000000;
            const auto now = [&] { return clock; };
            PrimaryFollow14 follow;
            PrimaryRebind14 r;
            auto look = follow.look(b, 1, f.read, now);
            bool fired = r.after(look.fault, clock);
            for (uint64_t a = Fixture::top(0) - 64; a < Fixture::top(0) + 376; ++a) f.put(a, uint8_t(0));
            f.put(Fixture::player(1), uint64_t(0));
            int wholes = 0;
            for (int k = 0; k < 150 && !fired; ++k) {
                clock += 1000, look = follow.look(b, 1, f.read, now);
                wholes += look.whole;
                if (look.whole) fired = r.after(look.fault, clock);
            }
            check(!fired && wholes == 150 && look.fault[0].code == PrimaryFault14::Board && look.fault[1].code == PrimaryFault14::Objects && !follow.last(),
                  "a new round's objects: seat 0 reads as its board gone, seat 1 as its objects gone, in every look");
            clock += 2000, look = follow.look(b, 1, f.read, now);
            check(look.whole && r.after(look.fault, clock), "... and the seats are bound anew 150 ms after");
        }
        {
            const uint8_t F = PrimaryFault14::Flags;
            PrimaryRebind14 r;
            bool early = false;
            for (uint64_t t = 1000000; t <= 2000000; t += 1000) early = early || r.after(fault(N, F), t);
            check(!early && r.after(fault(N, F), 2001000), "one seat not read for its flags while the other is: bound anew after a second, not sooner");
            PrimaryRebind14 q;
            bool any = false;
            for (uint64_t t = 3000000; t <= 9000000; t += 1000) any = any || q.after(fault(N, (t / 1000) % 900 < 800 ? F : N), t);
            check(!any, "... flags that read right again within the second: never");
            PrimaryRebind14 u;
            early = false;
            for (uint64_t t = 1000000; t <= 2000000; t += 1000) early = early || u.after(fault(N, t % 3000 ? F : C), t);
            check(!early && u.after(fault(N, F), 2001000), "... a seat changing as it is read now and then in between: the second is not begun anew");
        }
    }
    {
        Fixture f;
        const auto binding = f.bind();
        const auto object = [](int s) { return Fixture::player(s) + 0x50000; };
        for (int s = 0; s < 2; ++s) {
            const uint64_t a = object(s);
            f.put(Fixture::player(s) + 0x88, a);
            f.put(a, f.process.imageBase + 0x2aac18);
            f.put(a + 0x20, Fixture::player(s)), f.put(a + 0x28, uint32_t(s)), f.put(a + 0x54, uint8_t(1));
            f.put(a + 0x2c, uint16_t(s ? 0x28 : 0)), f.put(a + 0x2e, uint16_t(s ? 0x28 : 0));
        }
        f.touched.clear();
        const auto off = readPrimarySeat14(binding, 0, f.read);
        check(primaryInputSeat14.load() == -1 && off && !off->input && !f.touched.count(object(0)), "既定では相手の入力オブジェクトを読まない");
        primaryInputSeat14 = 0;
        const auto released = readPrimarySeat14(binding, 0, f.read);
        const auto unselected = readPrimarySeat14(binding, 1, f.read);
        check(released && released->input == uint8_t(0) && !samePrimarySeat14(off, released) && unselected && !unselected->input &&
                  !f.touched.count(object(1)), "選んだ席の無入力と未取得を区別する");
        primaryInputSeat14 = 1;
        const auto held = readPrimarySeat14(binding, 1, f.read);
        check(held && held->input == uint8_t(0x28), "もう一方の席を選ぶとその席の複合入力を読む");
        const uint64_t a = object(1);
        int reads = 0;
        FieldRead14 changing = [&](uint64_t addr, void* out, size_t n) {
            if (addr == a + 0x20 && ++reads == 2) {
                f.put(a + 0x2c, uint16_t(8)), f.put(a + 0x2e, uint16_t(8));
            }
            return f.read(addr, out, n);
        };
        const auto changed = readPrimarySeat14(binding, 1, changing);
        check(reads == 2 && changed && !changed->input, "読み取り中に入力が変わった場合も通常の席情報を返す");
        const auto absent = [&]() { const auto s = readPrimarySeat14(binding, 1, f.read); return s && !s->input; };
        f.put(a + 0x2e, uint16_t(0));
        check(absent(), "入力更新が終わっていなければその値を使わない");
        f.put(a + 0x2c, uint16_t(0x108)), f.put(a + 0x2e, uint16_t(0x108));
        check(absent(), "8ビットを超える論理入力を採らない");
        f.put(a + 0x2c, uint16_t(8)), f.put(a + 0x2e, uint16_t(8)), f.put(a + 0x54, uint8_t(0));
        check(absent(), "無効化された入力は読めない");
        f.put(a + 0x54, uint8_t(1)), f.put(a + 0x20, Fixture::player(0));
        check(absent(), "親プレイヤーが別席なら採らない");
        f.put(a + 0x20, Fixture::player(1)), f.put(a + 0x28, uint32_t(0));
        check(absent(), "入力オブジェクトの席が違えば採らない");
        f.put(a + 0x28, uint32_t(1)), f.put(a, f.process.imageBase + 0x2aac20);
        check(absent(), "型が異なるオブジェクトから入力を読まない");
        auto context = binding.context;
        ++context.process.image.timeStamp;
        check(!readPrimaryInput14(context, Fixture::player(1), 1, f.read), "対応する実行ファイルの版だけで入力を読む");
        primaryInputSeat14 = -1;
    }
    {
        // The mark of the all-clear bonus (the byte at 0xd0 of the field object): 0 or 1, another number not taken.
        Fixture f;
        const auto b = f.bind();
        const auto none = readPrimarySeat14(b, 0, f.read);
        f.put(Fixture::field(0) + 0xd0, uint8_t(1));
        const auto waits = readPrimarySeat14(b, 0, f.read);
        f.put(Fixture::field(0) + 0xd0, uint8_t(2));
        const auto other = readPrimarySeat14(b, 0, f.read);
        check(none && none->bonusKnown && !none->bonusWaits && waits && waits->bonusKnown && waits->bonusWaits && other && !other->bonusKnown,
              "the mark of the all-clear bonus: 0 and 1 read, another number not taken");
        check(!samePrimarySeat14(none, waits), "... and a mark that changes is another sample");
    }
    {
        Fixture f;
        const auto b = f.bind();
        f.put(Fixture::player(0) + 0x58, uint32_t(0));
        f.put(Fixture::player(1) + 0x58, uint32_t(0xc5d95c6b));
        f.put(Fixture::player(0) + 0xf8, uint8_t(4)), f.put(Fixture::field(0) + 0x1e8, uint8_t(5));
        f.touched.clear();
        const auto off = readPrimarySeat14(b, 0, f.read);
        check(off && !off->garbageCursor && !f.touched.count(Fixture::player(0) + 0xf8) && !f.touched.count(Fixture::field(0) + 0x1e8),
              "the rule and cursor are not read while random state is off");
        check(!primaryGarbageRandom14.load() && off && !off->garbageRandom, "seat random state is off by default");
        primaryGarbageRandom14 = true;
        const auto zero = readPrimarySeat14(b, 0, f.read), other = readPrimarySeat14(b, 1, f.read);
        check(zero && zero->garbageRandom == uint32_t(0) && other && other->garbageRandom == uint32_t(0xc5d95c6b), "each seat has its own state and zero is valid");
        const std::array<uint8_t, juraic::W> fromZero{1, 0, 3, 5, 4, 2}, fromRecord{4, 5, 2, 1, 0, 3};
        check(zero && zero->garbageCursor == uint8_t(5) && primaryGarbageOrder14(0, 5) == fromZero &&
                  primaryGarbageOrder14(0xc5d95c6bu, 0) == fromRecord, "column order from the state and cursor");
        f.put(Fixture::player(0) + 0xf8, uint8_t(0));
        auto absent = readPrimarySeat14(b, 0, f.read);
        check(absent && absent->garbageRandom && !absent->garbageCursor, "another garbage rule gives no forecast");
        f.put(Fixture::player(0) + 0xf8, uint8_t(4)), f.put(Fixture::field(0) + 0x1e8, uint8_t(6));
        absent = readPrimarySeat14(b, 0, f.read);
        check(absent && !absent->garbageCursor, "a cursor outside the six columns gives no forecast");
        for (const uint64_t address : {Fixture::player(0) + 0xf8, Fixture::field(0) + 0x1e8}) {
            int reads = 0;
            f.put(Fixture::player(0) + 0xf8, uint8_t(4)), f.put(Fixture::field(0) + 0x1e8, uint8_t(5));
            const FieldRead14 changing = [&](uint64_t addr, void* out, size_t n) {
                if (addr == address && n == 1 && ++reads == 2) f.put(addr, uint8_t(0));
                return f.read(addr, out, n);
            };
            absent = readPrimarySeat14(b, 0, changing);
            check(absent && absent->garbageRandom && !absent->garbageCursor && !samePrimarySeat14(absent, zero),
                  "a changed cursor or rule withholds only the forecast");
        }
        f.put(Fixture::field(0) + 0x1e8, uint8_t(5));
        check(!samePrimarySeat14(off, zero) && samePrimarySeat14(zero, readPrimarySeat14(b, 0, f.read)), "a known zero differs from an unknown state");
        for (const int changeAt : {2, 3, 4}) {
            int copies = 0;
            f.put(Fixture::player(0) + 0x58, uint32_t(0));
            const FieldRead14 torn = [&](uint64_t addr, void* out, size_t n) {
                if (addr == Fixture::player(0) && n == 160 && ++copies == changeAt) f.put(addr + 0x58, uint32_t(7));
                return f.read(addr, out, n);
            };
            const auto changed = readPrimarySeat14(b, 0, torn);
            check(copies == 4 && changed && !changed->garbageRandom, "a changing state is withheld while the public seat remains available");
        }
        const auto settled = readPrimarySeat14(b, 0, f.read);
        check(settled && settled->garbageRandom == uint32_t(7) && !samePrimarySeat14(zero, settled), "a later stable state is another sample");
        primaryGarbageRandom14 = false;
    }
    {
        Fixture f;
        const auto b = f.bind();
        const uint64_t player = Fixture::player(0), obj = player + 0x50000;
        f.put(player + 0xc0, obj), f.put(obj, f.process.imageBase + 0x2afbd0), f.put(obj + 8, player);
        const auto zero = readPrimarySeat14(b, 0, f.read);
        check(zero && zero->score && zero->score->total == 0, "zero is a known score");
        f.put(obj + 0x3c, uint32_t(38554)), f.put(obj + 0x40, uint32_t(54)), f.put(obj + 0x44, uint32_t(8960));
        auto shown = readPrimarySeat14(b, 0, f.read);
        check(shown && shown->score && shown->score->total == 38554 && shown->score->pool == 54 && shown->score->lastWave == 8960 &&
              !samePrimarySeat14(zero, shown), "the total, unconverted points and last wave of the player's score");
        f.put(obj + 8, Fixture::player(1));
        shown = readPrimarySeat14(b, 0, f.read);
        check(shown && !shown->score, "an object of the other seat gives no score");
        f.put(obj + 8, player), f.put(obj, f.process.imageBase + 0x2afbb0);
        shown = readPrimarySeat14(b, 0, f.read);
        check(shown && !shown->score, "another score interface gives no score");
        f.put(obj, f.process.imageBase + 0x2afbd0), f.put(obj + 0x3c, uint32_t(100000000));
        shown = readPrimarySeat14(b, 0, f.read);
        check(shown && !shown->score, "an impossible score leaves the seat available");
        f.gone.insert(obj);
        shown = readPrimarySeat14(b, 0, f.read);
        check(shown && !shown->score, "an unreadable score leaves the seat available");
    }
    {
        // The pairs beyond the window, from the game's own list: read only while primaryListedPairs14 is set.
        Fixture f;
        const auto b = f.bind();
        const uint64_t lists = Fixture::player(0) + 0x50000, image = f.process.imageBase;
        f.put(Fixture::root(0) + 0x30, lists), f.put(lists, image + 0x2aa128);
        for (int i = 0; i < 16; ++i) f.put(Fixture::root(0) + 0x44 + 4 * uint64_t(i), uint32_t(0x201));
        f.put(Fixture::root(0) + 0x84, uint8_t(5)), f.put(lists + 0x20, uint8_t(3)), f.put(lists + 0x21, uint8_t(5));
        const uint32_t colours[5] = {2, 4, 3, 1, 5};
        for (int i = 0; i < 5; ++i) f.put(lists + 0x24 + 4 * uint64_t(i), colours[i]);
        // (the list for five colours: the window's pairs 19..21 - 2 5, 1 3, 4 4 -, then the pair 22 as 4 3; the rest 2 2)
        const uint8_t window[6] = {0, 4, 3, 2, 1, 1};
        for (int i = 0; i < 6; ++i) f.put(lists + 0x238 + 38 + uint64_t(i), window[i]);
        f.put(lists + 0x238 + 44, uint8_t(1)), f.put(lists + 0x238 + 45, uint8_t(2));
        check(!primaryListedPairs14.load(), "the pairs beyond the window are not read unless asked for");
        f.touched.clear();
        auto s = readPrimarySeat14(b, 0, f.read);
        bool looked = false;
        for (uint64_t a = lists; a < lists + 0x338; ++a) looked = looked || f.touched.count(a);
        check(s && s->pairsKnown && !s->listedKnown && !looked && !f.touched.count(Fixture::root(0) + 0x30), "... and then nothing of the game's list is read");
        primaryListedPairs14 = true;
        s = readPrimarySeat14(b, 0, f.read);
        check(s && s->listedKnown && s->listed[0].axis == juraic::Cell(4) && s->listed[0].child == juraic::Cell(3) && s->listed[1].axis == juraic::Cell(2) &&
                  s->listed[7].child == juraic::Cell(2),
              "asked for: the pairs after the window's three, by the list the window's pairs stand in");
        check(samePrimarySeat14(s, readPrimarySeat14(b, 0, f.read)), "... the same read twice");
        f.put(lists + 0x238 + 39, uint8_t(3));
        s = readPrimarySeat14(b, 0, f.read);
        check(s && s->pairsKnown && !s->listedKnown, "the window's pairs stand in no list: the pairs beyond are not known");
        f.put(lists + 0x238 + 39, uint8_t(4)), f.put(lists + 0x24 + 4, uint32_t(2));
        s = readPrimarySeat14(b, 0, f.read);
        check(s && !s->listedKnown, "a colour named twice: not known");
        f.put(lists + 0x24 + 4, uint32_t(4)), f.put(lists, image + 0x2aa130);
        s = readPrimarySeat14(b, 0, f.read);
        check(s && !s->listedKnown, "another object where the lists are looked for: not known");
        f.put(lists, image + 0x2aa128), f.put(Fixture::field(0) + 0x108, uint32_t(500));
        s = readPrimarySeat14(b, 0, f.read);
        check(s && s->listedKnown, "... and known again with the object as it was");
        // (the window's pairs at 117..119: the pairs 120..127 are the last of the list)
        for (int i = 0; i < 6; ++i) f.put(lists + 0x238 + 234 + uint64_t(i), window[i]);
        f.put(Fixture::operate(0) + 0xa0, uint32_t(117));
        s = readPrimarySeat14(b, 0, f.read);
        check(s && s->pose.lockedPairs == 117 && s->listedKnown && s->listed[7].axis == juraic::Cell(2), "the pairs beyond the window up to the 128th of the list");
        // (at 118 the eight go round to the first pair of the list: 3 1)
        f.put(lists + 0x238, uint8_t(2)), f.put(lists + 0x238 + 1, uint8_t(3));
        for (int i = 0; i < 6; ++i) f.put(lists + 0x238 + 236 + uint64_t(i), window[i]);
        f.put(Fixture::operate(0) + 0xa0, uint32_t(118));
        s = readPrimarySeat14(b, 0, f.read);
        check(s && s->listedKnown && s->listed[7].axis == juraic::Cell(3) && s->listed[7].child == juraic::Cell(1), "... and beyond it the list goes round");
        // (the window's pairs at 126, 127 and 0 of the list: the window too is looked for round the end)
        for (int i = 0; i < 4; ++i) f.put(lists + 0x238 + 252 + uint64_t(i), window[i]);
        f.put(lists + 0x238, window[4]), f.put(lists + 0x238 + 1, window[5]), f.put(lists + 0x238 + 2, uint8_t(4)), f.put(lists + 0x238 + 3, uint8_t(0));
        f.put(Fixture::operate(0) + 0xa0, uint32_t(126));
        s = readPrimarySeat14(b, 0, f.read);
        check(s && s->listedKnown && s->listed[0].axis == juraic::Cell(5) && s->listed[0].child == juraic::Cell(2), "the window's pairs round the end of the list: the pairs after them from its start");
        for (const uint64_t off : {0xb8u, 0x138u, 0x148u}) {
            f.put(Fixture::root(0) + off, uint8_t(1));
            s = readPrimarySeat14(b, 0, f.read);
            check(s && s->pairsKnown && !s->listedKnown, "a regenerated list or scripted colours/shapes cannot be extrapolated");
            f.put(Fixture::root(0) + off, uint8_t(0));
        }
        f.put(Fixture::root(0) + 0x80, uint32_t(0x301));
        s = readPrimarySeat14(b, 0, f.read);
        check(s && s->pairsKnown && !s->listedKnown, "another shape in the cycle prevents indexing by two pieces per pair");
        f.put(Fixture::root(0) + 0x80, uint32_t(0x201)), f.put(Fixture::root(0) + 0x84, uint8_t(4));
        s = readPrimarySeat14(b, 0, f.read);
        check(s && s->pairsKnown && !s->listedKnown, "a matching window in an inactive colour table gives no future pairs");
        f.put(Fixture::root(0) + 0x84, uint8_t(5));
        f.put(lists + 0x238, uint8_t(0));
        s = readPrimarySeat14(b, 0, f.read);
        check(s && s->pairsKnown && !s->listedKnown, "... and nothing when the list does not have them so");
        primaryListedPairs14 = false;
    }
    std::printf("memory first: %d checks, %d failures\n", checks, failed);
    return failed ? 1 : 0;
}
