#pragma once
#include <stdint.h>
#include <stdbool.h>
#include "esp_err.h"

/**
 * @brief MQ135 ADC başlatma. smoke_sensor_init() sonrası SMOKE_WARMUP_MS
 *        beklenmelidir — bu süre dolmadan ölçüm güvenilir değil.
 */
esp_err_t smoke_sensor_init(void);

/**
 * @brief SMOKE_SAMPLE_COUNT ölçümün ortalamasını döndürür.
 * @return Ortalama ADC değeri (0–4095), veya hata durumunda -1.
 */
int smoke_sensor_read_avg(void);

// ─── LDR Işık Sensörü (Analog) ────────────────────────────────────────────────
esp_err_t ldr_sensor_init(void);
int ldr_sensor_read_avg(void);

/**
 * @brief Isınma süresi doldu mu?
 */
bool smoke_sensor_warmup_done(void);
