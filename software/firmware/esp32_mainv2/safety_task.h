// ============================================================
//  SafetyTask — the SLI (core 1, prio 4, 50 Hz)
//
//  Every 20 ms: reads the pose + load from SensorData, runs sli_evaluate()
//  against the active load chart, publishes the result (load %, capacity,
//  alarm level into SensorData; blocked-motion mask for CommandTask) and
//  reports a $S status line at 5 Hz and on every change.
//
//  Enforcement happens in CommandTask, the only writer of the Mega link:
//  it asks safety_motionAllowed() before forwarding a motion, and when the
//  mask gains a bit SafetyTask wakes it to stop any axis already moving in a
//  now-blocked direction. Worst case from "limit reached" to "M0 sent" is one
//  SafetyTask period (20 ms) plus the UART line to the Mega (~10 ms).
//
//  Also owned here:
//    - E-stop latch: T0 latches, every motion is refused until RST
//    - Timed bypass: SLI BYPASS <s> (max 600) disables the cut, never the
//      E-stop or dead-man; it lapses by itself and on every reboot
//    - The active load chart (RAM copy + flash via StorageTask)
//
//  Status line:  $S,<OK|WARN|CUT|FAULT|ESTOP>,<pct>,<capKg>,<zone>,<bypassLeftS>,<blockedHex>,<chart>,<ARMED|BYPASS|OFF>
//  (state + blocked mask are always computed; the mode says whether they are enforced)
// ============================================================
#ifndef SAFETY_TASK_H
#define SAFETY_TASK_H

#include <Arduino.h>

#define SAFETY_BYPASS_MAX_S  600

/** setup(): restore the load chart from flash (before the tasks start). */
void safety_init();

void safetyTask(void* pvParameters);

/** CommandTask: may axis 1-4 move in direction dir? reason = why not ("ESTOP", "CUT", ...). */
bool safety_motionAllowed(uint8_t axis, uint8_t dir, const char** reason);

void safety_estop();               // latch
bool safety_estopLatched();
void safety_reset();               // clear the latch (RST)

void safety_bypass(uint16_t seconds);
void safety_arm();                 // end bypass / OFF now
/** SLI OFF: no motion cut at all until SLI ON or the next reboot (not saved, so a
 *  power cycle always comes back armed). E-stop latch and dead-man still work. */
void safety_off();

/** CommandTask only: one "LC ..." line (args = text after "LC"). Sends its own $ACK. */
void safety_handleLc(const char* args);

#endif // SAFETY_TASK_H
