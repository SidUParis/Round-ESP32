// SPDX-License-Identifier: Apache-2.0
#pragma once
#include <ctime>
void round_clock_start();
bool round_clock_sync(time_t epoch,const char *timezone);
bool round_clock_rtc_valid();
const char *round_clock_source();
