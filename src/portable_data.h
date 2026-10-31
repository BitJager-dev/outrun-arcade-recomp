// Portable user data: saves, profile, achievements and the shader cache live in
// "userdata" next to the exe instead of the SDK's per-user folder
// (Documents\<app> on Windows, ~/.local/share/<app> on Linux).
#pragma once

#include <string>

#include <rex/rex_app.h>

// Call from OnConfigurePaths. Keeps the SDK paths when --user_data_root /
// --cache_root are given or the exe folder is not writable. Copies an existing
// per-user folder into "userdata" once, so earlier saves carry over.
void UsePortableUserData(rex::PathConfig& paths, const std::string& app_name);
