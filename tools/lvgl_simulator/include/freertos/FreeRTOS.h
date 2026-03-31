#pragma once

typedef int BaseType_t;
typedef unsigned int UBaseType_t;
typedef unsigned int TickType_t;

#ifndef pdPASS
#define pdPASS 1
#endif

#ifndef pdMS_TO_TICKS
#define pdMS_TO_TICKS(ms) (ms)
#endif
