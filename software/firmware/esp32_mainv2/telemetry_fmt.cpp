// ============================================================
//  Telemetry Formatter — Implementation
// ============================================================
#include "telemetry_fmt.h"
#include <stdio.h>

int TelemetryFormatter::format(const SensorData& data, char* buffer, size_t bufLen) {
    // Format: $T,boomAngle,extensionMM,measuredLoad,actualLoad,swingAngle,
    //            ropeLenMM,fsr1,fsr2,fsr3,fsr4,imuRoll,imuPitch,
    //            safeLimit,loadPct,alarmLvl,ropeTotalMM,hookDropMM\n
    int written = snprintf(buffer, bufLen,
        "$T,%.2f,%.1f,%.2f,%.2f,%.2f,%.1f,%u,%u,%u,%u,%.2f,%.2f,%.2f,%.1f,%u,%.1f,%.1f\n",
        data.boomAngle,
        data.extensionMM,
        data.loadCellRaw,
        data.actualLoadKg,
        data.swingAngle,
        data.ropeLengthMM,
        data.fsr[0], data.fsr[1], data.fsr[2], data.fsr[3],
        data.imuRoll,
        data.imuPitch,
        data.safeLoadLimit,
        data.loadPercent,
        data.alarmLevel,
        data.ropeTotalMM,
        data.hookDropMM
    );
    return written;
}

// snprintf into buffer+pos, never past the end even if the report gets long
#define DBG_APPEND(...) do {                                              \
    if (pos < bufLen) {                                                   \
        int n = snprintf(buffer + pos, bufLen - pos, __VA_ARGS__);        \
        if (n > 0) pos += (size_t)n;                                      \
    }                                                                     \
} while (0)

static const char* magnetVerdict(const AS5600MagnetStatus& m) {
    if (!m.valid)     return "NO_DATA";
    if (!m.detected)  return "NO_MAGNET";
    if (m.tooStrong)  return "TOO_STRONG";   // magnet too close
    if (m.tooWeak)    return "TOO_WEAK";     // magnet too far / off-centre
    return "OK";
}

