// ============================================================
//  G-Code Parser — Implementation
// ============================================================
#include "gcode_parser.h"
#include <string.h>
#include <stdlib.h>
#include <ctype.h>

ParseResult GCodeParser::parse(const char* line, ParsedCommand* out) {
    // Clear output
    memset(out, 0, sizeof(ParsedCommand));
    out->type = CMD_NONE;

    // Skip leading whitespace
    while (*line == ' ' || *line == '\t') line++;

    // Empty or newline-only
    if (*line == '\0' || *line == '\n' || *line == '\r') {
        return PARSE_EMPTY;
    }

    // --- T0: Emergency Stop ---
    if (strncmp(line, "T0", 2) == 0) {
        out->type = CMD_ESTOP;
        out->motor.isEstop = true;
        return PARSE_OK;
    }

    // --- RST: Clear E-stop latch ---
    if (strncmp(line, "RST", 3) == 0) {
        out->type = CMD_RESET;
        return PARSE_OK;
    }

    // --- M0 Ax: Stop specific axis ---
    if (strncmp(line, "M0", 2) == 0) {
        out->type = CMD_STOP_AXIS;
        out->motor.axis = 0;
        out->motor.speed = 0;
        out->motor.stopAxis = (uint8_t)extractParam(line, 'A', 0);
        return PARSE_OK;
    }

    // --- M1-M4: Motor commands ---
    if (line[0] == 'M' && line[1] >= '1' && line[1] <= '4') {
        out->type = CMD_MOTOR;
        out->motor.axis = line[1] - '0';
        out->motor.speed = (uint16_t)extractParam(line, 'S', 0);
        out->motor.direction = (uint8_t)extractParam(line, 'D', 0);
        out->motor.isEstop = false;

        // Clamp speed to 0-MOTOR_SPEED_MAX
        if (out->motor.speed > MOTOR_SPEED_MAX) out->motor.speed = MOTOR_SPEED_MAX;
        // Clamp direction to 0 or 1
        if (out->motor.direction > 1) out->motor.direction = 1;

        return PARSE_OK;
    }

    // --- CAL0, CAL1, CAL2: Calibration ---
    if (strncmp(line, "CAL", 3) == 0) {
        out->type = CMD_CALIBRATE;
        char digit = line[3];
        switch (digit) {
            case '0': out->calType = CAL_TARE_LOAD;  return PARSE_OK;
            case '1': {
                out->calType  = CAL_ZERO_IMU;
                out->boomAxis = (int8_t)extractParam(line, 'B', 0);
                out->boomInv  = (int8_t)extractParam(line, 'I', 0);
                out->tiltAxis = (int8_t)extractParam(line, 'T', 1);
                out->tiltInv  = (int8_t)extractParam(line, 'Q', 0);
                return PARSE_OK;
            }
            case '2': {
                out->calType    = CAL_RESET_TELE;
                out->teleScale  = extractFloatParam(line, 'S', 0.0f);
                out->teleInvert = (int8_t)extractParam(line, 'I', -1);
                return PARSE_OK;
            }
            case '3': {
                out->calType  = CAL_LOAD_GAIN;
                out->calClear = (strstr(line, "CLEAR") != NULL);
                out->calWeight = extractFloatParam(line, 'W', 0.0f);
                // Recording needs a weight; CLEAR needs nothing
                if (!out->calClear && out->calWeight <= 0.0f) return PARSE_INVALID_PARAMS;
                return PARSE_OK;
            }
            default:  return PARSE_INVALID_PARAMS;
        }
    }

    // --- LC: Load chart upload / read-back ---
    // Malformed LC lines still return PARSE_OK with LC_OP_INVALID so the
    // command task can answer with $LC,ERR instead of dropping them silently.
    if (line[0] == 'L' && line[1] == 'C' && (line[2] == ' ' || line[2] == '\0')) {
        out->type = CMD_LOADCHART;
        out->lcOp = LC_OP_INVALID;
        const char* p = line + 2;
        while (*p == ' ') p++;

        if (strncmp(p, "UPLOAD", 6) == 0) {
            out->lcOp    = LC_OP_UPLOAD;
            out->lcCount = (uint16_t)atoi(p + 6);
        } else if (strncmp(p, "SAVE", 4) == 0) {
            out->lcOp = LC_OP_SAVE;
        } else if (strncmp(p, "GET", 3) == 0) {
            out->lcOp = LC_OP_GET;
        } else {
            // "<angle>,<extension>,<limit>"
            char* end;
            float a = strtof(p, &end);
            if (end != p && *end == ',') {
                p = end + 1;
                float e = strtof(p, &end);
                if (end != p && *end == ',') {
                    p = end + 1;
                    float l = strtof(p, &end);
                    if (end != p) {
                        out->lcOp        = LC_OP_ENTRY;
                        out->lcAngle     = a;
                        out->lcExtension = e;
                        out->lcLimit     = l;
                    }
                }
            }
        }
        return PARSE_OK;
    }

    // --- DBG: Debug request ---
    if (strncmp(line, "DBG", 3) == 0) {
        out->type = CMD_DEBUG;
        return PARSE_OK;
    }

    // --- CFG RATE xx: Configuration ---
    if (strncmp(line, "CFG", 3) == 0) {
        out->type = CMD_CONFIG;
        // Look for "RATE" keyword
        const char* rateStr = strstr(line, "RATE");
        if (rateStr) {
            rateStr += 4;  // Skip "RATE"
            while (*rateStr == ' ') rateStr++;  // Skip spaces
            out->configRate = (uint16_t)atoi(rateStr);
            if (out->configRate == 0) out->configRate = TELEMETRY_RATE_HZ;
            return PARSE_OK;
        }
        return PARSE_INVALID_PARAMS;
    }

    return PARSE_UNKNOWN_CMD;
}

int GCodeParser::extractParam(const char* line, char prefix, int defaultVal) {
    // Search for the prefix character in the line
    const char* p = line;
    while (*p) {
        if (toupper(*p) == toupper(prefix)) {
            p++;
            // Skip spaces between prefix and number
            while (*p == ' ') p++;
            if (isdigit(*p) || *p == '-') {
                return atoi(p);
            }
        }
        p++;
    }
    return defaultVal;
}

float GCodeParser::extractFloatParam(const char* line, char prefix, float defaultVal) {
    const char* p = line;
    while (*p) {
        if (toupper(*p) == toupper(prefix)) {
            p++;
            while (*p == ' ') p++;
            if (isdigit(*p) || *p == '-' || *p == '.') {
                return (float)atof(p);
            }
        }
        p++;
    }
    return defaultVal;
}
