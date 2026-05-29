#pragma once

enum class AppMode {
  Normal,
  Config,
};

inline const char* appModeToString(AppMode mode) {
  switch (mode) {
    case AppMode::Config:
      return "CONFIG_MODE";
    case AppMode::Normal:
    default:
      return "NORMAL_MODE";
  }
}
