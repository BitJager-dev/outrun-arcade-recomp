// Quitting from the game's own menu.
//
// OutRun exits through XLaunchNewImage (sub_8227EB20, called only from
// sub_82193BE0), which calls XamLoaderLaunchTitle - on a 360, back to the
// dashboard. The SDK answers with KernelState::TerminateTitle: guest threads
// get 200 ms to stop and stragglers are left running, but the app only closes
// when the game's main thread ends. That thread doesn't, so the window stayed
// open on a frozen game (the save it writes just before is fine).
//
// Instead, close the window like the close button does: the SDK then
// terminates the title and hard-exits the process. The calling guest thread
// is parked until then.

#include "generated/default/outrun_arcade_init.h"

#include <chrono>
#include <functional>
#include <thread>
#include <utility>

#include <rex/logging.h>

namespace {
std::function<void()> g_quit;
}  // namespace

void SetTitleExitHandler(std::function<void()> quit) { g_quit = std::move(quit); }

extern "C" void __imp__sub_8227EB20(PPCContext&, uint8_t*);
extern "C" void sub_8227EB20(PPCContext& ctx, uint8_t* base) {
  if (!g_quit) {
    __imp__sub_8227EB20(ctx, base);
    return;
  }
  REXLOG_INFO("Game asked to exit (XLaunchNewImage): closing the window");
  g_quit();
  for (;;) std::this_thread::sleep_for(std::chrono::seconds(1));
}
