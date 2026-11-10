#include "DashboardServer.h"
#include "generated/WebAssets.h"
#include <cstring>

bool DashboardServer::sendWebAsset(const char* path) {
  const bool versioned=std::strncmp(path,WEB_ASSET_PREFIX,sizeof(WEB_ASSET_PREFIX)-1)==0;
  const char* key=versioned?path+sizeof(WEB_ASSET_PREFIX)-1:path;
  // A fresh namespace permits retrying a failed native module graph. Its
  // runtime aliases still reference the same initialized session and queue.
  if(versioned&&std::strncmp(key,"_r/",3)==0){
    const char* p=key+3;size_t digits=0;
    while(*p>='0'&&*p<='9'&&digits<6){++p;++digits;}
    if(!digits||*p!='/')return false;
    key=p+1;
  }
  for (const auto& asset : WEB_ASSETS) {
    if ((asset.path[0]!='/' )!=versioned || std::strcmp(key, asset.path)) continue;
    if (!isCanonicalHost()) { sendError(403, "Use the vest IP address or local hostname."); return true; }
    if (web.method() != HTTP_GET && web.method() != HTTP_HEAD) {
      web.sendHeader("Allow", "GET, HEAD"); sendError(405, "Method not allowed."); return true;
    }
    addSecurityHeaders(versioned);
    web.sendHeader("Content-Encoding", "gzip");
    web.sendHeader("Vary", "Accept-Encoding");
    web.sendFlash(WEB_ASSET_TYPES[asset.type], asset.data, asset.size, web.method() == HTTP_HEAD);
    return true;
  }
  return false;
}
