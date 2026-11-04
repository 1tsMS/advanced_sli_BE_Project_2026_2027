// ============================================================
//  G-Code Style Command Parser
//  Parses incoming serial commands from PC into MotorCommand
//  or calibration actions.
//
//  Supported commands:
//    T0              → Emergency stop
//    M1 S200 D1      → Swing motor, speed 200, direction 1
//    M2 S150 D0      → Boom lift, speed 150, direction 0
//    M3 S100 D1      → Telescope, speed 100, direction 1
//    M4 S200 D0      → Winch, speed 200, direction 0
//    M0 A1           → Stop axis 1 (swing)
//    CAL0            → Tare load cell
//    CAL1            → Calibrate IMU
//    CAL2            → Reset telescope encoder (CAL2 S<mm/rev> I<0|1> sets scale/direction)
//    CAL4 A2 V0      → Encoder axis 2 (boom) is at 0 deg right now (A1 swing, A3 telescope mm)
//    CAL5 A1 I1      → Invert encoder axis 1 (swing)
//    DBG             → Request debug/I2C scan report
//    CFG RATE 50     → Set telemetry rate
//    LC ...          → Load chart upload / readback (see load_chart.h)
//    SLI ON          → SLI armed (ends a bypass)
//    SLI BYPASS 300  → Disable the motion cut for 300 s (max 600; E-stop still works)
//    SLI OFF         → Disable the motion cut until SLI ON or reboot (E-stop still works)
//    RST             → Clear a latched E-stop
//    SHP 1 [L<mm>]   → Swing input shaping on (L = pendulum length override, 0/omitted = from hook drop)
//    SHP 0           → Swing input shaping off
// ============================================================
#ifndef GCODE_PARSER_H
#define GCODE_PARSER_H

#include "config.h"

/**
 * Result of parsing a G-code command line.
 */
enum ParseResult {
    PARSE_OK,
    PARSE_EMPTY,
    PARSE_UNKNOWN_CMD,
    PARSE_INVALID_PARAMS
};

/**
 * The type of command that was parsed.
 */
enum CommandType {
    CMD_NONE,
    CMD_ESTOP,         // T0
    CMD_MOTOR,         // M1-M4
    CMD_STOP_AXIS,     // M0 Ax
    CMD_CALIBRATE,     // CAL0, CAL1, CAL2
    CMD_DEBUG,         // DBG
    CMD_CONFIG,        // CFG
    CMD_LOADCHART,     // LC ...
    CMD_SLI,           // SLI ON / SLI BYPASS <s>
    CMD_RESET,         // RST
    CMD_SHAPE          // SHP 0 / SHP 1 [L<mm>]
};

/**
 * Parsed command structure containing all extracted fields.
 */
struct ParsedCommand {
    CommandType type;
    MotorCommand motor;       // Filled for CMD_MOTOR / CMD_STOP_AXIS / CMD_ESTOP
    CalCommand   calType;     // Filled for CMD_CALIBRATE
    uint16_t     configRate;  // Filled for CMD_CONFIG (telemetry rate Hz)
    int8_t       boomAxis;    // Filled for CAL_ZERO_IMU (0=X, 1=Y, 2=Z)
    int8_t       boomInv;     // 0 or 1
    int8_t       tiltAxis;    // 0=X, 1=Y, 2=Z
    int8_t       tiltInv;     // 0 or 1
    float        teleScale;   // Filled for CAL_RESET_TELE (scale in mm/rev)
    int8_t       teleInvert;  // Filled for CAL_RESET_TELE (0=normal, 1=inverted, -1=keep)
    uint8_t      encAxis;     // Filled for CAL_ENC_REF / CAL_ENC_INVERT (0=swing, 1=boom, 2=tele)
    float        encValue;    // Filled for CAL_ENC_REF (deg, or mm for the telescope)
    int8_t       encInvert;   // Filled for CAL_ENC_INVERT / CAL_AXIS_SCALE (0 or 1, -1 = keep)
    uint8_t      calSub;      // CAL_LOAD: 0 = record weight W, 1 = CLEAR points, 2 = set scale S
    const char*  args;        // CMD_LOADCHART: text after "LC" (points into the caller's line)
    uint16_t     bypassS;     // CMD_SLI: bypass seconds, 0 = arm
    bool         sliOff;      // CMD_SLI: SLI OFF
    bool         shapeOn;     // CMD_SHAPE
    float        shapeLenMm;  // CMD_SHAPE: pendulum length override, 0 = from hook drop
};

class GCodeParser {
public:
    GCodeParser() {}

    /**
     * Parse a single line of G-code text into a ParsedCommand.
     * @param line  Null-terminated string (e.g., "M1 S200 D1\n")
     * @param out   Parsed result
     * @return PARSE_OK on success
     */
    ParseResult parse(const char* line, ParsedCommand* out);

private:
    /**
     * Extract an integer parameter from the line.
     * Looks for prefix char (e.g., 'S') followed by a number.
     * @return the number, or defaultVal if not found
     */
    int extractParam(const char* line, char prefix, int defaultVal);

    /**
     * Extract a floating point parameter from the line.
     */
    float extractFloatParam(const char* line, char prefix, float defaultVal);
};

#endif // GCODE_PARSER_H
