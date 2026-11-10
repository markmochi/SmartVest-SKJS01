#pragma once
#include <cstdint>
#include <cstring>
#include "../../Config.h"

namespace Access {
enum class Role : uint8_t { Rider, Developer };
inline const char* name(Role role) {
  return role == Role::Developer ? "developer" : "rider";
}
inline bool allowed(Role role, Role required) {
  return required == Role::Rider || role == Role::Developer;
}
inline bool validRiderUsername(const char* username, bool administrator = false) {
  const size_t size = std::strlen(username);
  if (size < 3 || size > 32 ||
      (!administrator && !std::strcmp(username, Config::DEVELOPER_USERNAME)))
    return false;
  for (size_t i = 0; i < size; ++i) {
    const char c = username[i];
    if (!((c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') ||
          (c >= '0' && c <= '9') || c == '_' || c == '-' || c == '.'))
      return false;
  }
  return true;
}
}  // namespace Access
