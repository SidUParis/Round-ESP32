// SPDX-License-Identifier: Apache-2.0
#pragma once
#include <array>
#include <cstdint>
#include <ctime>
namespace round_clock_codec {
inline bool leap(int year) {
  return year % 4 == 0 && (year % 100 != 0 || year % 400 == 0);
}
inline int month_days(int year, int month) {
  constexpr int days[] = {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
  return days[month - 1] + (month == 2 && leap(year));
}
inline int bcd(uint8_t value) {
  if ((value & 15) > 9 || (value >> 4) > 9)
    return -1;
  return (value >> 4) * 10 + (value & 15);
}
inline uint8_t encode_bcd(int value) {
  return uint8_t((value / 10) * 16 + value % 10);
}
inline bool decode(const std::array<uint8_t, 7> &r, uint8_t control,
                   time_t &epoch) {
  if ((control & 0x22) || (r[0] & 0x80))
    return false; // stopped, 12-hour mode, or oscillator loss
  int sec = bcd(r[0] & 0x7f), min = bcd(r[1] & 0x7f), hour = bcd(r[2] & 0x3f),
      day = bcd(r[3] & 0x3f), month = bcd(r[5] & 0x1f), yy = bcd(r[6]);
  int year = 2000 + yy;
  if (sec < 0 || sec > 59 || min < 0 || min > 59 || hour < 0 || hour > 23 ||
      month < 1 || month > 12 || yy < 24 || yy > 99 || day < 1 ||
      day > month_days(year, month) || (r[4] & 7) > 6)
    return false;
  int64_t days = 0;
  for (int y = 1970; y < year; y++)
    days += leap(y) ? 366 : 365;
  for (int m = 1; m < month; m++)
    days += month_days(year, m);
  days += day - 1;
  epoch = time_t(days * 86400 + hour * 3600 + min * 60 + sec);
  return true;
}
inline bool encode(time_t epoch, std::array<uint8_t, 7> &r) {
  std::tm t{};
  gmtime_r(&epoch, &t);
  if (t.tm_year < 124 || t.tm_year > 199)
    return false;
  r = {encode_bcd(t.tm_sec),       encode_bcd(t.tm_min),
       encode_bcd(t.tm_hour),      encode_bcd(t.tm_mday),
       uint8_t(t.tm_wday),         encode_bcd(t.tm_mon + 1),
       encode_bcd(t.tm_year - 100)};
  return true;
}
} // namespace round_clock_codec
