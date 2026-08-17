#include "XiaoS3WIOBoard.h"

#ifdef RUNTIME_POWER_MANAGEMENT
static const mesh::PowerStateConfig runtime_power_config = {
  .warning_mv = PWR_LOW_WARNING_MV,
  .shutdown_mv = PWR_SHUTDOWN_MV,
  .critical_clear_mv = PWR_CRITICAL_CLEAR_MV,
  .warning_hysteresis_mv = PWR_LOW_HYSTERESIS_MV,
  .shutdown_delay_ms = (uint32_t)PWR_SHUTDOWN_DELAY_SEC * 1000U,
  .valid_min_mv = 1000,
  .valid_max_mv = 5000
};

static constexpr uint32_t LOW_VOLTAGE_RECOVERY_MAGIC = 0x584C5652; // "XLVR"
static constexpr uint8_t SHUTDOWN_REASON_NONE = 0;
static constexpr uint8_t SHUTDOWN_REASON_LOW_VOLTAGE = 1;
static RTC_DATA_ATTR uint32_t low_voltage_recovery_magic = 0;
static RTC_DATA_ATTR uint8_t retained_shutdown_reason = SHUTDOWN_REASON_NONE;
#endif

XiaoS3WIOBoard::XiaoS3WIOBoard()
#ifdef RUNTIME_POWER_MANAGEMENT
  : power_state_machine(runtime_power_config)
#endif
{ }

void XiaoS3WIOBoard::begin() {
  ESP32Board::begin();

#ifdef RUNTIME_POWER_MANAGEMENT
  const esp_reset_reason_t reason = esp_reset_reason();
  if (reason != ESP_RST_DEEPSLEEP) {
    low_voltage_recovery_magic = 0;
  }

  pinMode(PIN_VBAT_READ, INPUT);
  analogReadResolution(12);
  analogSetPinAttenuation(PIN_VBAT_READ, ADC_2_5db);
  delay(20);

  boot_voltage_mv = getBattMilliVolts();
  previous_shutdown_reason = retained_shutdown_reason;
  const bool valid_voltage =
      boot_voltage_mv >= 1000 && boot_voltage_mv <= 5000;

  if (low_voltage_recovery_magic == LOW_VOLTAGE_RECOVERY_MAGIC) {
    if (!valid_voltage ||
        boot_voltage_mv < LOW_VOLTAGE_RECOVERY_MILLIVOLTS) {
      enterLowVoltageRecoverySleep();
    }
    low_voltage_recovery_magic = 0;
    retained_shutdown_reason = SHUTDOWN_REASON_NONE;
  } else if (valid_voltage &&
             boot_voltage_mv < AUTO_SHUTDOWN_MILLIVOLTS) {
    low_voltage_recovery_magic = LOW_VOLTAGE_RECOVERY_MAGIC;
    retained_shutdown_reason = SHUTDOWN_REASON_LOW_VOLTAGE;
    enterLowVoltageRecoverySleep();
  }

  releaseRadioPinHolds();
  power_state_machine.update(millis(), boot_voltage_mv, false);
  next_power_sample_ms =
      millis() + (uint32_t)PWR_SAMPLE_INTERVAL_SEC * 1000U;
#endif
}

uint16_t XiaoS3WIOBoard::getBattMilliVolts() {
#ifdef RUNTIME_POWER_MANAGEMENT
  // The XIAO ESP32-S3 has no internal battery-to-ADC route. This input is the
  // documented external 1 MOhm / 330 kOhm divider on D4/GPIO5, with 100 nF
  // from D4 to GND. Discard the first conversion after the sampling capacitor
  // settles, then use a median so a radio-current transient cannot shut down
  // the repeater by itself.
  analogReadResolution(12);
  analogSetPinAttenuation(PIN_VBAT_READ, ADC_2_5db);
  delay(3);
  (void)analogReadMilliVolts(PIN_VBAT_READ);

  uint16_t samples[9];
  for (uint8_t i = 0; i < 9; i++) {
    const uint32_t pin_mv = analogReadMilliVolts(PIN_VBAT_READ);
    samples[i] = (uint16_t)(pin_mv * battery_divider_multiplier + 0.5f);
    delay(2);
  }
  for (uint8_t i = 1; i < 9; i++) {
    const uint16_t value = samples[i];
    int8_t j = (int8_t)i - 1;
    while (j >= 0 && samples[j] > value) {
      samples[j + 1] = samples[j];
      j--;
    }
    samples[j + 1] = value;
  }
  return samples[4];
#else
  return ESP32Board::getBattMilliVolts();
#endif
}

void XiaoS3WIOBoard::servicePowerManagement() {
#ifdef RUNTIME_POWER_MANAGEMENT
  const uint32_t now = millis();
  if (low_voltage_shutdown_pending) {
    if ((int32_t)(now - low_voltage_shutdown_at_ms) >= 0) {
      powerOffLowVoltage();
    }
    return;
  }
  if ((int32_t)(now - next_power_sample_ms) < 0) return;
  next_power_sample_ms = now + (uint32_t)PWR_SAMPLE_INTERVAL_SEC * 1000U;

  const uint16_t battery_mv = getBattMilliVolts();
  const mesh::PowerAction action =
      power_state_machine.update(now, battery_mv, false);
  if (action == mesh::PowerAction::SHUTDOWN) {
#if defined(P1_POWER_ALERTS)
    low_voltage_shutdown_pending = true;
    low_voltage_shutdown_at_ms =
        now + (uint32_t)P1_ALERT_SHUTDOWN_GRACE_SEC * 1000U;
#else
    powerOffLowVoltage();
#endif
  }
#endif
}

