// ============================================================
//  SerialTxTask (core 0, priority 4) — the only writer of USB serial
//
//  Blocks on the serial-out queue; every queued line (ACK, DBG, boot info)
//  is written as soon as it arrives. When the telemetry period is due it
//  snapshots SensorData (under sensorMutex) and writes the $T packet.
//  One writer means lines never interleave and no real-time task ever
//  blocks on the UART.
// ============================================================
#ifndef TELEMETRY_TASK_H
#define TELEMETRY_TASK_H

#include "config.h"

void telemetryTask(void* pvParameters);

/** Change the $T rate (CFG RATE <hz>). Safe from any task. */
void telemetryTask_setRate(uint16_t hz);

#endif // TELEMETRY_TASK_H
