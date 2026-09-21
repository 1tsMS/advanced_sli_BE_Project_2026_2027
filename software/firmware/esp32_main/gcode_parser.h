// ============================================================
//  G-Code Style Command Parser
//  Parses incoming serial commands from PC into MotorCommand
//  or calibration actions.
//
//  Supported commands:
//    T0              → Emergency stop (latched until RST)
//    RST             → Clear the E-stop latch and re-enable drivers
//    M1 S200 D1      → Swing motor, speed 200, direction 1
//                      (dead-man: repeat within MOTION_TIMEOUT_MS or it stops)
//    M2 S150 D0      → Boom lift, speed 150, direction 0
//    M3 S100 D1      → Telescope, speed 100, direction 1
//    M4 S200 D0      → Winch, speed 200, direction 0
//    M0 A1           → Stop axis 1 (swing)
//    CAL0            → Tare load cell
//    CAL1            → Calibrate IMU
//    CAL2            → Reset telescope encoder
//    CAL3 W<kg>      → Record load-cell correction: known weight hanging at the current boom angle
//    CAL3 CLEAR      → Remove all recorded load-cell corrections
//    DBG             → Request debug/I2C scan report
//    CFG RATE 50     → Set telemetry rate
//    LC ...          → Load chart upload / read-back (see load_chart.h)
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
    CMD_RESET,         // RST (clear the E-stop latch)
    CMD_MOTOR,         // M1-M4
    CMD_STOP_AXIS,     // M0 Ax
    CMD_CALIBRATE,     // CAL0, CAL1, CAL2
    CMD_DEBUG,         // DBG
    CMD_CONFIG,        // CFG
    CMD_LOADCHART      // LC UPLOAD / LC <a>,<e>,<l> / LC SAVE / LC GET
};

/**
 * Sub-operation of an LC command (ParsedCommand::lcOp).
 * LC_OP_INVALID means the line started with "LC" but was malformed.
 */
enum LoadChartOp : uint8_t {
    LC_OP_INVALID = 0,
    LC_OP_UPLOAD,
    LC_OP_ENTRY,
    LC_OP_SAVE,
    LC_OP_GET
};

/**
 * Parsed command structure containing all extracted fields.
 */
struct ParsedCommand {
    CommandType type;
    MotorCommand motor;       // Filled for CMD_MOTOR / CMD_STOP_AXIS / CMD_ESTOP
    CalCommand   calType;     // Filled for CMD_CALIBRATE
    uint16_t     configRate;  // Filled for CMD_CONFIG (telemetry rate Hz)
    int8_t       boomAxis;    // Filled for CAL_ZERO_IMU (0=X/roll, 1=Y/pitch)
    int8_t       boomInv;     // 0 or 1
    int8_t       tiltAxis;    // 0=X/roll, 1=Y/pitch
    int8_t       tiltInv;     // 0 or 1
    float        teleScale;   // Filled for CAL_RESET_TELE (scale in mm/rev)
    int8_t       teleInvert;  // Filled for CAL_RESET_TELE (0=normal, 1=inverted, -1=keep)
    float        calWeight;   // CAL_LOAD_GAIN: known weight hanging on the hook (kg)
    bool         calClear;    // CAL_LOAD_GAIN: remove all recorded angle corrections
    uint8_t      lcOp;        // Filled for CMD_LOADCHART (LoadChartOp)
    uint16_t     lcCount;     // LC_OP_UPLOAD: number of entries that will follow
    float        lcAngle;     // LC_OP_ENTRY: boom angle (deg)
    float        lcExtension; // LC_OP_ENTRY: extension (mm)
    float        lcLimit;     // LC_OP_ENTRY: safe load limit (kg)
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
