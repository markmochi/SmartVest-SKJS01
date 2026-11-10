#pragma once
#include <string>

// Pure host/origin rules shared by the server and host-based regression tests.
// Exact matches only; no suffix/wildcard matching (DNS rebinding protection).
namespace NetworkPolicy {
inline std::string normalizeAuthority(std::string authority) {
  for (char& c : authority) if (c >= 'A' && c <= 'Z') c += 'a'-'A';
  if (authority.size() >= 3 && authority.compare(authority.size()-3, 3, ":80") == 0)
    authority.resize(authority.size()-3);
  return authority;
}
inline bool allowedHost(const std::string& host, const std::string& hostname,
                        const std::string& apIp, const std::string& stationIp) {
  const auto h = normalizeAuthority(host);
  return !h.empty() && (h == hostname || h == hostname + ".local" ||
         (!apIp.empty() && h == apIp) || (!stationIp.empty() && h == stationIp));
}
inline bool sameOrigin(const std::string& origin, const std::string& host) {
  if (origin.empty()) return true; // Session writes ALSO require the CSRF token.
  if (origin.compare(0, 7, "http://") != 0) return false;
  return normalizeAuthority(origin.substr(7)) == normalizeAuthority(host);
}
} // namespace NetworkPolicy
