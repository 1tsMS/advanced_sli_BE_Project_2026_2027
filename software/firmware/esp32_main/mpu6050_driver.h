// ============================================================
//  MPU6050 6-Axis IMU Driver — Backed by Adafruit_MPU6050
//  Uses Hardware Wire (Bus 0, GPIO 21/22) — confirmed working
//  on this bus from mpu_hx711_test.ino.
//
//  Range settings mirror working test code:
//    Accelerometer: ±8g
//    Gyroscope:     ±500°/s
//    Filter:        21 Hz DLPF bandwidth
//
//  Applies complementary filter for stable roll/pitch estimation.
// ============================================================
#ifndef MPU6050_DRIVER_H
#define MPU6050_DRIVER_H

#include "config.h"
#include <Wire.h>
#include <Adafruit_MPU6050.h>
#include <Adafruit_Sensor.h>

class MPU6050Driver {
public:
    MPU6050Driver()
        : _connected(false),
          _roll(0.0f), _pitch(0.0f),
          _gyroOffsetX(0.0f), _gyroOffsetY(0.0f),
          _lastUpdateUs(0) {}

    /** Initialize sensor on Wire using Adafruit_MPU6050. */
    bool begin();

    bool isConnected() const { return _connected; }

    /** Read sensor event and update complementary filter. Call at 100Hz. */
    void update();

    /** Calibrate gyroscope zero offset while stationary. */
    void calibrateGyro(uint16_t samples = 200);

    // Filtered orientation in degrees.
    // Roll is about the sensor X axis, pitch about Y. Any axis choice or
    // inversion is applied downstream (see sensor_task.cpp).
    float getRoll()  const { return _roll; }
    float getPitch() const { return _pitch; }

private:
    Adafruit_MPU6050 _mpu;
    bool  _connected;
    float _roll, _pitch;
    float _gyroOffsetX, _gyroOffsetY;  // °/s
    unsigned long _lastUpdateUs;

    static constexpr float ALPHA = 0.98f;
};

#endif // MPU6050_DRIVER_H
