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

// Returns cached available import/export products for the Settings UI.
const std::vector<TariffProduct>& tariff_api_get_import_products();
const std::vector<TariffProduct>& tariff_api_get_export_products();

// Last fetch result and ready status.
FetchResult tariff_api_last_result();
bool tariff_api_ready();
