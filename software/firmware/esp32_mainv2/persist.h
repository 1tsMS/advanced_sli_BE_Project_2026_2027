// ============================================================
//  Persistent settings: staged by real-time tasks, written by StorageTask
//
//  Writing flash (NVS) takes milliseconds and, on the ESP32, stalls code
//  running from flash on BOTH cores while a sector is written or erased.
//  So SensorTask / HwBusTask never call Preferences themselves: they copy the
//  new value into a staging area (a few bytes, under a spinlock) and mark it
//  dirty. StorageTask (core 0, lowest priority) wakes, waits a moment to
//  merge bursts of changes, and writes only the dirty groups.
//
//  Reading the settings at boot still happens directly in setup(), before
//  any task runs. Namespaces and keys are the same as the v1 firmware, so
//  calibration done with v1 carries over.
// ============================================================
#ifndef PERSIST_H
#define PERSIST_H

#include <Arduino.h>
#include "load_chart.h"

struct PersistPos {          // one axis position (sli_pos2)
    float    ref;
    int32_t  counts;         // raw encoder counts from the reference
    uint16_t raw;            // raw angle at save time (AS5600 axes)
};

struct PersistImu {          // IMU zero and axis mapping (sli_imu)
    float   boomOffset, tiltOffset;
    uint8_t boomSource, tiltSource;
    bool    boomInvert, tiltInvert;
};

struct PersistRotary {       // swing / boom encoder reference (sli_enc)
    uint16_t zeroRaw;
    float    ref;
    bool     inverted;
};

// Position slots: 0 swing, 1 boom, 2 telescope, 3 rope
void persist_setPosition(uint8_t slot, const PersistPos& p);
void persist_setTele(float scale, bool inverted);
void persist_setRope(float scale, bool inverted, float extRef);   // extRef NAN = not referenced
void persist_setImu(const PersistImu& imu);
void persist_setRotary(uint8_t idx, const PersistRotary& r, bool boomCalibrated);   // idx 0 swing, 1 boom
void persist_setTareOffset(int32_t offset);
void persist_setLoadScale(float countsPerKg);
void persist_setAxisScale(uint8_t idx, float unitsPerRev);   // idx 0 swing, 1 boom
void persist_setLoadChart(const LoadChart& chart);           // sli_lc / chart (blob)

/** StorageTask (core 0, lowest priority): the only writer of NVS. */
void storageTask(void* pvParameters);

/** Flash writes done / failed since boot (DBG). */
uint32_t persist_writes();
uint32_t persist_failures();

#endif // PERSIST_H
