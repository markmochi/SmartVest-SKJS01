#include "../../common/CameraAuth.h"
#include <cassert>
#include <cstring>
#include <iostream>
constexpr const char* key="000102030405060708090a0b0c0d0e0f101112131415161718191a1b1c1d1e1f";
int main(){
  char digest[65],peek[65];CameraCrypto::Hash hash;
  assert(hash.begin()&&hash.add("ab",2)&&hash.peek(peek)&&hash.add("c",1)&&hash.finish(digest));
  assert(!strcmp(digest,"ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad"));
  assert(!strcmp(peek,"fb8e20fc2e4c3f248c60c39bd652f3c1347298bb977b8b4d5903b85055620603"));
  CameraCrypto::Mac mac;assert(mac.begin(key)&&mac.add("abc",3)&&mac.finish(digest));
  std::cout<<"HMAC "<<digest<<"\n";
  using namespace CameraAuth;
  Peer peer;Exchange a,b;const char* client="11111111111111111111111111111111";
  const char* nonce="22222222222222222222222222222222";
  assert(peer.offer(client,nonce,UINT32_MAX-100,a));a.sequence=1;
  const char* path="/api/v1/cameras/ack";const char* body="{\"stage\":\"protected\"}";
  assert(message(key,"front",a,"POST",path,body,strlen(body),0,digest));
  assert(!peer.accept(key,"rear",a,"POST",path,body,strlen(body),digest,UINT32_MAX-50));
  assert(!peer.accept(key,"front",a,"POST",path,"{}",2,digest,UINT32_MAX-50));
  assert(!peer.accept(key,"front",a,"GET",path,body,strlen(body),digest,UINT32_MAX-50));
  assert(!peer.accept(key,"front",a,"POST","/other",body,strlen(body),digest,UINT32_MAX-50));
  assert(peer.accept(key,"front",a,"POST",path,body,strlen(body),digest,UINT32_MAX-50));
  assert(!peer.accept(key,"front",a,"POST",path,body,strlen(body),digest,1));
  assert(peer.offer("33333333333333333333333333333333","44444444444444444444444444444444",1,b));
  Exchange duplicate;assert(!peer.offer("55555555555555555555555555555555",nonce,2,duplicate));
  // Offering a new handshake cannot evict a working camera session.
  ++a.sequence;assert(message(key,"front",a,"POST",path,body,strlen(body),0,digest));
  assert(peer.accept(key,"front",a,"POST",path,body,strlen(body),digest,2));
  b.sequence=1;assert(message(key,"front",b,"POST",path,body,strlen(body),0,digest));
  assert(peer.accept(key,"front",b,"POST",path,body,strlen(body),digest,3));
  ++a.sequence;assert(message(key,"front",a,"POST",path,body,strlen(body),0,digest));
  assert(!peer.accept(key,"front",a,"POST",path,body,strlen(body),digest,4));
  assert(message(key,"front",b,"POST",path,"{}",2,200,digest));char other[65];
  assert(message(key,"front",b,"POST",path,"{}",2,409,other)&&strcmp(digest,other));
  assert(message(key,"front",b,"POST",path,"{}",2,0,other)&&strcmp(digest,other));
  Peer restarted;assert(!restarted.accept(key,"front",b,"POST",path,body,strlen(body),digest,5));
  Peer expiry;assert(expiry.offer(client,nonce,UINT32_MAX-10,a));a.sequence=1;
  assert(message(key,"front",a,"POST",path,body,strlen(body),0,digest));
  assert(!expiry.accept(key,"front",a,"POST",path,body,strlen(body),digest,PENDING_MS));
  uint32_t n;assert(number("4294967295",n)&&n==UINT32_MAX);
  for(auto invalid:{"0","01","-1","1 ","4294967296","","000000000001"})assert(!number(invalid,n));
  std::cout<<"PASS hash cloning, role/body/path/method authentication, replay, session replacement, restart, response binding, expiry and integer bounds\n";
}
