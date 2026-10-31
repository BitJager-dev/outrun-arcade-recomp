// In-game settings menu - see game_menu.h. Per-game key layout: game_keys.h.

#include "game_menu.h"
#include "display_setup.h"
#include "game_keys.h"

#include <algorithm>
#include <cstdio>
#include <string>
#include <utility>
#include <vector>

#include <SDL3/SDL.h>
#include <imgui.h>

#include <rex/cvar.h>
#include <rex/logging.h>

// Set once the first-launch defaults (windowed, keyboard on, key layout) are written.
REXCVAR_DEFINE_BOOL(recomp_defaults_applied, false, "Recomp",
                    "First-launch defaults have been applied");
// Bumped when a newer build adds a default (2 = per-game audio buffer).
REXCVAR_DEFINE_INT32(recomp_defaults_version, 0, "Recomp", "Last applied defaults version");
// Settings screen before the game starts. --recomp_launcher=false skips it once.
REXCVAR_DEFINE_BOOL(recomp_launcher, true, "Recomp", "Show the launcher before the game starts");

namespace recomp_ui {
namespace {

namespace cvar = rex::cvar;

// ------------------------------------------------------------------ colours
// Same palette as the ReBirth recomp menus.
const ImU32 kWhite = IM_COL32(255, 255, 255, 255);
const ImU32 kGrey = IM_COL32(178, 178, 191, 255);
const ImU32 kGold = IM_COL32(255, 217, 77, 255);
const ImU32 kPanel = IM_COL32(13, 18, 38, 224);
const ImU32 kHighlight = IM_COL32(64, 89, 178, 230);
const ImU32 kShade = IM_COL32(0, 0, 0, 115);

// ------------------------------------------------------------------ cvar helpers
bool GetBool(const char* n) { return cvar::Query<bool>(n); }
int GetInt(const char* n) { return cvar::Query<int32_t>(n); }
std::string GetStr(const char* n) { return cvar::Query<std::string>(n); }
void SetBool(const char* n, bool v) { cvar::SetFlagByName(n, v ? "true" : "false"); }
void SetInt(const char* n, int v) { cvar::SetFlagByName(n, std::to_string(v)); }
void SetStr(const char* n, const std::string& v) { cvar::SetFlagByName(n, v); }

// ------------------------------------------------------------------ main page
enum MainRow {
  kStart,
  kDisplayMode,
  kMonitor,
  kWindowSize,
  kRenderRes,
  kAspect,
  kFiltering,
  kAudioBuffer,
  kKeyboard,
  kLauncher,
  kEditKeys,
  kCloseMenu,
  kQuit,
  kMainRowCount
};

struct WindowSize {
  int w, h;
  const char* label;
};
const WindowSize kWindowSizes[] = {
    {1280, 720, "1280 x 720"},
    {1600, 900, "1600 x 900"},
    {1920, 1080, "1920 x 1080"},
    {2560, 1440, "2560 x 1440"},
};
constexpr int kWindowSizeCount = 4;

// anisotropic_override values and their labels
const int kAnisoValues[] = {-1, 0, 2, 3, 4, 5};
const char* const kAnisoLabels[] = {"Game default", "Off", "2x", "4x", "8x", "16x"};
constexpr int kAnisoCount = 6;

// audio_maxqframes values and their labels
const int kAudioFrames[] = {8, 16, 32, 64};
const char* const kAudioLabels[] = {"Small (8)", "Medium (16)", "Large (32)", "Max (64)"};
constexpr int kAudioCount = 4;

int AudioIndex() {
  int v = GetInt("audio_maxqframes"), best = 0;
  for (int i = 0; i < kAudioCount; i++) {
    if (kAudioFrames[i] <= v) best = i;
  }
  return best;
}

const char* const kRenderLabels[] = {"1x  1280x720 (original)", "2x  2560x1440", "3x  3840x2160"};

// Render resolution choices: 0 = auto (from the monitor), 1-3 = fixed scale.
int RenderIndex() {
  return GetBool("recomp_render_auto") ? 0 : std::clamp(GetInt("resolution_scale"), 1, 3);
}

std::string MonitorValue() {
  const int m = GetInt("monitor");
  const auto monitors = display::ListMonitors();
  if (m <= 0 || m > int(monitors.size())) return "Primary";
  const auto& mon = monitors[m - 1];
  return std::to_string(m) + ": " + mon.name + "  " + std::to_string(mon.width) + "x" +
         std::to_string(mon.height);
}

const char* MainLabel(int row) {
  switch (row) {
    case kStart: return "Start game";
    case kDisplayMode: return "Display mode";
    case kMonitor: return "Monitor";
    case kWindowSize: return "Window size";
    case kRenderRes: return "Render resolution";
    case kAspect: return "Aspect ratio";
    case kFiltering: return "Texture filtering";
    case kAudioBuffer: return "Audio buffer";
    case kKeyboard: return "Keyboard controls";
    case kLauncher: return "Launcher at start";
    case kEditKeys: return "Edit keyboard keys";
    case kCloseMenu: return "Close menu";
    case kQuit: return "Quit game";
  }
  return "";
}

const char* MainNote(int row, bool launcher) {
  switch (row) {
    case kStart: return "Start OutRun with these settings.";
    case kDisplayMode: return "Fullscreen uses the monitor's own resolution. F11 also switches.";
    case kMonitor: return "Which monitor the game shows on.";
    case kWindowSize: return "Size of the window in windowed mode.";
    case kRenderRes:
      return launcher ? "Internal rendering resolution. Auto matches your monitor."
                      : "Internal rendering resolution. Applies next launch.";
    case kAspect: return "Keep 16:9 adds black bars on other window shapes.";
    case kFiltering: return "Sharper textures seen at an angle.";
    case kAudioBuffer:
      return launcher ? "Bigger fixes crackles and dropouts, smaller has less delay."
                      : "Bigger fixes crackles and dropouts, smaller has less delay. Next launch.";
    case kKeyboard: return "Play with the keyboard as controller 1.";
    case kLauncher: return "Show this screen before the game. Off = straight into the game.";
    case kEditKeys: return "Choose which key presses each controller button.";
  }
  return nullptr;
}

int WindowSizeIndex() {
  int w = GetInt("window_width"), h = GetInt("window_height");
  for (int i = 0; i < kWindowSizeCount; i++) {
    if (kWindowSizes[i].w == w && kWindowSizes[i].h == h) return i;
  }
  return 0;  // 0 x 0 = SDK default, which is 1280 x 720
}

int AnisoIndex() {
  int v = GetInt("anisotropic_override");
  for (int i = 0; i < kAnisoCount; i++) {
    if (kAnisoValues[i] == v) return i;
  }
  return 0;
}

// Value text for an option row, or nullptr for an action row.
std::string MainValue(int row) {
  switch (row) {
    case kDisplayMode: return GetBool("fullscreen") ? "Fullscreen" : "Windowed";
    case kMonitor: return MonitorValue();
    case kWindowSize: return kWindowSizes[WindowSizeIndex()].label;
    case kRenderRes: {
      int i = RenderIndex();
      if (i > 0) return kRenderLabels[i - 1];
      return std::string("Auto  (") + kRenderLabels[display::AutoRenderScale() - 1] + ")";
    }
    case kAspect: return GetBool("present_letterbox") ? "Keep 16:9" : "Stretch to window";
    case kFiltering: return kAnisoLabels[AnisoIndex()];
    case kAudioBuffer: return kAudioLabels[AudioIndex()];
    case kKeyboard: return GetBool("mnk_mode") ? "On" : "Off";
    case kLauncher: return GetBool("recomp_launcher") ? "On" : "Off";
  }
  return {};
}

bool IsOptionRow(int row) { return row >= kDisplayMode && row <= kLauncher; }

// ------------------------------------------------------------------ keys page
using game_keys::KeyBind;
using game_keys::kKeyBinds;
constexpr int kKeyBindCount = sizeof(kKeyBinds) / sizeof(kKeyBinds[0]);
// Rows after the binds.
constexpr int kKeysReset = kKeyBindCount;
constexpr int kKeysBack = kKeyBindCount + 1;
constexpr int kKeysRowCount = kKeyBindCount + 2;

// Bindable keys: ReXGlue key name <-> ImGui key. F-keys, Esc and the backtick
// are left out because the menu and the SDK overlays use them.
struct KeyName {
  ImGuiKey key;
  const char* name;
};
const std::vector<KeyName>& BindableKeys() {
  static const std::vector<KeyName> keys = [] {
    std::vector<KeyName> v;
    static const char* const kLetters[] = {"A", "B", "C", "D", "E", "F", "G", "H", "I",
                                           "J", "K", "L", "M", "N", "O", "P", "Q", "R",
                                           "S", "T", "U", "V", "W", "X", "Y", "Z"};
    for (int i = 0; i < 26; i++) v.push_back({ImGuiKey(ImGuiKey_A + i), kLetters[i]});
    static const char* const kDigits[] = {"0", "1", "2", "3", "4", "5", "6", "7", "8", "9"};
    for (int i = 0; i < 10; i++) v.push_back({ImGuiKey(ImGuiKey_0 + i), kDigits[i]});
    v.insert(v.end(), {
                          {ImGuiKey_UpArrow, "Up"},
                          {ImGuiKey_DownArrow, "Down"},
                          {ImGuiKey_LeftArrow, "Left"},
                          {ImGuiKey_RightArrow, "Right"},
                          {ImGuiKey_Enter, "Return"},
                          {ImGuiKey_Space, "Space"},
                          {ImGuiKey_Tab, "Tab"},
                          {ImGuiKey_Backspace, "Backspace"},
                          {ImGuiKey_LeftShift, "Shift"},
                          {ImGuiKey_LeftCtrl, "Control"},
                          {ImGuiKey_LeftAlt, "Alt"},
                          {ImGuiKey_Insert, "Insert"},
                          {ImGuiKey_Delete, "Delete"},
                          {ImGuiKey_Home, "Home"},
                          {ImGuiKey_End, "End"},
                          {ImGuiKey_PageUp, "PageUp"},
                          {ImGuiKey_PageDown, "PageDown"},
                          {ImGuiKey_Minus, "Minus"},
                          {ImGuiKey_Equal, "Plus"},
                          {ImGuiKey_LeftBracket, "LBracket"},
                          {ImGuiKey_RightBracket, "RBracket"},
                      });
    return v;
  }();
  return keys;
}

std::string KeyDisplay(const std::string& name) {
  if (name.empty()) return "(none)";
  if (name == "Return") return "Enter";
  return name;
}

// ------------------------------------------------------------------ drawing helpers
struct Painter {
  ImDrawList* dl;
  ImFont* font;
  float size;

