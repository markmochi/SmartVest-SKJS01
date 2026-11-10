#pragma once

#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include "HttpMemoryBudget.h"

namespace HttpRequest {
constexpr size_t MAX_HEADERS = 2048;
constexpr size_t MAX_TARGET = 512;
constexpr size_t MAX_FIELDS = 32;
constexpr size_t MAX_BODY = 4096;
constexpr size_t MAX_ARGS = 16;
constexpr size_t MAX_LINE = 1024;

enum class Method : uint8_t { Get, Head, Post, Put, Patch, Delete, Options };

// Incremental framing only. Socket scheduling and request deadlines belong to
// ReadyWebServer; no incomplete request reaches a route or the SDK parser.
class Parser {
 public:
  using Allocate = void* (*)(size_t);
  using Release = void (*)(void*);
  explicit Parser(Allocate allocate = std::malloc, Release release = std::free)
      : allocate_(allocate ? allocate : std::malloc),
        release_(release ? release : std::free) {}
  ~Parser() { releaseBody(); }
  Parser(const Parser&) = delete;
  Parser& operator=(const Parser&) = delete;

  void reset() {
    releaseBody();
    used_ = lineStart_ = lineLength_ = count_ = 0;
    pathAt_ = queryAt_ = hostAt_ = 0;
    contentLength_ = received_ = 0;
    status_ = 0;
    version_ = 0;
    method_ = Method::Get;
    requestLine_ = pendingCr_ = headersDone_ = done_ = lengthSeen_ = false;
    headers_[0] = '\0';
  }

  void feed(uint8_t byte) {
    if (status_ || done_) return;
    if (headersDone_) {
      if (!byte) { fail(400); return; }
      body_[received_++] = char(byte);
      if (received_ == contentLength_) {
        body_[received_] = '\0';
        done_ = true;
      }
      return;
    }
    if (used_ == MAX_HEADERS) { fail(431); return; }
    if (pendingCr_) {
      if (byte != '\n') { fail(400); return; }
      headers_[used_++] = '\0';
      pendingCr_ = false;
      if (!requestLine_) parseRequestLine();
      else if (!lineLength_) finishHeaders();
      else parseHeader();
      lineStart_ = used_;
      lineLength_ = 0;
      return;
    }
    if (byte == '\r') {
      headers_[used_++] = '\0';
      pendingCr_ = true;
      return;
    }
    if (!byte || byte == '\n' || (byte < 32 && byte != '\t') || byte == 127) {
      fail(400); return;
    }
    if (lineLength_ == MAX_LINE) { fail(requestLine_ ? 431 : 414); return; }
    headers_[used_++] = char(byte);
    ++lineLength_;
  }

  bool complete() const { return done_ && !status_; }
  bool headersComplete() const { return headersDone_ && !status_; }
  unsigned error() const { return status_; }
  Method method() const { return method_; }
  uint8_t version() const { return version_; }
  const char* path() const { return headers_ + pathAt_; }
  const char* query() const { return queryAt_ ? headers_ + queryAt_ : ""; }
  const char* host() const { return hostAt_ ? headers_ + hostAt_ : ""; }
  size_t headerCount() const { return count_; }
  const char* headerName(size_t index) const {
    return index < count_ ? headers_ + fields_[index].name : "";
  }
  const char* headerValue(size_t index) const {
    return index < count_ ? headers_ + fields_[index].value : "";
  }
  const char* body() const { return body_ ? body_ : ""; }
  size_t bodyLength() const { return contentLength_; }

 private:
  struct Field { uint16_t name = 0, value = 0; };
  char headers_[MAX_HEADERS + 1] = {};
  Field fields_[MAX_FIELDS];
  size_t used_ = 0, lineStart_ = 0, lineLength_ = 0, count_ = 0;
  uint16_t pathAt_ = 0, queryAt_ = 0, hostAt_ = 0;
  size_t contentLength_ = 0, received_ = 0;
  unsigned status_ = 0;
  uint8_t version_ = 0;
  Method method_ = Method::Get;
  bool requestLine_ = false, pendingCr_ = false, headersDone_ = false;
  bool done_ = false, lengthSeen_ = false;
  char* body_ = nullptr;
  Allocate allocate_;
  Release release_;

