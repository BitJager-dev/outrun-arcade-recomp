// In-game settings menu (shared across the darkuni recomps).
//
// Same layout and behaviour as the menus in the Contra / Castlevania / Gradius
// ReBirth recomps (centred panel, "Label  < value >" rows, Up/Down to select,
// Left/Right/Enter to change, note line at the bottom, settings saved on every
// change), rebuilt on ReXGlue's ImGui overlay instead of raw OpenGL.
//
// Open/close: F1 or Esc.  Everything is stored in <app>.toml next to the
// exe via ReXGlue's cvar system, so it is also editable by hand.
//
// Launcher mode (MenuHost::launcher): the same menu shown before the game
// starts, with "Start game" on top. Settings the GPU/audio read at setup
// (render resolution, audio buffer) then apply without a restart.
// Keyboard, mouse and gamepad (d-pad / left stick, A = select, B = back).

#pragma once

#include <filesystem>
#include <functional>
#include <string>
#include <vector>

#include <rex/ui/imgui_dialog.h>

struct SDL_Gamepad;

namespace recomp_ui {

struct MenuHost {
  std::string title;                         // shown as "<TITLE>  -  SETTINGS"
  std::filesystem::path config_path;
  std::function<void(bool)> set_fullscreen;  // live display-mode switch
  std::function<void()> quit;                // close the game
  std::function<void()> on_closed;           // menu closed itself (deleted)
  bool launcher = false;                     // pre-launch screen (see above)
  std::function<void()> start;               // launcher: start the game
};

class GameMenu : public rex::ui::ImGuiDialog {
 public:
  GameMenu(rex::ui::ImGuiDrawer* drawer, MenuHost host);
  ~GameMenu() override;

  // True while waiting for a key to bind; the app's F1/Esc binds stay quiet.
  bool capturing() const { return capturing_ >= 0; }

  // Default keyboard layout (written on first launch, and by "Reset keys").
  static void ApplyDefaultKeys();

  // Show the launcher before the game starts (recomp_launcher cvar).
  static bool LauncherEnabled();

 protected:
  void OnDraw(ImGuiIO& io) override;
  void OnClose() override;

 private:
  enum class Page { kMain, kKeys };

  void DrawMain(float scale);
  void DrawKeys(float scale);
  void Change(int row, int dir);
  void Save();
  void PollGamepads();

  // Gamepad, as key presses for this frame (edge + auto-repeat for moves).
  struct Pad {
    bool up, down, left, right, accept, back;
  };
  Pad pad_{};
  std::vector<SDL_Gamepad*> gamepads_;
  int pad_held_ = -1;        // direction being held (0-3), for auto-repeat
  double pad_next_ = 0.0;    // time of the next repeat
  bool pad_was_[2] = {};     // accept / back down last frame

  MenuHost host_;
  std::vector<int> rows_;  // main page rows (MainRow ids) for this mode
  Page page_ = Page::kMain;
  int sel_ = 0;
  int keys_sel_ = 0;
  int capturing_ = -1;
  bool quit_armed_ = false;
  std::string status_;
};

}  // namespace recomp_ui
