// Display helpers for the launcher / settings menu: the monitor list with each
// one's native (desktop) resolution, the automatic render resolution, and
// moving or resizing the game window live.
//
// They work on the SDL window the SDK created (there is one); the SDK window
// follows SDL's move/resize/fullscreen events on its own.

#pragma once

#include <string>
#include <vector>

namespace recomp_ui::display {

struct Monitor {
  std::string name;
  int width = 0, height = 0;  // desktop mode in pixels
};

// In SDL enumeration order, which is what the SDK's 1-based `monitor` cvar uses.
std::vector<Monitor> ListMonitors();

// Desktop resolution of the monitor the game window is on (primary display if
// there is no window yet).
bool NativeResolution(int& width, int& height);

// resolution_scale (1-3: 720p, 1440p, 2160p) closest to the native height.
// 1080p rounds up to 2x (supersampled), 800p (Steam Deck) stays at 1x.
int AutoRenderScale();

// recomp_render_auto on: set resolution_scale from AutoRenderScale(). Call
// before the runtime is constructed (the GPU reads the scale at setup).
void ApplyAutoRenderScale();

// Move the window to `monitor` (1-based, 0 = primary), keeping fullscreen.
void MoveToMonitor(int monitor);

// Windowed mode only: resize and re-centre the window.
void ResizeWindow(int width, int height);

}  // namespace recomp_ui::display
