// Octopus Energy Tariff API service
// Fetches available energy products/tariffs from Octopus Energy.

#pragma once

#include <Arduino.h>
#include <vector>
#include "fetch_result.h"

struct TariffProduct {
  String code;
  String display_name;
  bool is_export = false;
};

// Periodic background service function called from the poll task.
// Fetches available products from https://api.octopus.energy/v1/products/
FetchResult tariff_api_service();

// Returns a copy of the cached products for the Settings UI. The poll task can
// replace the cache at any time, so callers must not hold a reference into it.
std::vector<TariffProduct> tariff_api_get_import_products();
std::vector<TariffProduct> tariff_api_get_export_products();

// Last fetch result and ready status.
FetchResult tariff_api_last_result();
bool tariff_api_ready();
