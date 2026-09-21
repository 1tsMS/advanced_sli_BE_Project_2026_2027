// In-memory stand-in for ESP32 NVS Preferences: data survives across Preferences objects,
// like flash does, so tests can simulate a power cycle by building fresh objects.
#pragma once
#include "Arduino.h"
#include <map>
#include <string>
#include <vector>

class Preferences {
public:
    static std::map<std::string, std::map<std::string, std::vector<uint8_t>>>& store() {
        static std::map<std::string, std::map<std::string, std::vector<uint8_t>>> s;
        return s;
    }
    static bool& failWrites() { static bool f = false; return f; }   // test hook: simulate a full/broken flash

    bool begin(const char* ns, bool readOnly = false) {
        _ns = ns; _ro = readOnly;
        if (readOnly && !store().count(_ns)) { _started = false; return false; }
        store()[_ns];
        _started = true;
        return true;
    }
    void end() { _started = false; }

    uint8_t getUChar(const char* k, uint8_t def = 0) {
        if (!_started || !store()[_ns].count(k)) return def;
        return store()[_ns][k][0];
    }
    size_t putUChar(const char* k, uint8_t v) {
        if (!_started || _ro || failWrites()) return 0;
        store()[_ns][k] = std::vector<uint8_t>(1, v);
        return 1;
    }
    size_t getBytesLength(const char* k) {
        if (!_started || !store()[_ns].count(k)) return 0;
        return store()[_ns][k].size();
    }
    size_t getBytes(const char* k, void* buf, size_t len) {
        if (!_started || !store()[_ns].count(k)) return 0;
        auto& v = store()[_ns][k];
        size_t n = len < v.size() ? len : v.size();
        memcpy(buf, v.data(), n);
        return n;
    }
    size_t putBytes(const char* k, const void* buf, size_t len) {
        if (!_started || _ro || failWrites()) return 0;
        const uint8_t* p = (const uint8_t*)buf;
        store()[_ns][k] = std::vector<uint8_t>(p, p + len);
        return len;
    }
    float getFloat(const char* k, float def = 0) { float v = def; if (_started && store()[_ns].count(k)) memcpy(&v, store()[_ns][k].data(), sizeof(v)); return v; }
    size_t putFloat(const char* k, float v) { return putBytes(k, &v, sizeof(v)); }
    bool getBool(const char* k, bool def = false) { return getUChar(k, def ? 1 : 0) != 0; }
    size_t putBool(const char* k, bool v) { return putUChar(k, v ? 1 : 0); }
private:
    std::string _ns; bool _ro = false; bool _started = false;
};
