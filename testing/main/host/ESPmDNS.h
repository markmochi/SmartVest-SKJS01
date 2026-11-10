#pragma once
inline int mdnsBegins=0,mdnsEnds=0;
struct MockMDNS {bool ok=true; bool begin(const char*){++mdnsBegins;return ok;}
 void end(){++mdnsEnds;} void addService(const char*,const char*,int){} };
inline MockMDNS MDNS;
