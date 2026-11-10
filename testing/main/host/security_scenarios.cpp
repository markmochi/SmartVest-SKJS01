#include <cassert>
#include <iostream>
#include <limits>
#include "src/security/Security.h"
#include <mbedtls/pkcs5.h>
uint32_t clockMs = 100;
int main() {
 DeviceState state; state.salt="0123456789abcdef0123456789abcdef";
 assert(Security::hashPassword("admin123",state.salt)=="2f5490e9e507ed00de24d1b95914183ddbda70abc1b7b8138cf9426b9ac7c29d");
 for(const char* stored:{Config::DEFAULT_RIDER_PASSWORD,Config::LEGACY_DEFAULT_PASSWORD}){
  state.passwordHash=Security::hashPassword(stored,state.salt);
  state.defaultPassword=false;
  assert(Security::factoryRiderAccount(state));
  for(const char* entered:{Config::DEFAULT_RIDER_PASSWORD,Config::LEGACY_DEFAULT_PASSWORD}){
   Security auth; const auto before=passwordDerivations;auto session=auth.login("admin",entered,state);
   if(Config::ALLOW_DEFAULT_RIDER_LOGIN)assert(passwordDerivations-before==(std::string(stored)==Config::DEFAULT_RIDER_PASSWORD?0u:std::string(entered)==stored?1u:2u));
   assert(bool(session)==Config::ALLOW_DEFAULT_RIDER_LOGIN);
   if(session){assert(session->role==Access::Role::Rider);assert(session->token.length()==64);assert(session->csrf.length()==64);assert(!Security::equal(session->token,session->csrf));}
  }
 }
 state.passwordHash=Security::hashPassword("Private-rider-2026",state.salt);
 state.defaultPassword=true;
 assert(!Security::factoryRiderAccount(state));
 assert(!Security::verifyRiderPassword(Config::DEFAULT_RIDER_PASSWORD,state));
 assert(!Security::verifyRiderPassword(Config::LEGACY_DEFAULT_PASSWORD,state));
 assert(Security::verifyRiderPassword("Private-rider-2026",state));
 assert(!Security::validNewRiderPassword("short"));
 assert(!Security::validNewRiderPassword(Config::LEGACY_DEFAULT_PASSWORD));
 assert(!Security::validNewRiderPassword(Config::DEFAULT_RIDER_PASSWORD));
 assert(Security::validNewRiderPassword("Private-rider-2026"));
 assert(!Security::validNewRiderPassword(String(std::string(65,'a'))));
 assert(Access::validRiderUsername("rider.name"));
 assert(!Access::validRiderUsername(Config::DEVELOPER_USERNAME));
 assert(!Access::allowed(Access::Role::Rider,Access::Role::Developer));
 assert(Access::allowed(Access::Role::Developer,Access::Role::Rider));
 Security auth;auto rider=auth.login("admin","Private-rider-2026",state);assert(rider);
 WebServer web;web.cookie=String("other=value; SVSID=")+rider->token+"; more=value";
 assert(auth.current(web)==rider);
 auto developer=auth.login(Config::DEVELOPER_USERNAME,Config::DEVELOPER_PASSWORD,state);
 assert(developer&&developer->role==Access::Role::Developer);
 auth.clearRiderSessions();assert(!auth.current(web));assert(developer->token.length()==64);
 web.cookie=String("SVSID=")+developer->token;
 clockMs+=Config::SESSION_TIMEOUT_MS;assert(!auth.current(web));
 Security throttle;
 for(int attempt=0;attempt<5;++attempt)assert(!throttle.login("admin","incorrect",state));
 assert(throttle.throttled());assert(!throttle.login("admin","Private-rider-2026",state));
 clockMs+=Config::LOGIN_THROTTLE_MS;assert(!throttle.throttled());assert(throttle.login("admin","Private-rider-2026",state));
 clockMs=std::numeric_limits<uint32_t>::max()-50;
 Security wrapped;auto wrapSession=wrapped.login("admin","Private-rider-2026",state);
 web.cookie=String("SVSID=")+wrapSession->token;clockMs+=Config::SESSION_TIMEOUT_MS;
 assert(!wrapped.current(web));
 std::cout<<"PASS: factory aliases/optional change, private-account preservation, password policy, roles, cookies, throttle, expiry/wrap; home="<<Config::USE_HOME_WIFI<<", defaults="<<Config::ALLOW_DEFAULT_RIDER_LOGIN<<"\n";

 state.adminUsername="owner";state.adminSalt="0123456789abcdef0123456789abcdef";state.adminHash=Security::hashPassword("Owner-private-2026",state.adminSalt);
 Security separate;assert(!separate.login(Config::DEVELOPER_USERNAME,Config::DEVELOPER_PASSWORD,state));
 assert(!separate.login("owner","Private-rider-2026",state));
 auto owner=separate.login("owner","Owner-private-2026",state);assert(owner&&owner->role==Access::Role::Developer);
 assert(separate.login(state.riderUsername,"Private-rider-2026",state));
 std::cout<<"PASS separate persisted administrator and rider hashes and login names\n";
}
