// outrun_arcade - ReXGlue Recompiled Project
//
// App hooks: default paths, GPU plugin, full-game license, first-launch
// defaults, the launcher shown before the game starts, and the F1/Esc
// settings menu (both src/game_menu.*).

#pragma once

#include <cstdlib>
#include <filesystem>
#include <functional>
#include <optional>
#include <string>

#include <rex/cvar.h>
#include <rex/filesystem.h>
#include <rex/filesystem/vfs.h>
#include <rex/input/input_system.h>
#include <rex/logging.h>
#include <rex/rex_app.h>
#include <rex/runtime.h>
#include <rex/ui/keybinds.h>

void InstallCrashHandler();  // src/crash_handler.cpp

#include <imgui.h>

#include "display_setup.h"
#include "game_keys.h"
#include "game_menu.h"
#include "package_setup.h"
#include "portable_data.h"

class OutrunArcadeApp : public rex::ReXApp {
 public:
  using rex::ReXApp::ReXApp;

  static std::unique_ptr<rex::ui::WindowedApp> Create(
      rex::ui::WindowedAppContext& ctx) {
    return std::unique_ptr<OutrunArcadeApp>(new OutrunArcadeApp(ctx, "outrun_arcade",
        PPCImageConfig));
  }

  // Default the game data folder to "assets" next to the exe, so the game
  // runs with a plain double-click. --game_data_root still overrides it.
  void OnConfigurePaths(rex::PathConfig& paths) override {
    if (paths.game_data_root.empty()) {
      paths.game_data_root =
          rex::filesystem::GetExecutableFolder() / "assets";
      // First start: make "assets" from the player's own XBLA package next to the exe.
      if (!package_setup_first_run(rex::filesystem::GetExecutableFolder(),
                                   paths.game_data_root, 0x58410968u,
                                   "OutRun Online Arcade")) {
        std::exit(1);
      }
    }
    // Saves, profile and shader cache in "userdata" next to the exe.
    UsePortableUserData(paths, "outrun_arcade");
    config_path_ = paths.config_path;
  }

  void OnPreSetup(rex::RuntimeConfig& config) override {
    // GPU emulation is opt-in in ReXGlue (gpu_plugin cvar). Default to the
    // Xenos plugin so the game actually renders; --gpu_plugin still overrides.
    if (config.gpu_plugin.empty()) {
      config.gpu_plugin = "xenos";
    }
    // XBLA titles run as the trial ("locked") unless XamContentGetLicenseMask
    // reports a purchase; bit 0 = full game. ReXGlue's license_mask cvar
    // defaults to 0 (trial), so unlock unless the user set it explicitly.
    if (rex::cvar::Query<uint32_t>("license_mask") == 0) {
      rex::cvar::SetFlagByName("license_mask", "1");
      REXLOG_INFO("License: full game (license_mask=1)");
    }
    // First launch: fullscreen (borderless, at the monitor's own resolution),
    // 1280x720 for windowed mode, keyboard on, driving key layout. After this
    // everything comes from outrun_arcade.toml (edited by the launcher / F1 menu).
    if (!rex::cvar::Query<bool>("recomp_defaults_applied")) {
      rex::cvar::SetFlagByName("fullscreen", "true");
      rex::cvar::SetFlagByName("window_width", "1280");
      rex::cvar::SetFlagByName("window_height", "720");
      rex::cvar::SetFlagByName("mnk_mode", "true");
      recomp_ui::GameMenu::ApplyDefaultKeys();
      rex::cvar::SetFlagByName("recomp_defaults_applied", "true");
      if (!config_path_.empty()) {
        rex::cvar::SaveConfig(config_path_);
      }
      REXLOG_INFO("First launch: wrote default settings to {}",
                  config_path_.filename().string());
    }
    // Defaults added by later builds, applied once on top of existing settings.
    if (rex::cvar::Query<int32_t>("recomp_defaults_version") < 2) {
      rex::cvar::SetFlagByName("audio_maxqframes",
                               std::to_string(recomp_ui::game_keys::kDefaultAudioFrames));
      rex::cvar::SetFlagByName("recomp_defaults_version", "2");
      if (!config_path_.empty()) rex::cvar::SaveConfig(config_path_);
    }
    // Steam Deck (Steam sets SteamDeck=1, also under Proton): texture
    // filtering off and the small audio buffer, applied once. Tested on a
    // Deck: choppy with the PC defaults, 60 fps with these.
    const char* deck = std::getenv("SteamDeck");
    if (deck && deck[0] == '1' && rex::cvar::Query<int32_t>("recomp_defaults_version") < 3) {
      rex::cvar::SetFlagByName("anisotropic_override", "0");
      rex::cvar::SetFlagByName("audio_maxqframes", "8");
      rex::cvar::SetFlagByName("recomp_defaults_version", "3");
      if (!config_path_.empty()) rex::cvar::SaveConfig(config_path_);
      REXLOG_INFO("Steam Deck detected: texture filtering off, small audio buffer");
    }
    REXLOG_INFO("Audio buffer: {} frames", rex::cvar::Query<int32_t>("audio_maxqframes"));
    // VSync is always on: no menu option, a saved "vsync = false" is ignored,
    // and anything that turns it off at runtime (e.g. the SDK's F4 overlay)
    // is undone straight away.
    rex::cvar::SetFlagByName("vsync", "true");
    rex::cvar::RegisterChangeCallback("vsync", [](std::string_view, std::string_view value) {
      if (value != "true") rex::cvar::SetFlagByName("vsync", "true");
    });
  }

