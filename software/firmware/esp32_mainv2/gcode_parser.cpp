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
            case '4':
            case '5': {
                // Note: "CAL" itself contains an 'A', so look for the axis after the digit
                const int axis = extractParam(line + 4, 'A', 0);
                if (axis < 1 || axis > 4) return PARSE_INVALID_PARAMS;   // A4 = rope (winch encoder)
                out->encAxis = (uint8_t)(axis - 1);
                if (digit == '4') {
                    out->calType  = CAL_ENC_REF;
                    out->encValue = extractFloatParam(line + 4, 'V', 0.0f);
                } else {
                    const int inv = extractParam(line + 4, 'I', -1);
                    if (inv != 0 && inv != 1) return PARSE_INVALID_PARAMS;
                    out->calType   = CAL_ENC_INVERT;
                    out->encInvert = (int8_t)inv;
                }
                return PARSE_OK;
            }
            case '7': {
                out->calType = CAL_LOAD;
                if (strstr(line + 4, "CLEAR")) { out->calSub = 1; return PARSE_OK; }
                const float w = extractFloatParam(line + 4, 'W', 0.0f);
                if (w > 0.0f) { out->calSub = 0; out->encValue = w; return PARSE_OK; }
                const float sc = extractFloatParam(line + 4, 'S', 0.0f);
                if (sc != 0.0f) { out->calSub = 2; out->encValue = sc; return PARSE_OK; }
                return PARSE_INVALID_PARAMS;
            }
            case '8': {
                const int axis = extractParam(line + 4, 'A', 0);
                if (axis < 1 || axis > 3) return PARSE_INVALID_PARAMS;
                out->calType   = CAL_AXIS_SCALE;
                out->encAxis   = (uint8_t)(axis - 1);
                out->encValue  = extractFloatParam(line + 4, 'S', 0.0f);
                out->encInvert = (int8_t)extractParam(line + 4, 'I', -1);
                if (!(out->encValue > 0.0f)) return PARSE_INVALID_PARAMS;
                return PARSE_OK;
            }
            case '6': {
                out->calType  = CAL_ROPE_SCALE;
                out->encAxis  = 3;
                out->encValue = extractFloatParam(line + 4, 'S', 0.0f);
                if (!(out->encValue > 0.0f)) return PARSE_INVALID_PARAMS;
                return PARSE_OK;
            }
            default:  return PARSE_INVALID_PARAMS;
        }
    }

    // --- LC ...: load chart (the safety module parses the rest) ---
    if (line[0] == 'L' && line[1] == 'C' && (line[2] == ' ' || line[2] == '\0')) {
        out->type = CMD_LOADCHART;
        out->args = line + 2;
        return PARSE_OK;
    }

    // --- SLI ON / SLI BYPASS <s> ---
    if (strncmp(line, "SLI", 3) == 0) {
        out->type = CMD_SLI;
        out->sliOff = false;
        if (strstr(line + 3, "OFF")) { out->sliOff = true; return PARSE_OK; }
        if (strstr(line + 3, "ON"))  { out->bypassS = 0; return PARSE_OK; }
        const char* b = strstr(line + 3, "BYPASS");
        if (b) {
            const int s = atoi(b + 6);
            if (s <= 0) return PARSE_INVALID_PARAMS;
            out->bypassS = (uint16_t)(s > 600 ? 600 : s);
            return PARSE_OK;
        }
        return PARSE_INVALID_PARAMS;
    }

    // --- SHP 0 / SHP 1 [L<mm>]: swing input shaping ---
    if (strncmp(line, "SHP", 3) == 0) {
        const char* p = line + 3;
        while (*p == ' ') p++;
        if (*p != '0' && *p != '1') return PARSE_INVALID_PARAMS;
        out->type       = CMD_SHAPE;
        out->shapeOn    = (*p == '1');
        out->shapeLenMm = extractFloatParam(p + 1, 'L', 0.0f);
        if (out->shapeLenMm < 0.0f) return PARSE_INVALID_PARAMS;
        return PARSE_OK;
    }

    // --- RST: clear a latched E-stop ---
    if (strncmp(line, "RST", 3) == 0) {
        out->type = CMD_RESET;
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