bool XiaoS3WIOBoard::getPowerStatus(
    mesh::MainBoard::PowerStatus& status) const {
#ifdef RUNTIME_POWER_MANAGEMENT
  status.state = mesh::PowerStateMachine::stateName(power_state_machine.state());
  status.wake_threshold = "timer ADC >= recovery threshold";
  status.battery_mv = power_state_machine.lastValidMilliVolts();
  status.warning_mv = PWR_LOW_WARNING_MV;
  status.shutdown_mv = PWR_SHUTDOWN_MV;
  status.low_seconds = power_state_machine.criticalDurationSeconds(millis());
  status.shutdown_delay_seconds = PWR_SHUTDOWN_DELAY_SEC;
  status.power_saving = power_state_machine.isPowerSaving();
  return true;
#else
  (void)status;
  return false;
#endif
}

void XiaoS3WIOBoard::powerOffLowVoltage() {
#ifdef RUNTIME_POWER_MANAGEMENT
  low_voltage_recovery_magic = LOW_VOLTAGE_RECOVERY_MAGIC;
  retained_shutdown_reason = SHUTDOWN_REASON_LOW_VOLTAGE;
  enterLowVoltageRecoverySleep();
#else
  powerOff();
#endif
}

#ifdef RUNTIME_POWER_MANAGEMENT
static void holdOutput(uint8_t pin, uint8_t level) {
  pinMode(pin, OUTPUT);
  digitalWrite(pin, level);
  gpio_hold_en((gpio_num_t)pin);
}

void XiaoS3WIOBoard::enterLowVoltageRecoverySleep() {
  // Hold every SX1262 in reset and deselected. The two radios share the 3V3
  // rail with the XIAO, so this is the hardware-compatible quiescent state;
  // there is no separate load switch on either Wio-SX1262 board.
  holdOutput(P_LORA_NSS, HIGH);
  holdOutput(P_LORA_RESET, LOW);
#ifdef DUAL_SX1262_REPEATER
  holdOutput(P_LORA2_NSS, HIGH);
  holdOutput(P_LORA2_RESET, LOW);
#endif
#ifdef SX126X_RXEN
  holdOutput(SX126X_RXEN, LOW);
#endif
#ifdef PIN_STATUS_LED
  holdOutput(PIN_STATUS_LED, LOW);
#endif
  gpio_deep_sleep_hold_en();

  Serial.flush();
  delay(50);
  esp_sleep_disable_wakeup_source(ESP_SLEEP_WAKEUP_ALL);
  esp_sleep_enable_timer_wakeup(
      (uint64_t)LOW_VOLTAGE_RECOVERY_INTERVAL_SECS * 1000000ULL);
  esp_deep_sleep_start();
}

void XiaoS3WIOBoard::releaseRadioPinHolds() {
  gpio_deep_sleep_hold_dis();
  gpio_hold_dis((gpio_num_t)P_LORA_NSS);
  gpio_hold_dis((gpio_num_t)P_LORA_RESET);
#ifdef DUAL_SX1262_REPEATER
  gpio_hold_dis((gpio_num_t)P_LORA2_NSS);
  gpio_hold_dis((gpio_num_t)P_LORA2_RESET);
#endif
#ifdef SX126X_RXEN
  gpio_hold_dis((gpio_num_t)SX126X_RXEN);
#endif
#ifdef PIN_STATUS_LED
  gpio_hold_dis((gpio_num_t)PIN_STATUS_LED);
#endif
}
#endif

uint16_t XiaoS3WIOBoard::getBootVoltage() {
#ifdef RUNTIME_POWER_MANAGEMENT
  return boot_voltage_mv;
#else
  return 0;
#endif
}

uint8_t XiaoS3WIOBoard::getShutdownReason() const {
#ifdef RUNTIME_POWER_MANAGEMENT
  return previous_shutdown_reason;
#else
  return 0;
#endif
}

const char* XiaoS3WIOBoard::getShutdownReasonString(uint8_t reason) {
#ifdef RUNTIME_POWER_MANAGEMENT
  if (reason == SHUTDOWN_REASON_LOW_VOLTAGE) return "low voltage";
  return "none recorded";
#else
  (void)reason;
  return "Not available";
#endif
}

bool XiaoS3WIOBoard::setAdcMultiplier(float multiplier) {
#ifdef RUNTIME_POWER_MANAGEMENT
  battery_divider_multiplier =
      multiplier == 0.0f ? XIAO_VBAT_DIVIDER_MULTIPLIER : multiplier;
  return true;
#else
  (void)multiplier;
  return false;
#endif
}

float XiaoS3WIOBoard::getAdcMultiplier() const {
#ifdef RUNTIME_POWER_MANAGEMENT
  return battery_divider_multiplier;
#else
  return 0.0f;
#endif
}
