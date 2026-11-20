#pragma once
// Підключіть у кожному застосунку, який запускається з Launcher.
// Викличте bootReturnToLauncher() першим у setup(): наступне перезавантаження
// знову покаже стартове меню, а не одразу цей застосунок.
#include "esp_ota_ops.h"
#include "esp_partition.h"

inline void bootReturnToLauncher() {
  const esp_partition_t* f =
      esp_partition_find_first(ESP_PARTITION_TYPE_APP, ESP_PARTITION_SUBTYPE_APP_FACTORY, nullptr);
  if (f) esp_ota_set_boot_partition(f);
}
