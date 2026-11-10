#pragma once
#include "Arduino.h"
using TickType_t=uint32_t;using TaskHandle_t=void*;
constexpr int pdTRUE=1,pdPASS=1;inline uint32_t pdMS_TO_TICKS(uint32_t n){return n;}
inline uint32_t xTaskGetTickCount(){return millis();}
inline void vTaskDelayUntil(uint32_t* w,uint32_t p){*w+=p;clockMs=*w;}
inline int xTaskCreatePinnedToCore(void(*)(void*),const char*,uint32_t,void*,int,TaskHandle_t* h,int){*h=(void*)1;return pdPASS;}
