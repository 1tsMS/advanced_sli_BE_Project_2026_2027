// Host-side stand-in for the ESP32 Arduino core: just enough to compile the firmware sources
// and unit-test their logic on a PC. It does NOT emulate any hardware.
#pragma once
#include <stdint.h>
#include <stddef.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <math.h>
#include <ctype.h>

#define PI 3.14159265358979f
#define IRAM_ATTR

typedef uint32_t TickType_t;
typedef int      SemaphoreHandle_t;
#define pdMS_TO_TICKS(x) ((TickType_t)(x))
#define pdTRUE 1

extern uint32_t g_fake_millis;
inline uint32_t millis() { return g_fake_millis; }
inline void delay(uint32_t) {}

// Test hook: when false, xSemaphoreTake times out (simulates a busy mutex)
extern bool g_mutex_available;
inline int  xSemaphoreTake(SemaphoreHandle_t, TickType_t) { return g_mutex_available ? 1 : 0; }
inline void xSemaphoreGive(SemaphoreHandle_t) {}
inline TickType_t xTaskGetTickCount() { return 0; }
inline void vTaskDelayUntil(TickType_t*, TickType_t) {}
inline void vTaskDelay(TickType_t) {}

struct HardwareSerial {
    int  available() { return 0; }
    int  read() { return -1; }
    template <class T> void print(T) {}
    void print(const char* s) { fputs(s, stdout); }
    template <class T> void println(T) {}
    void println(const char* s) { puts(s); }
    void println() {}
    void print(float, int) {}
    void begin(...) {}
};
extern HardwareSerial Serial;

inline void analogReadResolution(int) {}
inline int  analogRead(int) { return 0; }
#define SERIAL_8N1 0x800001c