  float TextWidth(const char* s, float scale = 1.0f) const {
    return font->CalcTextSizeA(size * scale, FLT_MAX, 0.0f, s).x;
  }
  void Text(float x, float y, const char* s, ImU32 col, float scale = 1.0f) const {
    dl->AddText(font, size * scale, ImVec2(x, y), col, s);
  }
  void Rect(float x, float y, float w, float h, ImU32 col) const {
    dl->AddRectFilled(ImVec2(x, y), ImVec2(x + w, y + h), col);
  }
};

bool RowClicked(float x, float y, float w, float h, int mouse_button) {
  return ImGui::IsMouseHoveringRect(ImVec2(x, y), ImVec2(x + w, y + h)) &&
         ImGui::IsMouseClicked(mouse_button);
}

}  // namespace

// ================================================================== GameMenu

GameMenu::GameMenu(rex::ui::ImGuiDrawer* drawer, MenuHost host)
    : ImGuiDialog(drawer), host_(std::move(host)) {
  if (host_.launcher) rows_.push_back(kStart);
  rows_.push_back(kDisplayMode);
  if (display::ListMonitors().size() > 1) rows_.push_back(kMonitor);
  for (int r : {kWindowSize, kRenderRes, kAspect, kFiltering, kAudioBuffer, kKeyboard,
                kLauncher, kEditKeys}) {
    rows_.push_back(r);
  }
  if (!host_.launcher) rows_.push_back(kCloseMenu);
  rows_.push_back(kQuit);
  // Gamepads for menu navigation (the SDK's input system only opens them once
  // the game runs). Init/open are reference counted, so this doesn't disturb it.
  SDL_InitSubSystem(SDL_INIT_GAMEPAD);
}

GameMenu::~GameMenu() {
  for (SDL_Gamepad* g : gamepads_) SDL_CloseGamepad(g);
  SDL_QuitSubSystem(SDL_INIT_GAMEPAD);
}

bool GameMenu::LauncherEnabled() { return GetBool("recomp_launcher"); }

void GameMenu::PollGamepads() {
  int count = 0;
  SDL_JoystickID* ids = SDL_GetGamepads(&count);
  for (int i = 0; ids && i < count; i++) {
    if (!SDL_GetGamepadFromID(ids[i])) {
      if (SDL_Gamepad* g = SDL_OpenGamepad(ids[i])) gamepads_.push_back(g);
    }
  }
  SDL_free(ids);

  bool dir[4] = {}, accept = false, back = false;  // up, down, left, right
  constexpr Sint16 kDead = 16000;
  for (SDL_Gamepad* g : gamepads_) {
    if (!SDL_GamepadConnected(g)) continue;
    const Sint16 x = SDL_GetGamepadAxis(g, SDL_GAMEPAD_AXIS_LEFTX);
    const Sint16 y = SDL_GetGamepadAxis(g, SDL_GAMEPAD_AXIS_LEFTY);
    dir[0] |= SDL_GetGamepadButton(g, SDL_GAMEPAD_BUTTON_DPAD_UP) || y < -kDead;
    dir[1] |= SDL_GetGamepadButton(g, SDL_GAMEPAD_BUTTON_DPAD_DOWN) || y > kDead;
    dir[2] |= SDL_GetGamepadButton(g, SDL_GAMEPAD_BUTTON_DPAD_LEFT) || x < -kDead;
    dir[3] |= SDL_GetGamepadButton(g, SDL_GAMEPAD_BUTTON_DPAD_RIGHT) || x > kDead;
    accept |= SDL_GetGamepadButton(g, SDL_GAMEPAD_BUTTON_SOUTH) ||
              SDL_GetGamepadButton(g, SDL_GAMEPAD_BUTTON_START);
    back |= SDL_GetGamepadButton(g, SDL_GAMEPAD_BUTTON_EAST);
  }

  // Directions: press, then repeat after 0.4 s every 0.12 s while held.
  pad_ = {};
  int held = -1;
  for (int i = 0; i < 4; i++) {
    if (dir[i]) { held = i; break; }
  }
  const double now = ImGui::GetTime();
  bool fire = false;
  if (held != pad_held_) {
    fire = held >= 0;
    pad_next_ = now + 0.4;
  } else if (held >= 0 && now >= pad_next_) {
    fire = true;
    pad_next_ = now + 0.12;
  }
  pad_held_ = held;
  if (fire) {
    pad_.up = held == 0;
    pad_.down = held == 1;
    pad_.left = held == 2;
    pad_.right = held == 3;
  }
  pad_.accept = accept && !pad_was_[0];
  pad_.back = back && !pad_was_[1];
  pad_was_[0] = accept;
  pad_was_[1] = back;
}

void GameMenu::OnClose() {
  if (host_.on_closed) host_.on_closed();
}

void GameMenu::ApplyDefaultKeys() {
  for (const auto& b : kKeyBinds) SetStr(b.cvar, b.default_key);
  // Anything the game layout doesn't use is unbound so the SDK's FPS-style
  // defaults (WASD stick, mouse buttons) can't collide with it.
  for (const char* n : game_keys::kUnbound) SetStr(n, "");
}

void GameMenu::Save() {
  if (!host_.config_path.empty()) {
    cvar::SaveConfig(host_.config_path);
  }
}

void GameMenu::Change(int row, int dir) {
  switch (row) {
    case kDisplayMode: {
      bool fs = !GetBool("fullscreen");
      SetBool("fullscreen", fs);
      if (host_.set_fullscreen) host_.set_fullscreen(fs);
      break;
    }
    case kMonitor: {
      const int n = int(display::ListMonitors().size()) + 1;  // + "Primary" (0)
      const int m = (std::clamp(GetInt("monitor"), 0, n - 1) + dir + n) % n;
      SetInt("monitor", m);
      display::MoveToMonitor(m);
      break;
    }
    case kWindowSize: {
      int i = (WindowSizeIndex() + dir + kWindowSizeCount) % kWindowSizeCount;
      SetInt("window_width", kWindowSizes[i].w);
      SetInt("window_height", kWindowSizes[i].h);
      display::ResizeWindow(kWindowSizes[i].w, kWindowSizes[i].h);
      break;
    }
    case kRenderRes: {
      int i = (RenderIndex() + dir + 4) % 4;
      SetBool("recomp_render_auto", i == 0);
      if (i > 0) SetInt("resolution_scale", i);
      if (!host_.launcher) status_ = "Render resolution changes on the next launch.";
      break;
    }
    case kAspect: SetBool("present_letterbox", !GetBool("present_letterbox")); break;
    case kFiltering: {
      int i = (AnisoIndex() + dir + kAnisoCount) % kAnisoCount;
      SetInt("anisotropic_override", kAnisoValues[i]);
      break;
    }
    case kAudioBuffer: {
      int i = (AudioIndex() + dir + kAudioCount) % kAudioCount;
      SetInt("audio_maxqframes", kAudioFrames[i]);
      if (!host_.launcher) status_ = "Audio buffer changes on the next launch.";
      break;
    }
    case kKeyboard: SetBool("mnk_mode", !GetBool("mnk_mode")); break;
    case kLauncher: SetBool("recomp_launcher", !GetBool("recomp_launcher")); break;
    default: return;
  }
  Save();
}

void GameMenu::OnDraw(ImGuiIO& io) {
  const ImVec2 display = io.DisplaySize;
  if (display.x <= 0 || display.y <= 0) return;

  ImGui::SetNextWindowPos(ImVec2(0, 0));
  ImGui::SetNextWindowSize(display);
  ImGui::Begin("##recomp_settings_menu", nullptr,
               ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove |
                   ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoBackground |
                   ImGuiWindowFlags_NoFocusOnAppearing | ImGuiWindowFlags_NoNav);
  float scale = std::max(0.6f, display.y / 900.0f);
  PollGamepads();
  if (page_ == Page::kMain) {
    DrawMain(scale);
  } else {
    DrawKeys(scale);
  }
  ImGui::End();
}

void GameMenu::DrawMain(float scale) {
  // ---- keyboard / gamepad
  const int n = int(rows_.size());
  if (ImGui::IsKeyPressed(ImGuiKey_UpArrow) || pad_.up) { sel_ = (sel_ + n - 1) % n; quit_armed_ = false; }
  if (ImGui::IsKeyPressed(ImGuiKey_DownArrow) || pad_.down) { sel_ = (sel_ + 1) % n; quit_armed_ = false; }
  int activate = 0;  // -1 left, +1 right/enter
  if (ImGui::IsKeyPressed(ImGuiKey_LeftArrow) || pad_.left) activate = -1;
  if (ImGui::IsKeyPressed(ImGuiKey_RightArrow) || ImGui::IsKeyPressed(ImGuiKey_Enter) ||
      ImGui::IsKeyPressed(ImGuiKey_Space) || pad_.right || pad_.accept) {
    activate = 1;
  }
  // B closes the in-game menu.
  if (pad_.back && !host_.launcher) {
    Close();
    return;
  }

  // ---- layout
  ImGuiIO& io = ImGui::GetIO();
  Painter p{ImGui::GetWindowDrawList(), ImGui::GetFont(), 26.0f * scale};
  float lh = p.size * 1.35f;
  float pw = p.TextWidth("M") * 50.0f;
  float ph = (n + 4.5f) * lh;
  float px = (io.DisplaySize.x - pw) / 2, py = (io.DisplaySize.y - ph) / 2;
  float value_x = px + 20 * scale + p.TextWidth("M") * 20.0f;

  // The launcher has no game picture behind it: solid background.
  p.Rect(0, 0, io.DisplaySize.x, io.DisplaySize.y, host_.launcher ? IM_COL32(0, 0, 0, 255) : kShade);
  p.Rect(px, py, pw, ph, kPanel);
  std::string title = host_.title + (host_.launcher ? "" : "  -  SETTINGS");
  p.Text(px + 20 * scale, py + 14 * scale, title.c_str(), kGold);

  float y = py + 14 * scale + lh * 1.4f;
  for (int i = 0; i < n; i++) {
    float rx = px + 8 * scale, rw = pw - 16 * scale;
    // ---- mouse: hover selects, left click = next, right click = previous
    if (ImGui::IsMouseHoveringRect(ImVec2(rx, y - 4 * scale), ImVec2(rx + rw, y - 4 * scale + lh)) &&
        (io.MouseDelta.x != 0 || io.MouseDelta.y != 0)) {
      if (sel_ != i) quit_armed_ = false;
      sel_ = i;
    }
    if (RowClicked(rx, y - 4 * scale, rw, lh, ImGuiMouseButton_Left)) { sel_ = i; activate = 1; }
    if (RowClicked(rx, y - 4 * scale, rw, lh, ImGuiMouseButton_Right)) { sel_ = i; activate = -1; }

    const int row = rows_[i];
    if (i == sel_) p.Rect(rx, y - 4 * scale, rw, lh, kHighlight);
    p.Text(px + 20 * scale, y, MainLabel(row), row == kStart ? kGold : i == sel_ ? kWhite : kGrey);
    if (IsOptionRow(row)) {
      std::string v = "< " + MainValue(row) + " >";
      p.Text(value_x, y, v.c_str(), kWhite);
    } else if (row == kEditKeys) {
      p.Text(value_x, y, ">", kWhite);
    } else if (row == kQuit && i == sel_ && quit_armed_) {
      p.Text(value_x, y, "press again to quit", kGold);
    }
    y += lh;
  }

  // ---- act on the selection
  if (activate != 0) {
    const int row = rows_[sel_];
    if (IsOptionRow(row)) {
      Change(row, activate);
    } else if (activate > 0) {
      switch (row) {
        case kStart:
          Save();
          if (host_.start) host_.start();
          Close();
          return;
        case kEditKeys:
          page_ = Page::kKeys;
          keys_sel_ = 0;
          break;
        case kCloseMenu:
          Close();
          break;
        case kQuit:
          if (quit_armed_) {
            if (host_.quit) host_.quit();
          } else {
            quit_armed_ = true;
          }
          break;
      }
    }
  }

  const char* note = MainNote(rows_[sel_], host_.launcher);
  std::string bottom = !status_.empty() ? status_
                       : note             ? note
                                          : "Up/Down select, Left/Right change. Saved automatically.";
  p.Text(px + 20 * scale, y + lh * 0.4f, bottom.c_str(), status_.empty() ? kGrey : kGold, 0.75f);
  p.Text(px + 20 * scale, y + lh * 1.3f,
         host_.launcher ? "Enter / A: select     Left/Right: change     F11: fullscreen"
                        : "F1 / Esc: close     F3: performance     F11: fullscreen",
         kGrey, 0.75f);
}

void GameMenu::DrawKeys(float scale) {
  int n = kKeysRowCount;

  // ---- waiting for a key?
  if (capturing_ >= 0) {
    if (ImGui::IsKeyPressed(ImGuiKey_Escape, false)) {
      capturing_ = -1;
    } else {
      for (const auto& k : BindableKeys()) {
        if (ImGui::IsKeyPressed(k.key, false)) {
          SetStr(kKeyBinds[capturing_].cvar, k.name);
          Save();
          capturing_ = -1;
          break;
        }
      }
    }
  } else {
    if (ImGui::IsKeyPressed(ImGuiKey_UpArrow) || pad_.up) keys_sel_ = (keys_sel_ + n - 1) % n;
    if (ImGui::IsKeyPressed(ImGuiKey_DownArrow) || pad_.down) keys_sel_ = (keys_sel_ + 1) % n;
    if (ImGui::IsKeyPressed(ImGuiKey_Backspace, false) ||
        ImGui::IsKeyPressed(ImGuiKey_LeftArrow, false) || pad_.back || pad_.left) {
      page_ = Page::kMain;
      return;
    }
  }
  bool activate = capturing_ < 0 && (ImGui::IsKeyPressed(ImGuiKey_Enter, false) ||
                                     ImGui::IsKeyPressed(ImGuiKey_Space, false) || pad_.accept);

  ImGuiIO& io = ImGui::GetIO();
  Painter p{ImGui::GetWindowDrawList(), ImGui::GetFont(), 24.0f * scale};
  float lh = p.size * 1.3f;
  float pw = p.TextWidth("M") * 46.0f;
  float ph = (n + 4.5f) * lh;
  float px = (io.DisplaySize.x - pw) / 2, py = (io.DisplaySize.y - ph) / 2;
  float value_x = px + 20 * scale + p.TextWidth("M") * 22.0f;

  p.Rect(0, 0, io.DisplaySize.x, io.DisplaySize.y, host_.launcher ? IM_COL32(0, 0, 0, 255) : kShade);
  p.Rect(px, py, pw, ph, kPanel);
  p.Text(px + 20 * scale, py + 14 * scale, "KEYBOARD KEYS", kGold);

  float y = py + 14 * scale + lh * 1.4f;
  for (int i = 0; i < n; i++) {
    float rx = px + 8 * scale, rw = pw - 16 * scale;
    if (capturing_ < 0) {
      if (ImGui::IsMouseHoveringRect(ImVec2(rx, y - 4 * scale), ImVec2(rx + rw, y - 4 * scale + lh)) &&
          (io.MouseDelta.x != 0 || io.MouseDelta.y != 0)) {
        keys_sel_ = i;
      }
      if (RowClicked(rx, y - 4 * scale, rw, lh, ImGuiMouseButton_Left)) {
        keys_sel_ = i;
        activate = true;
      }
    }
    if (i == keys_sel_) p.Rect(rx, y - 4 * scale, rw, lh, kHighlight);
    if (i < kKeyBindCount) {
      p.Text(px + 20 * scale, y, kKeyBinds[i].label, i == keys_sel_ ? kWhite : kGrey);
      if (i == capturing_) {
        p.Text(value_x, y, "press a key...", kGold);
      } else {
        p.Text(value_x, y, KeyDisplay(GetStr(kKeyBinds[i].cvar)).c_str(), kWhite);
      }
    } else if (i == kKeysReset) {
      p.Text(px + 20 * scale, y, "Reset to defaults", i == keys_sel_ ? kWhite : kGrey);
    } else {
      p.Text(px + 20 * scale, y, "Back", i == keys_sel_ ? kWhite : kGrey);
    }
    y += lh;
  }

  if (activate) {
    if (keys_sel_ < kKeyBindCount) {
      capturing_ = keys_sel_;
    } else if (keys_sel_ == kKeysReset) {
      ApplyDefaultKeys();
      Save();
    } else {
      page_ = Page::kMain;
    }
  }

  const char* hint = capturing_ >= 0 ? "Press the new key. Esc cancels."
                                     : "Enter: change key   Backspace / Left: back";
  p.Text(px + 20 * scale, y + lh * 0.4f, hint, capturing_ >= 0 ? kGold : kGrey, 0.75f);
  p.Text(px + 20 * scale, y + lh * 1.3f, "Keys only work when Keyboard controls is On.", kGrey,
         0.75f);
}

}  // namespace recomp_ui
