// ============================================================
//  Serial output queue — Implementation
// ============================================================
#include "serial_out.h"
#include <stdarg.h>
#include <stdio.h>
#include <string.h>

static QueueHandle_t     _txQueue = nullptr;
static volatile uint32_t _dropped = 0;

void serialOut_init() {
    if (!_txQueue) _txQueue = xQueueCreate(SERIAL_QUEUE_LEN, SERIAL_LINE_MAX);
}

static void enqueue(const char* line, TickType_t wait) {
    if (!_txQueue) {                 // before serialOut_init (early boot): print directly
        Serial.print(line);
        return;
    }
    if (xQueueSend(_txQueue, line, wait) != pdTRUE) _dropped = _dropped + 1;
}

void serialLine(const char* fmt, ...) {
    char buf[SERIAL_LINE_MAX];
    va_list ap;
    va_start(ap, fmt);
    int n = vsnprintf(buf, sizeof(buf) - 1, fmt, ap);
    va_end(ap);
    if (n < 0) return;
    if (n > (int)sizeof(buf) - 2) n = sizeof(buf) - 2;   // truncated: still end the line
    buf[n]     = '\n';
    buf[n + 1] = '\0';
    enqueue(buf, 0);
}

void serialOut_sendBlock(const char* text, TickType_t waitPerLine) {
    char line[SERIAL_LINE_MAX];
    while (*text) {
        const char* nl = strchr(text, '\n');
        size_t len = nl ? (size_t)(nl - text) : strlen(text);
        if (len > SERIAL_LINE_MAX - 2) len = SERIAL_LINE_MAX - 2;
        memcpy(line, text, len);
        line[len]     = '\n';
        line[len + 1] = '\0';
        if (len > 0) enqueue(line, waitPerLine);
        if (!nl) break;
        text = nl + 1;
    }
}

bool serialOut_receive(char* line, TickType_t wait) {
    return _txQueue && xQueueReceive(_txQueue, line, wait) == pdTRUE;
}

uint32_t serialOut_dropped() { return _dropped; }
