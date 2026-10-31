// Display helpers - see display_setup.h.

#include "display_setup.h"

#include <algorithm>
#include <cmath>

#include <SDL3/SDL.h>

#include <rex/cvar.h>
#include <rex/logging.h>

// Render resolution follows the monitor's native resolution (resolution_scale
// is then written at every start); off = resolution_scale as set.
REXCVAR_DEFINE_BOOL(recomp_render_auto, true, "Recomp",
                    "Pick the render resolution from the monitor's native resolution");

namespace recomp_ui::display {
namespace {

SDL_Window* GameWindow() {
  int count = 0;
  SDL_Window** windows = SDL_GetWindows(&count);
  SDL_Window* w = (windows && count > 0) ? windows[0] : nullptr;
  SDL_free(windows);
  return w;
}

SDL_DisplayID DisplayForMonitor(int monitor) {
  SDL_DisplayID id = SDL_GetPrimaryDisplay();
  int count = 0;
  SDL_DisplayID* displays = SDL_GetDisplays(&count);
  if (displays && monitor > 0 && monitor <= count) id = displays[monitor - 1];
  SDL_free(displays);
  return id;
}

}  // namespace

std::vector<Monitor> ListMonitors() {
  std::vector<Monitor> out;
  int count = 0;
  SDL_DisplayID* displays = SDL_GetDisplays(&count);
  for (int i = 0; displays && i < count; i++) {
    Monitor m;
    const char* name = SDL_GetDisplayName(displays[i]);
    m.name = name ? name : "Display";
    if (const SDL_DisplayMode* mode = SDL_GetDesktopDisplayMode(displays[i])) {
      m.width = int(std::lround(mode->w * mode->pixel_density));
      m.height = int(std::lround(mode->h * mode->pixel_density));
    }
    out.push_back(std::move(m));
  }
  SDL_free(displays);
  return out;
}

bool NativeResolution(int& width, int& height) {
  SDL_Window* w = GameWindow();
  SDL_DisplayID id = w ? SDL_GetDisplayForWindow(w) : 0;
  if (!id) id = SDL_GetPrimaryDisplay();
  const SDL_DisplayMode* mode = id ? SDL_GetDesktopDisplayMode(id) : nullptr;
  if (!mode) return false;
  width = int(std::lround(mode->w * mode->pixel_density));
  height = int(std::lround(mode->h * mode->pixel_density));
  return width > 0 && height > 0;
}

int AutoRenderScale() {
  int w = 0, h = 0;
  if (!NativeResolution(w, h)) return 1;
  return std::clamp(int(std::lround(h / 720.0)), 1, 3);
}

void ApplyAutoRenderScale() {
  if (!rex::cvar::Query<bool>("recomp_render_auto")) return;
  int scale = AutoRenderScale();
  rex::cvar::SetFlagByName("resolution_scale", std::to_string(scale));
  int w = 0, h = 0;
  NativeResolution(w, h);
  REXLOG_INFO("Render resolution: auto, {}x (monitor {}x{})", scale, w, h);
}

void MoveToMonitor(int monitor) {
  SDL_Window* w = GameWindow();
  if (!w) return;
  SDL_DisplayID id = DisplayForMonitor(monitor);
  // A fullscreen window stays on its display: leave fullscreen, move, re-enter.
  const bool fs = (SDL_GetWindowFlags(w) & SDL_WINDOW_FULLSCREEN) != 0;
  if (fs) {
    SDL_SetWindowFullscreen(w, false);
    SDL_SyncWindow(w);
  }
  SDL_SetWindowPosition(w, SDL_WINDOWPOS_CENTERED_DISPLAY(id), SDL_WINDOWPOS_CENTERED_DISPLAY(id));
  SDL_SyncWindow(w);
  if (fs) SDL_SetWindowFullscreen(w, true);
}

void ResizeWindow(int width, int height) {
  SDL_Window* w = GameWindow();
  if (!w || (SDL_GetWindowFlags(w) & SDL_WINDOW_FULLSCREEN)) return;
  SDL_DisplayID id = SDL_GetDisplayForWindow(w);
  SDL_SetWindowSize(w, width, height);
  if (id) {
    SDL_SetWindowPosition(w, SDL_WINDOWPOS_CENTERED_DISPLAY(id),
                          SDL_WINDOWPOS_CENTERED_DISPLAY(id));
  }
}

}  // namespace recomp_ui::display
