// Tests of the client of the driver ViGEmBus (third_party/ViGEmClient), which the mode of the real game plugs its pad in
// with (play/realgame_pad_win.h: vigem_alloc, vigem_connect, ..., vigem_disconnect, vigem_free each time). The driver is
// not called (vigem_connect is left out: it can wait on the machine's devices): a client allocated, disconnected and
// freed again and again keeps no handle - vigem_disconnect leaves the client the event vigem_free closes.
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <ViGEm/Client.h>
#include <cstdio>

static void cycle() {
    const PVIGEM_CLIENT client = vigem_alloc();
    if (!client) return;
    vigem_disconnect(client);
    vigem_free(client);
}

int main() {
    for (int i = 0; i < 10; ++i) cycle(); // (what the first calls keep for good)
    DWORD before = 0, after = 0;
    GetProcessHandleCount(GetCurrentProcess(), &before);
    constexpr int CYCLES = 1000;
    for (int i = 0; i < CYCLES; ++i) cycle();
    GetProcessHandleCount(GetCurrentProcess(), &after);
    const long kept = long(after) - long(before);
    std::printf("%d clients allocated, disconnected and freed: %ld handles more than before\n", CYCLES, kept);
    if (kept > 20) {
        std::printf("FAILED: a client disconnected and freed keeps a handle\n");
        return 1;
    }
    std::printf("vigem client tests passed\n");
    return 0;
}
