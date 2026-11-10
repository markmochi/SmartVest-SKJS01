#pragma once
#include <cstring>
#include "NetworkPolicy.h"

// HTTP-only compatibility replies for a local device AP. These are not proof
// of Internet connectivity. Unknown sites and every dashboard/API request keep
// their normal Host, authentication and CSRF rules. No TLS impersonation.
namespace HotspotConnectivity {
struct Reply {int status;const char* type;const char* body;};
inline const Reply* lookup(const char* host,const char* path) {
  static const Reply android{204,"text/plain",""};
  static const Reply apple{200,"text/html","<HTML><HEAD><TITLE>Success</TITLE></HEAD><BODY>Success</BODY></HTML>"};
  static const Reply windows{200,"text/plain","Microsoft Connect Test"};
  static const Reply windowsLegacy{200,"text/plain","Microsoft NCSI"};
  static const Reply firefox{200,"text/plain","success\n"};
  if (!path || !host)return nullptr;
  for(const auto* p:{"/generate_204","/gen_204","/generate204","/connectivity-check.html","/mobile/status.php"})
    if(!std::strcmp(path,p))return &android;
  for(const auto* p:{"/hotspot-detect.html","/library/test/success.html","/success.html"})
    if(!std::strcmp(path,p))return &apple;
  if(!std::strcmp(path,"/connecttest.txt"))return &windows;
  if(!std::strcmp(path,"/ncsi.txt"))return &windowsLegacy;
  if(!std::strcmp(path,"/success.txt"))return &firefox;
  // Some Apple probes use the host root. Never intercept the dashboard root.
  const auto authority=NetworkPolicy::normalizeAuthority(host);
  if(!std::strcmp(path,"/") && (authority=="captive.apple.com" ||
       authority=="www.appleiphonecell.com"))return &apple;
  return nullptr;
}
}
