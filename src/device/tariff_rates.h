// Octopus Energy Day Tariff Rates Service
// Fetches daily half-hourly rates for configured import/export tariffs.

#pragma once

#include <time.h>
#include "fetch_result.h"
#include "snapshot.h"

// Periodic background service called from poll_task.
// Fetches rates at user-configured time or when cache is invalid.
FetchResult tariff_rates_service();

// Returns cached day rates data.
const DayTariffRates& tariff_rates_get();

// Returns current import and export rate in pence/kWh for the current 30-minute slot.
// Returns true if at least one rate is known.
bool tariff_rates_get_current(float* out_import_p, float* out_export_p);

// Returns active 30-minute slot index (0..47) based on local wall clock time.
uint8_t tariff_rates_get_current_slot();
