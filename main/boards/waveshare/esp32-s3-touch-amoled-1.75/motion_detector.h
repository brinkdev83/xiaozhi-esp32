#pragma once

// Mochi: detects "shaken" and "picked up" with the onboard QMI8658 and reports
// them to the server as a bracketed message (via the wake-word path), so the
// pet can react. Only fires while the device is idle.

#include "application.h"

#include <cmath>
#include <cstdint>
#include <driver/i2c_master.h>
#include <esp_log.h>
#include <esp_timer.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

class MotionDetector {
public:
    explicit MotionDetector(i2c_master_bus_handle_t bus) {
        if (Initialize(bus) != ESP_OK) {
            ESP_LOGW(kTag, "QMI8658 not found, motion events disabled");
            return;
        }
        xTaskCreate(&MotionDetector::TaskEntry, "motion", 3072, this, 2, nullptr);
    }

private:
    static constexpr const char* kTag = "MotionDetector";
    static constexpr int kSampleMs = 20;            // 50 Hz
    static constexpr int kShakeDeviationMg = 700;   // jolt away from 1 g
    static constexpr int kShakeJolts = 6;           // within the window below
    static constexpr int kShakeWindowMs = 1000;
    static constexpr int kStillDeviationMg = 60;
    static constexpr int kStillForMs = 10000;       // resting before a pick-up counts
    static constexpr int kPickupDeviationMg = 200;
    static constexpr int kPickupHoldMs = 300;
    static constexpr int64_t kShakeCooldownMs = 8000;
    static constexpr int64_t kPickupCooldownMs = 30000;

    i2c_master_dev_handle_t device_ = nullptr;

    esp_err_t Read(uint8_t reg, uint8_t* data, size_t length) {
        return i2c_master_transmit_receive(device_, &reg, 1, data, length, 50);
    }

    esp_err_t Write(uint8_t reg, uint8_t value) {
        const uint8_t data[] = {reg, value};
        return i2c_master_transmit(device_, data, sizeof(data), 50);
    }

    esp_err_t Initialize(i2c_master_bus_handle_t bus) {
        esp_err_t ret = ESP_ERR_NOT_FOUND;
        for (uint8_t address : {uint8_t{0x6B}, uint8_t{0x6A}}) {
            if (i2c_master_probe(bus, address, 50) != ESP_OK) {
                continue;
            }
            i2c_device_config_t config = {};
            config.device_address = address;
            config.scl_speed_hz = 400000;
            ret = i2c_master_bus_add_device(bus, &config, &device_);
            break;
        }
        if (ret != ESP_OK) {
            return ret;
        }
        uint8_t id = 0;
        ret = Read(0x00, &id, 1);
        if (ret == ESP_OK && id != 0x05) {
            ret = ESP_ERR_NOT_FOUND;
        }
        // Same setup as the cyber-vocat board: auto-increment, +/-8 g at 250 Hz,
        // accelerometer only.
        if (ret == ESP_OK) ret = Write(0x02, 0x60);
        if (ret == ESP_OK) ret = Write(0x03, 0x25);
        if (ret == ESP_OK) ret = Write(0x08, 0x01);
        if (ret != ESP_OK) {
            i2c_master_bus_rm_device(device_);
            device_ = nullptr;
        }
        return ret;
    }

    // Deviation of the acceleration magnitude from 1 g, in milli-g.
    bool ReadDeviation(int& deviation) {
        uint8_t data[6] = {};
        if (Read(0x35, data, sizeof(data)) != ESP_OK) {
            return false;
        }
        auto axis = [&](int i) {
            int16_t raw = static_cast<int16_t>(data[i] | (data[i + 1] << 8));
            return raw * 1000.0f / 4096.0f;
        };
        float x = axis(0), y = axis(2), z = axis(4);
        deviation = static_cast<int>(std::fabs(std::sqrt(x * x + y * y + z * z) - 1000.0f));
        return true;
    }

    static void TaskEntry(void* arg) {
        static_cast<MotionDetector*>(arg)->Run();
    }

    void Report(const char* event) {
        auto& app = Application::GetInstance();
        if (app.GetDeviceState() != kDeviceStateIdle) {
            return;  // never interrupt a conversation
        }
        ESP_LOGI(kTag, "Motion event: %s", event);
        app.WakeWordInvoke(event);
    }

    void Run() {
        int64_t jolt_times[kShakeJolts] = {};
        int jolt_head = 0;
        int64_t still_since = 0;
        int64_t moving_since = 0;
        int64_t last_shake = -kShakeCooldownMs;
        int64_t last_pickup = -kPickupCooldownMs;
        int64_t last_jolt = -kShakeWindowMs;

        while (true) {
            vTaskDelay(pdMS_TO_TICKS(kSampleMs));
            int dev = 0;
            if (!ReadDeviation(dev)) {
                continue;
            }
            int64_t now = esp_timer_get_time() / 1000;

            // Shake: several hard jolts within a short window.
            if (dev > kShakeDeviationMg) {
                last_jolt = now;
                jolt_times[jolt_head] = now;
                jolt_head = (jolt_head + 1) % kShakeJolts;
                int64_t oldest = jolt_times[jolt_head];
                if (oldest != 0 && now - oldest < kShakeWindowMs &&
                    now - last_shake > kShakeCooldownMs) {
                    Report("[Mochi wordt door elkaar geschud]");
                    last_shake = now;
                    still_since = 0;
                    moving_since = 0;
                    for (auto& t : jolt_times) t = 0;
                    continue;
                }
            }

            // Pick-up: lying still for a while, then sustained gentle motion
            // without hard jolts (those belong to a shake).
            if (dev < kStillDeviationMg) {
                if (still_since == 0) still_since = now;
                moving_since = 0;
            } else if (dev > kPickupDeviationMg) {
                bool was_resting = still_since != 0 && now - still_since > kStillForMs;
                bool jolting = now - last_jolt < kShakeWindowMs;
                if (moving_since == 0) moving_since = now;
                if (was_resting && !jolting && now - moving_since > kPickupHoldMs &&
                    now - last_pickup > kPickupCooldownMs) {
                    Report("[Mochi wordt opgepakt]");
                    last_pickup = now;
                }
                if (now - moving_since > kPickupHoldMs) {
                    still_since = 0;
                }
            }
        }
    }
};
