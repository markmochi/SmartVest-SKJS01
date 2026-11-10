#pragma once
#include <ArduinoJson.h>
#include "HttpMemoryBudget.h"
#include <cstdlib>

// Stateless allocator: parsed request documents may outlive readJsonBody().
// Every growth preserves the same internal-heap reserve as the HTTP writer.
class JsonRequestMemory final:public ArduinoJson::Allocator {
 public:
  void* allocate(size_t bytes)override{return HttpMemoryBudget::available(bytes)?std::malloc(bytes):nullptr;}
  void deallocate(void* pointer)override{std::free(pointer);}
  void* reallocate(void* pointer,size_t bytes)override{
    if(!bytes){std::free(pointer);return nullptr;}
    return HttpMemoryBudget::available(bytes)?std::realloc(pointer,bytes):nullptr;
  }
};
