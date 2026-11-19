// ============================================================
//  PC unit tests for esp32_mainv2/loadcell_health.h
//
//  Build + run from software/firmware:
//    g++ -std=c++17 -Wall -Wextra -I esp32_mainv2 -I host_tests host_tests/test_loadcell_health.cpp -o host_tests/test_loadcell_health && host_tests/test_loadcell_health
// ============================================================
#include "loadcell_health.h"
#include "check.h"
#include <string>

static const int32_t OFFSET = 120000;
static const float   SCALE  = 122000.0f;   // counts per kg
static float kg(int32_t raw) { return (raw - OFFSET) / SCALE; }

static void test_live_cell_is_ok() {
    LoadCellMonitor m;
    for (int i = 0; i < 100; i++) {
        const int32_t raw = OFFSET + 61000 + ((i * 37) % 41) - 20;   // 0.5 kg, +-20 counts noise
        m.push(raw, kg(raw));
    }
    CHECK(m.health() == LC_HEALTH_OK);
    CHECK(m.lastSpan() > 2);
}

static void test_open_signal_wire_saturates() {
    LoadCellMonitor m;
    m.push(8388607, kg(8388607));
    m.push(8388607, kg(8388607));
    CHECK(m.health() == LC_HEALTH_OK);           // two rail samples: not yet
    m.push(8388607, kg(8388607));
    CHECK(m.health() == LC_HEALTH_SATURATED);
    m.push(OFFSET + 10, kg(OFFSET + 10));        // reconnected: clears at once
    CHECK(m.health() == LC_HEALTH_OK);
    for (int i = 0; i < 3; i++) m.push(-8388608, kg(-8388608));   // negative rail too
    CHECK(m.health() == LC_HEALTH_SATURATED);
}

static void test_dead_bridge_is_frozen() {
    LoadCellMonitor m;
    for (int i = 0; i < 29; i++) m.push(OFFSET, kg(OFFSET));
    CHECK(m.health() == LC_HEALTH_OK);           // window not full yet
    m.push(OFFSET, kg(OFFSET));
    CHECK(m.health() == LC_HEALTH_FROZEN);
    for (int i = 0; i < 30; i++) m.push(OFFSET + (i % 7) * 5, kg(OFFSET));   // noise is back
    CHECK(m.health() == LC_HEALTH_OK);
}

static void test_lost_excitation_reads_negative() {
    LoadCellMonitor m;
    const int32_t raw = OFFSET - 200000;         // -1.6 kg: nothing pushes a hook up
    for (int i = 0; i < 9; i++) m.push(raw + (i % 3) * 10, kg(raw));
    CHECK(m.health() == LC_HEALTH_OK);
    m.push(raw, kg(raw));
    CHECK(m.health() == LC_HEALTH_NEGATIVE);
    m.push(OFFSET + 5, kg(OFFSET + 5));          // one normal sample clears it
    CHECK(m.health() == LC_HEALTH_OK);
    CHECK(std::string(loadCellHealthName(LC_HEALTH_NEGATIVE)) == "NEGATIVE");
}

static void test_unplugged_cell_reads_near_zero() {
    // Crane, 2026-11-09: all 4 wires out -> raw 2161..2225, noise ~60 counts
    LoadCellMonitor m;
    const int32_t zero = 150000;                 // this cell's tare offset (far from 0)
    for (int i = 0; i < 9; i++) m.push(2180 + (i % 5) * 10, (2180 - zero) / SCALE, zero);
    CHECK(m.health() == LC_HEALTH_OK);           // under 1 s: not yet
    m.push(2190, (2190 - zero) / SCALE, zero);
    CHECK(m.health() == LC_HEALTH_OPEN);
    // New board, 16 Nov: white (S-) wire broken -> raw 4400..5700 against the cell's zero 574841
    LoadCellMonitor w;
    for (int i = 0; i < 10; i++) w.push(4400 + i * 130, (4400 + i * 130 - 574841) / SCALE, 574841);
    CHECK(w.health() == LC_HEALTH_OPEN);
    // Same cell fixed, box hanging: raw ~1024500 -> OK
    LoadCellMonitor f;
    for (int i = 0; i < 40; i++) f.push(1022447 + (i % 7) * 650, (1022447 - 574841) / SCALE, 574841);
    CHECK(f.health() == LC_HEALTH_OK);
    // New cell, 19 Nov: zero -51228. A real ~0.12 kg load puts raw near 0 (+5000):
    // must stay OK (the reading is ABOVE the tare zero), even for many samples
    LoadCellMonitor c;
    for (int i = 0; i < 40; i++) c.push(5000 + (i % 5) * 40, (5000 + 51228) / SCALE, -51228);
    CHECK(c.health() == LC_HEALTH_OK);
    // A cell whose own zero is near 0 can't be told apart: never flagged
    LoadCellMonitor n;
    for (int i = 0; i < 40; i++) n.push(2180 + (i % 5) * 10, 0.0f, 3000);
    CHECK(n.health() == LC_HEALTH_OK);
}

int main() {
    test_live_cell_is_ok();
    test_open_signal_wire_saturates();
    test_dead_bridge_is_frozen();
    test_lost_excitation_reads_negative();
    test_unplugged_cell_reads_near_zero();
    return check_summary("loadcell_health");
}