  void releaseBody() { if (body_) { release_(body_); body_ = nullptr; } }
  void fail(unsigned status) { status_ = status; releaseBody(); }
  static bool whitespace(char c) { return c == ' ' || c == '\t'; }
  static char lower(char c) { return c >= 'A' && c <= 'Z' ? char(c + 'a' - 'A') : c; }
  static bool same(const char* a, const char* b) {
    while (*a && *b) if (lower(*a++) != lower(*b++)) return false;
    return *a == *b;
  }
  static bool prefix(const char* value, const char* start) {
    while (*start) if (lower(*value++) != lower(*start++)) return false;
    return true;
  }
  static bool token(char c) {
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
           (c >= '0' && c <= '9') || (c && std::strchr("!#$%&'*+-.^_`|~", c));
  }
  static bool authority(const char* value) {
    if (!*value) return false;
    const char* p = value;
    if (*p == '[') {
      ++p;
      bool colon = false, digit = false;
      for (; *p && *p != ']'; ++p) {
        const char c = lower(*p);
        if (c == ':') colon = true;
        else if ((c >= '0' && c <= '9') || (c >= 'a' && c <= 'f') || c == '.') digit = true;
        else return false;
      }
      if (*p != ']' || !colon || (!digit && p - value != 3)) return false;
      ++p;
    } else {
      for (; *p && *p != ':'; ++p)
        if (!((*p >= 'a' && *p <= 'z') || (*p >= 'A' && *p <= 'Z') ||
              (*p >= '0' && *p <= '9') || *p == '-' || *p == '.')) return false;
      if (p == value) return false;
    }
    if (!*p) return true;
    if (*p++ != ':' || !*p) return false;
    unsigned port = 0;
    for (; *p; ++p) {
      if (*p < '0' || *p > '9') return false;
      const unsigned digit = unsigned(*p - '0');
      if (port > (65535U - digit) / 10U) return false;
      port = port * 10U + digit;
    }
    return port != 0;
  }

  void parseRequestLine() {
    char* line = headers_ + lineStart_;
    char* first = std::strchr(line, ' ');
    if (!first || first == line) { fail(400); return; }
    *first++ = '\0';
    char* end = std::strchr(first, ' ');
    if (!end || end == first) { fail(400); return; }
    *end++ = '\0';
    if (!std::strcmp(line, "GET")) method_ = Method::Get;
    else if (!std::strcmp(line, "HEAD")) method_ = Method::Head;
    else if (!std::strcmp(line, "POST")) method_ = Method::Post;
    else if (!std::strcmp(line, "PUT")) method_ = Method::Put;
    else if (!std::strcmp(line, "PATCH")) method_ = Method::Patch;
    else if (!std::strcmp(line, "DELETE")) method_ = Method::Delete;
    else if (!std::strcmp(line, "OPTIONS")) method_ = Method::Options;
    else { fail(405); return; }
    if (!std::strcmp(end, "HTTP/1.1")) version_ = 1;
    else if (!std::strcmp(end, "HTTP/1.0")) version_ = 0;
    else { fail(505); return; }
    if (std::strlen(first) > MAX_TARGET) { fail(414); return; }
    if (*first != '/') { fail(400); return; }
    for (const unsigned char* p = reinterpret_cast<const unsigned char*>(first); *p; ++p)
      if (*p <= 32 || *p >= 127 || *p == '#' || *p == '\\') { fail(400); return; }
    pathAt_ = uint16_t(first - headers_);
    if (char* query = std::strchr(first, '?')) {
      *query++ = '\0';
      queryAt_ = uint16_t(query - headers_);
    }
    requestLine_ = true;
  }

  void parseHeader() {
    char* name = headers_ + lineStart_;
    if (whitespace(*name)) { fail(400); return; }
    char* colon = std::strchr(name, ':');
    if (!colon || colon == name) { fail(400); return; }
    *colon = '\0';
    for (const char* p = name; *p; ++p) if (!token(*p)) { fail(400); return; }
    for (size_t i = 0; i < count_; ++i)
      if (same(name, headerName(i))) { fail(400); return; }
    if (count_ == MAX_FIELDS) { fail(431); return; }
    char* value = colon + 1;
    while (whitespace(*value)) ++value;
    char* last = headers_ + used_ - 2;
    while (last > value && whitespace(last[-1])) *--last = '\0';
    fields_[count_++] = {uint16_t(name - headers_), uint16_t(value - headers_)};
    if (same(name, "Content-Length")) {
      if (!*value) { fail(400); return; }
      for (const char* p = value; *p; ++p)
        if (*p < '0' || *p > '9') { fail(400); return; }
      size_t length = 0;
      for (const char* p = value; *p; ++p) {
        const unsigned digit = unsigned(*p - '0');
        if (length > (MAX_BODY - digit) / 10) { fail(413); return; }
        length = length * 10 + digit;
      }
      lengthSeen_ = true;
      contentLength_ = length;
    } else if (same(name, "Transfer-Encoding")) fail(501);
    else if (same(name, "Expect")) fail(417);
    else if (same(name, "Content-Type") && prefix(value, "multipart/")) fail(415);
    else if (same(name, "Host")) {
      if (!authority(value)) { fail(400); return; }
      hostAt_ = uint16_t(value - headers_);
    }
  }

  void finishHeaders() {
    if (version_ == 1 && !hostAt_) { fail(400); return; }
    if (method_ == Method::Post && !lengthSeen_) { fail(411); return; }
    if ((method_ == Method::Get || method_ == Method::Head) && contentLength_) {
      fail(400); return;
    }
    headersDone_ = true;
    if (!contentLength_) { done_ = true; return; }
    if(!HttpMemoryBudget::available(contentLength_+1)){fail(503);return;}
    body_ = static_cast<char*>(allocate_(contentLength_ + 1));
    if (!body_) { fail(503); return; }
    body_[0] = '\0';
  }
};
}  // namespace HttpRequest
