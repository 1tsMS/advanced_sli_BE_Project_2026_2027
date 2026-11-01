// ============================================================
//  Telemetry Packet Formatter
//  Formats SensorData into the $T CSV telemetry string
//  sent to the PC at TELEMETRY_RATE_HZ.
//
//  Packet format:
//  $T,boomAngle,extensionMM,measuredLoad,actualLoad,swingAngle,
//     ropeLenMM,fsr1,fsr2,fsr3,fsr4,imuRoll,imuPitch,
//     safeLimit,loadPct,alarmLvl,ropeTotalMM,hookDropMM\n
//  (the last two were appended later, so older parsers that read 15 fields still line up)
// ============================================================
#ifndef TELEMETRY_FMT_H
#define TELEMETRY_FMT_H

#include "config.h"
#include "sensor_task.h"   // SensorDiag

/** RTOS health for the DBG report. */
struct RtosDiag {
    uint32_t    stackFree[5];   // bytes never used: sensor, hwbus, command, serial tx, storage
    uint32_t    txDropped;      // serial lines dropped (queue full)
    uint32_t    nvsWrites, nvsFailures;
    uint32_t    heapFree;
    const char* resetReason;
};

class TelemetryFormatter {
public:
    TelemetryFormatter() {}

    /**
     * Format a SensorData struct into the $T telemetry string.
     * @param data   Current sensor readings
     * @param buffer Output char buffer (must be >= 256 bytes)
     * @param bufLen Size of the output buffer
     * @return Number of chars written (excluding null terminator)
     */
    int format(const SensorData& data, char* buffer, size_t bufLen);

    /**
     * Format the debug report sent in answer to DBG.
     * Every line is "$D,<TAG>,<detail>,<status>" (FSRx / N20_TICKS: "$D,<TAG>,<value>"),
     * ending with "$D,END". Encoder lines come from SensorTask's diagnostics.
     */
    void formatDebugReport(
        bool mpuOK, bool hx711OK,
        const uint16_t fsr[4],
        long n20Ticks,
        const SensorDiag& diag,
        const struct RtosDiag& rtos,
        char* buffer, size_t bufLen
    );
};

#endif // TELEMETRY_FMT_H
