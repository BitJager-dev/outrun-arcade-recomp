// Portable user data - see portable_data.h.
//
// Runs before the SDK sets up logging, so messages go to stderr.

#include "portable_data.h"

#include <cstdio>
#include <filesystem>
#include <fstream>
#include <system_error>

#include <rex/cvar.h>
#include <rex/filesystem.h>

namespace fs = std::filesystem;

namespace {

bool Writable(const fs::path& dir) {
  std::error_code ec;
  fs::create_directories(dir, ec);
  if (ec) return false;
  const fs::path probe = dir / ".write_test";
  { std::ofstream f(probe); if (!f) return false; }
  fs::remove(probe, ec);
  return true;
}

}  // namespace

void UsePortableUserData(rex::PathConfig& paths, const std::string& app_name) {
  if (!rex::cvar::Query<std::string>("user_data_root").empty()) return;

  const fs::path exe_dir = rex::filesystem::GetExecutableFolder();
  const fs::path portable = exe_dir / "userdata";
  const fs::path old_dir = rex::filesystem::GetUserFolder() / app_name;
  std::error_code ec;

  if (!fs::exists(portable, ec)) {
    if (!Writable(exe_dir)) {
      std::fprintf(stderr, "Game folder not writable, saves stay in %s\n", old_dir.string().c_str());
      return;
    }
    // Earlier builds saved to the per-user folder: copy it over (left in place).
    if (fs::is_directory(old_dir, ec)) {
      fs::copy(old_dir, portable, fs::copy_options::recursive, ec);
      if (ec) {
        std::fprintf(stderr, "Copying saves from %s failed: %s\n", old_dir.string().c_str(),
                     ec.message().c_str());
      } else {
        std::fprintf(stderr, "Copied saves from %s to %s\n", old_dir.string().c_str(),
                     portable.string().c_str());
      }
    }
    fs::create_directories(portable, ec);
  }

  if (paths.cache_root == paths.user_data_root / "cache") paths.cache_root = portable / "cache";
  paths.user_data_root = portable;
}