void TelemetryFormatter::formatDebugReport(
    bool mpuOK, bool hx711OK,
    const uint16_t fsr[4],
    long n20Ticks,
    const SensorDiag& diag,
    const RtosDiag& rtos,
    char* buffer, size_t bufLen
) {
    static const char* ENC_TAG[ENC_COUNT] = { "I2C0_SWING", "I2C1_BOOM", "I2C2_TELE" };
    static const char* AXIS[ENC_COUNT]    = { "SWING", "BOOM", "TELE" };
    size_t pos = 0;
    if (bufLen == 0) return;
    buffer[0] = '\0';

    // Presence (same tags as before so older dashboards still show them)
    for (uint8_t i = 0; i < ENC_COUNT; i++) {
        DBG_APPEND("$D,%s,0x%02X,%s\n", ENC_TAG[i], AS5600_ADDR, diag.enc[i].connected ? "OK" : "FAIL");
    }
    DBG_APPEND("$D,MPU6050,0x%02X,%s\n", MPU6050_ADDR, mpuOK ? "OK" : "FAIL");
    DBG_APPEND("$D,HX711,NA,%s\n", hx711OK ? "OK" : "FAIL");
    DBG_APPEND("$D,HX711_RAW,%ld,%s\n", (long)diag.hx711Raw, hx711OK ? "OK" : "FAIL");
    for (uint8_t i = 0; i < 4; i++) DBG_APPEND("$D,FSR%u,%u\n", (unsigned)(i + 1), (unsigned)fsr[i]);
    DBG_APPEND("$D,N20_TICKS,%ld\n", n20Ticks);

    for (uint8_t i = 0; i < ENC_COUNT; i++) {
        const EncoderDiag& e = diag.enc[i];
        // Magnet placement, as judged by the AS5600 itself
        DBG_APPEND("$D,MAG_%s,AGC:%u MAG:%u,%s\n", AXIS[i],
                   (unsigned)e.magnet.agc, (unsigned)e.magnet.magnitude, magnetVerdict(e.magnet));
        // I2C transfer quality and glitch rejections (NOISY above 1 % failed transfers)
        const bool noisy = e.reads > 100 && e.fails * 100 > e.reads;
        DBG_APPEND("$D,ENC_%s,reads:%lu fails:%lu rejected:%lu resync:%lu,%s\n", AXIS[i],
                   (unsigned long)e.reads, (unsigned long)e.fails,
                   (unsigned long)e.rejected, (unsigned long)e.resyncs,
                   !e.connected ? "FAIL" : noisy ? "NOISY" : "OK");
    }

    // Calibration stored on the ESP32 (the dashboard loads these: flash is the only source)
    DBG_APPEND("$D,AXIS_SWING,inv:%u ref:%.2f scale:%.4f,OK\n",
               diag.enc[ENC_SWING].inverted ? 1u : 0u, diag.enc[ENC_SWING].reference,
               diag.enc[ENC_SWING].scale);
    DBG_APPEND("$D,AXIS_BOOM,inv:%u ref:%.2f scale:%.4f src:%s,%s\n",
               diag.enc[ENC_BOOM].inverted ? 1u : 0u, diag.enc[ENC_BOOM].reference,
               diag.enc[ENC_BOOM].scale,
               diag.boomFromEncoder ? "ENC" : "IMU", diag.boomCalibrated ? "OK" : "UNCAL");
    DBG_APPEND("$D,AXIS_TELE,inv:%u scale:%.4f,OK\n",
               diag.enc[ENC_TELE].inverted ? 1u : 0u, diag.teleScale);
    DBG_APPEND("$D,AXIS_ROPE,inv:%u scale:%.6f counts:%ld,OK\n",
               diag.ropeInverted ? 1u : 0u, diag.ropeScale, (long)diag.ropeCounts);

    DBG_APPEND("$D,LOAD_CAL,scale:%.1f points:%u,%s\n",
               diag.loadScale, (unsigned)diag.loadPoints,
               diag.loadScaleCalibrated ? "OK" : "DEFAULT");

    // Sensor loop timing since the last DBG (period should stay ~10 ms)
    DBG_APPEND("$D,SENSOR_LOOP,avg:%.2fms max:%.2fms period_max:%.1fms,%s\n",
               diag.loopAvgMs, diag.loopMaxMs, diag.periodMaxMs,
               diag.periodMaxMs > 1.5f * SENSOR_PERIOD_MS ? "SLOW" : "OK");

    // Which loop stage is slow (worst ms since last DBG), and IMU dropout counters
    DBG_APPEND("$D,LOOP_STAGES,enc:%.1f save:%.1f mag:%.1f cal:%.1f hwbus:%.1f hx:%.1f imucal:%.1f,%s\n",
               diag.stageMaxMs[1], diag.stageMaxMs[2], diag.stageMaxMs[3], diag.stageMaxMs[4],
               diag.stageMaxMs[5], diag.stageMaxMs[6], diag.stageMaxMs[0],
               diag.periodMaxMs > 1.5f * SENSOR_PERIOD_MS ? "SLOW" : "OK");
    DBG_APPEND("$D,IMU_DROPOUTS,reinit:%lu rejected:%lu,%s\n",
               (unsigned long)diag.imuReconnects, (unsigned long)diag.imuRejected,
               diag.imuReconnects ? "NOISY" : "OK");

    // RTOS health: stack headroom per task (bytes never used), serial queue, flash, reset cause
    bool stackLow = false;
    for (int i = 0; i < 5; i++) if (rtos.stackFree[i] > 0 && rtos.stackFree[i] < 512) stackLow = true;
    DBG_APPEND("$D,STACKS,sensor:%lu hwbus:%lu cmd:%lu tx:%lu store:%lu,%s\n",
               (unsigned long)rtos.stackFree[0], (unsigned long)rtos.stackFree[1],
               (unsigned long)rtos.stackFree[2], (unsigned long)rtos.stackFree[3],
               (unsigned long)rtos.stackFree[4], stackLow ? "LOW" : "OK");
    DBG_APPEND("$D,SERIAL_TX,dropped:%lu,%s\n",
               (unsigned long)rtos.txDropped, rtos.txDropped ? "DROPS" : "OK");
    DBG_APPEND("$D,NVS,writes:%lu fails:%lu,%s\n",
               (unsigned long)rtos.nvsWrites, (unsigned long)rtos.nvsFailures,
               rtos.nvsFailures ? "FAIL" : "OK");
    DBG_APPEND("$D,RESET,%s heap:%lu,OK\n",
               rtos.resetReason ? rtos.resetReason : "?", (unsigned long)rtos.heapFree);

    DBG_APPEND("$D,END\n");
}
