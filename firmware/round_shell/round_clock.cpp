// SPDX-FileCopyrightText: 2026 Round ESP32 contributors
// SPDX-License-Identifier: Apache-2.0
#include "round_clock.hpp"
#include "bsp/esp-bsp.h"
#include "clock_codec.hpp"
#include "driver/i2c_master.h"
#include "esp_log.h"
#include "esp_sntp.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "nvs.h"
#include "system_status.hpp"
#include <atomic>
#include <cctype>
#include <cstdlib>
#include <cstring>
#include <sys/time.h>
namespace {
constexpr const char *TAG = "RoundClock";
i2c_master_dev_handle_t rtc = nullptr;
SemaphoreHandle_t lock = nullptr;
std::atomic<bool> rtc_valid{false}, save_sntp{false};
std::atomic<const char *> source{"unsynced"};
char current_tz[96] = "UTC0";
bool read_reg(uint8_t address, uint8_t *out, size_t n) {
  return rtc &&
         i2c_master_transmit_receive(rtc, &address, 1, out, n, 100) == ESP_OK;
}
bool write_reg(uint8_t address, const uint8_t *data, size_t n) {
  uint8_t b[8];
  if (n > 7)
    return false;
  b[0] = address;
  memcpy(b + 1, data, n);
  return rtc && i2c_master_transmit(rtc, b, n + 1, 100) == ESP_OK;
}
bool save_rtc(time_t epoch) {
  std::array<uint8_t, 7> registers{};
  uint8_t control = 0;
  if (!round_clock_codec::encode(epoch, registers) || !read_reg(0, &control, 1))
    return false;
  uint8_t running = control & 0x05,
          stopped =
              running | 0x20; // retain CIE/CAP_SEL, use normal 24h operation
  if (!write_reg(0, &stopped, 1))
    return false;
  bool ok = write_reg(4, registers.data(), registers.size());
  bool resumed = write_reg(0, &running, 1);
  rtc_valid = ok && resumed;
  return rtc_valid;
}
bool valid_tz(const char *tz) {
  if (!tz || !tz[0] || strlen(tz) >= sizeof(current_tz))
    return false;
  for (const unsigned char *p = (const unsigned char *)tz; *p; p++)
    if (!isalnum(*p) && !strchr("+-:,./<>", *p))
      return false;
  return true;
}
void synced(struct timeval *) {
  source = "sntp";
  save_sntp = true;
}
void clock_worker(void *) {
  bool ntp_started = false;
  for (;;) {
    brookesia::system_status::Snapshot snapshot{};
    if (!ntp_started && brookesia::system_status::get_snapshot(snapshot) &&
        snapshot.wifi_connected) {
      esp_sntp_setoperatingmode(ESP_SNTP_OPMODE_POLL);
      esp_sntp_setservername(0, "pool.ntp.org");
      esp_sntp_set_time_sync_notification_cb(synced);
      esp_sntp_init();
      ntp_started = true;
    }
    if (save_sntp.exchange(false) &&
        xSemaphoreTake(lock, pdMS_TO_TICKS(500)) == pdTRUE) {
      bool ok = save_rtc(time(nullptr));
      xSemaphoreGive(lock);
      ESP_LOGI(TAG, "NTP synchronized, RTC saved=%d", ok);
    }
    vTaskDelay(pdMS_TO_TICKS(1000));
  }
}
} // namespace
void round_clock_start() {
  lock = xSemaphoreCreateMutex();
  if (!lock)
    return;
  nvs_handle_t handle;
  if (nvs_open("round_clock", NVS_READONLY, &handle) == ESP_OK) {
    size_t n = sizeof(current_tz);
    char zone[96];
    if (nvs_get_str(handle, "tz", zone, &n) == ESP_OK && valid_tz(zone))
      strlcpy(current_tz, zone, sizeof(current_tz));
    nvs_close(handle);
  }
  setenv("TZ", current_tz, 1);
  tzset();
  i2c_device_config_t cfg{};
  cfg.dev_addr_length = I2C_ADDR_BIT_LEN_7;
  cfg.device_address = 0x51;
  cfg.scl_speed_hz = 100000;
  auto bus = bsp_i2c_get_handle();
  if (bus && i2c_master_bus_add_device(bus, &cfg, &rtc) == ESP_OK) {
    uint8_t control = 0;
    std::array<uint8_t, 7> data{};
    time_t epoch = 0;
    if (read_reg(0, &control, 1) && read_reg(4, data.data(), data.size()) &&
        round_clock_codec::decode(data, control, epoch)) {
      struct timeval tv{epoch, 0};
      settimeofday(&tv, nullptr);
      rtc_valid = true;
      source = "rtc";
    }
  }
  ESP_LOGI(TAG, "Clock source=%s RTC_valid=%d", source.load(),
           rtc_valid.load());
  xTaskCreate(clock_worker, "round_clock", 4096, nullptr, 2, nullptr);
}
bool round_clock_sync(time_t epoch, const char *zone) {
  std::array<uint8_t, 7> r{};
  if (!lock || !valid_tz(zone) || !round_clock_codec::encode(epoch, r) ||
      xSemaphoreTake(lock, pdMS_TO_TICKS(500)) != pdTRUE)
    return false;
  setenv("TZ", zone, 1);
  tzset();
  struct timeval tv{epoch, 0};
  bool ok = settimeofday(&tv, nullptr) == 0;
  if (strcmp(zone, current_tz)) {
    nvs_handle_t h;
    if (nvs_open("round_clock", NVS_READWRITE, &h) == ESP_OK) {
      if (nvs_set_str(h, "tz", zone) != ESP_OK || nvs_commit(h) != ESP_OK)
        ok = false;
      nvs_close(h);
    } else {
      ok = false;
    }
    strlcpy(current_tz, zone, sizeof(current_tz));
  }
  bool stored = save_rtc(epoch);
  source = "usb";
  xSemaphoreGive(lock);
  ESP_LOGI(TAG, "USB time synchronized RTC_saved=%d", stored);
  return ok;
}
bool round_clock_rtc_valid() { return rtc_valid.load(); }
const char *round_clock_source() { return source.load(); }
