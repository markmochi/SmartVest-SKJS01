#pragma once
#include "Arduino.h"
enum class DNSReplyCode { NoError };
inline int dnsStarts=0, dnsTicks=0, dnsStops=0;
inline bool dnsOK=true;
class DNSServer { public: void setTTL(int){} void setErrorReplyCode(DNSReplyCode){}
 bool start(int,const char*,IPAddress){++dnsStarts;return dnsOK;}
 void stop(){++dnsStops;}
 void processNextRequest(){++dnsTicks;} };