  // Launcher: the window, Vulkan presenter and ImGui exist, the runtime does
  // not yet - so the GPU and audio settings picked here apply without a
  // restart. "Start game" resumes the SDK's startup.
  std::optional<rex::PathConfig> OnFinalizePaths(
      const rex::PathConfig& defaults, std::function<void(rex::PathConfig)> resume) override {
    if (!recomp_ui::GameMenu::LauncherEnabled() || !imgui_drawer()) {
      recomp_ui::display::ApplyAutoRenderScale();
      return defaults;
    }
    recomp_ui::MenuHost host = MakeMenuHost();
    host.launcher = true;
    host.start = [this, defaults, resume] {
      launcher_open_ = false;
      recomp_ui::display::ApplyAutoRenderScale();
      if (!config_path_.empty()) rex::cvar::SaveConfig(config_path_);
      app_context().CallInUIThreadDeferred([defaults, resume] { resume(defaults); });
    };
    launcher_open_ = true;
    menu_ = new recomp_ui::GameMenu(imgui_drawer(), std::move(host));
    return std::nullopt;
  }

  void OnCreateDialogs(rex::ui::ImGuiDrawer* drawer) override {
    (void)drawer;
    rex::ui::RegisterBind("bind_menu", "F1", "Open/close the settings menu",
                          [this] { ToggleMenu(); });
    rex::ui::RegisterBind("bind_menu_alt", "Escape", "Open/close the settings menu",
                          [this] { ToggleMenu(); });
    rex::ui::RegisterBind("bind_fullscreen", "F11", "Toggle fullscreen", [this] {
      if (menu_ && menu_->capturing()) return;
      bool fs = !window()->IsFullscreen();
      rex::cvar::SetFlagByName("fullscreen", fs ? "true" : "false");
      window()->SetFullscreen(fs);
      if (!config_path_.empty()) rex::cvar::SaveConfig(config_path_);
    });
  }

  // The game must not see keys/buttons while the menu is open.
  void OnPostSetup() override {
    InstallCrashHandler();  // re-assert after SDL/runtime init
    auto* input_sys = static_cast<rex::input::InputSystem*>(runtime()->input_system());
    if (input_sys && imgui_drawer()) {
      input_sys->SetActiveCallback([this]() {
        if (menu_) return false;
        return !imgui_drawer()->GetIO().WantCaptureMouse;
      });
    }
  }

 private:
  void ToggleMenu() {
    if (launcher_open_) return;  // the launcher stays until Start / Quit
    if (menu_) {
      if (menu_->capturing()) return;  // Esc cancels the key capture instead
      delete menu_;                    // removes itself from the drawer
      menu_ = nullptr;
      return;
    }
    if (!imgui_drawer()) return;
    menu_ = new recomp_ui::GameMenu(imgui_drawer(), MakeMenuHost());
  }

  recomp_ui::MenuHost MakeMenuHost() {
    recomp_ui::MenuHost host;
    host.title = "OUTRUN ONLINE ARCADE";
    host.config_path = config_path_;
    host.set_fullscreen = [this](bool fs) {
      app_context().CallInUIThreadDeferred([this, fs] { window()->SetFullscreen(fs); });
    };
    host.quit = [this] {
      app_context().CallInUIThreadDeferred([this] { window()->RequestClose(); });
    };
    host.on_closed = [this] { menu_ = nullptr; };
    return host;
  }

  std::filesystem::path config_path_;
  recomp_ui::GameMenu* menu_ = nullptr;  // owned by the ImGui drawer
  bool launcher_open_ = false;
};
