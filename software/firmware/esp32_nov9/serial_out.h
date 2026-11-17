// ============================================================
//  Serial output queue: ONE task owns the USB serial TX
//
//  Every task that wants to report something (ACKs, DBG lines, boot info)
//  formats a complete line and drops it into a FreeRTOS queue without
//  waiting. SerialTxTask (core 0) is the only task that writes to Serial: it
//  sends queued lines and the periodic $T telemetry.
//
//  Why: Serial.write() blocks while the UART buffer is full. If SensorTask
//  on core 1 printed directly it could stall behind a 1.5 KB DBG report or
//  telemetry, and lines from different tasks could interleave. With a queue,
//  real-time tasks never block on serial and every line arrives whole.
//  If the queue is full a line is dropped (counted, shown in DBG) rather
//  than blocking a real-time task.
// ============================================================
#ifndef SERIAL_OUT_H
#define SERIAL_OUT_H

#include <Arduino.h>

#define SERIAL_LINE_MAX   128   // bytes per queued line, including '\n'
#define SERIAL_QUEUE_LEN  48

/** Create the queue. Call in setup() before any task starts. */
void serialOut_init();

/** Format one line (newline added) and queue it. Never blocks. Safe from any task. */
void serialLine(const char* fmt, ...) __attribute__((format(printf, 1, 2)));

/**
 * Queue a multi-line block (e.g. the DBG report), one queue item per line.
 * Waits up to `waitPerLine` ticks for space so a long report isn't dropped;
 * only for non-real-time callers (CommandTask).
 */
void serialOut_sendBlock(const char* text, TickType_t waitPerLine);

/** SerialTxTask only: take the next queued line, waiting up to `wait` ticks. */
bool serialOut_receive(char* line, TickType_t wait);

/** Lines dropped because the queue was full (shown in DBG). */
uint32_t serialOut_dropped();

#endif // SERIAL_OUT_H
