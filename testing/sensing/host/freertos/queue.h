#pragma once
#include "FreeRTOS.h"
struct FakeQueue{size_t capacity,size;std::deque<std::vector<uint8_t>> items;};using QueueHandle_t=FakeQueue*;
inline QueueHandle_t xQueueCreate(size_t capacity,size_t size){return new FakeQueue{capacity,size,{}};}
inline int xQueueSend(QueueHandle_t q,const void* p,int){if(q->items.size()==q->capacity)return 0;q->items.emplace_back((const uint8_t*)p,(const uint8_t*)p+q->size);return 1;}
inline int xQueuePeek(QueueHandle_t q,void* p,int){if(q->items.empty())return 0;memcpy(p,q->items.front().data(),q->size);return 1;}
inline int xQueueReceive(QueueHandle_t q,void* p,int n){if(!xQueuePeek(q,p,n))return 0;q->items.pop_front();return 1;}
inline int xQueueOverwrite(QueueHandle_t q,const void* p){q->items.clear();return xQueueSend(q,p,0);}
