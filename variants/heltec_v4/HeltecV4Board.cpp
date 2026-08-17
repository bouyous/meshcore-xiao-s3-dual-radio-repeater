#include "HeltecV4Board.h"

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

static constexpr uint8_t SHUTDOWN_REASON_NONE = 0;
static constexpr uint8_t SHUTDOWN_REASON_LOW_VOLTAGE = 1;
static RTC_DATA_ATTR uint8_t retained_shutdown_reason = SHUTDOWN_REASON_NONE;
#endif

#ifdef AUTO_SHUTDOWN_MILLIVOLTS
#ifndef LOW_VOLTAGE_RECOVERY_MILLIVOLTS
#define LOW_VOLTAGE_RECOVERY_MILLIVOLTS 3600
#endif

#ifndef LOW_VOLTAGE_RECOVERY_INTERVAL_SECS
#define LOW_VOLTAGE_RECOVERY_INTERVAL_SECS 300
#endif

static RTC_DATA_ATTR uint32_t low_voltage_recovery_magic = 0;
static const uint32_t LOW_VOLTAGE_RECOVERY_MAGIC = 0x4C565257; // "LVRW"
static const uint16_t MAX_VALID_LIPO_MILLIVOLTS = 5000;
#endif

HeltecV4Board::HeltecV4Board()
  : periph_power(PIN_VEXT_EN, PIN_VEXT_EN_ACTIVE)
#ifdef RUNTIME_POWER_MANAGEMENT
  , power_state_machine(runtime_power_config)
#endif
{ }

void HeltecV4Board::begin() {
    ESP32Board::begin();


    pinMode(PIN_ADC_CTRL, OUTPUT);
    digitalWrite(PIN_ADC_CTRL, LOW); // Initially inactive

    esp_reset_reason_t reason = esp_reset_reason();

#ifdef AUTO_SHUTDOWN_MILLIVOLTS
    if (reason != ESP_RST_DEEPSLEEP) {
      low_voltage_recovery_magic = 0;
    }

    uint16_t boot_voltage = getBattMilliVolts();
    bool valid_voltage = boot_voltage > 1000 && boot_voltage <= MAX_VALID_LIPO_MILLIVOLTS;

#ifdef RUNTIME_POWER_MANAGEMENT
    boot_voltage_mv = boot_voltage;
    previous_shutdown_reason = retained_shutdown_reason;
#endif

    if (low_voltage_recovery_magic == LOW_VOLTAGE_RECOVERY_MAGIC) {
      if (!valid_voltage || boot_voltage < LOW_VOLTAGE_RECOVERY_MILLIVOLTS) {
        MESH_DEBUG_PRINTLN("Low-voltage recovery: %u mV; sleeping for %u seconds",
          boot_voltage, LOW_VOLTAGE_RECOVERY_INTERVAL_SECS);
        enterLowVoltageRecoverySleep();
      }

      MESH_DEBUG_PRINTLN("Low-voltage recovery complete: %u mV", boot_voltage);
      low_voltage_recovery_magic = 0;
#ifdef RUNTIME_POWER_MANAGEMENT
      retained_shutdown_reason = SHUTDOWN_REASON_NONE;
#endif
    } else if (valid_voltage && boot_voltage < AUTO_SHUTDOWN_MILLIVOLTS) {
      MESH_DEBUG_PRINTLN("Boot voltage too low: %u mV; entering recovery sleep", boot_voltage);
      low_voltage_recovery_magic = LOW_VOLTAGE_RECOVERY_MAGIC;
      enterLowVoltageRecoverySleep();
    }
#endif

    if (reason == ESP_RST_DEEPSLEEP) {
      rtc_gpio_hold_dis((gpio_num_t)P_LORA_PA_POWER);
      rtc_gpio_hold_dis((gpio_num_t)P_LORA_NSS);
      rtc_gpio_deinit((gpio_num_t)P_LORA_DIO_1);
    }

    loRaFEMControl.init();

    periph_power.begin();
    esp_reset_reason_t reason = esp_reset_reason();
    if (reason == ESP_RST_DEEPSLEEP) {
      long wakeup_source = esp_sleep_get_ext1_wakeup_status();
      if (wakeup_source & (1 << P_LORA_DIO_1)) {  // received a LoRa packet (while in deep sleep)
        startup_reason = BD_STARTUP_RX_PACKET;
    }

      rtc_gpio_hold_dis((gpio_num_t)P_LORA_NSS);
      rtc_gpio_deinit((gpio_num_t)P_LORA_DIO_1);
    }

#ifdef RUNTIME_POWER_MANAGEMENT
    power_state_machine.update(millis(), boot_voltage_mv, false);
    next_power_sample_ms = millis() + (uint32_t)PWR_SAMPLE_INTERVAL_SEC * 1000U;
#endif
  }

  void HeltecV4Board::onBeforeTransmit(void) {
    digitalWrite(P_LORA_TX_LED, HIGH);   // turn TX LED on
    loRaFEMControl.setTxModeEnable();
  }

  void HeltecV4Board::onAfterTransmit(void) {
    digitalWrite(P_LORA_TX_LED, LOW);   // turn TX LED off
    loRaFEMControl.setRxModeEnable();
  }

  void HeltecV4Board::powerOff() {
    // Turn off PA
    digitalWrite(P_LORA_PA_POWER, LOW);
    rtc_gpio_hold_en((gpio_num_t)P_LORA_PA_POWER);

    ESP32Board::powerOff();
  }

  void HeltecV4Board::powerOffLowVoltage() {
#ifdef AUTO_SHUTDOWN_MILLIVOLTS
    low_voltage_recovery_magic = LOW_VOLTAGE_RECOVERY_MAGIC;
#ifdef RUNTIME_POWER_MANAGEMENT
    retained_shutdown_reason = SHUTDOWN_REASON_LOW_VOLTAGE;
#endif

    // Turn off the external PA before entering deep sleep.
    digitalWrite(P_LORA_PA_POWER, LOW);
    rtc_gpio_hold_en((gpio_num_t)P_LORA_PA_POWER);

    // The ESP32-S3 has no nRF52-style LPCOMP wake. Periodic timer wakeups
    // let the solar charger replenish the cell while keeping consumption low.
    ESP32Board::enterDeepSleep(LOW_VOLTAGE_RECOVERY_INTERVAL_SECS);
#else
    powerOff();
#endif
  }

  void HeltecV4Board::servicePowerManagement() {
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

  bool HeltecV4Board::getPowerStatus(mesh::MainBoard::PowerStatus& status) const {
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

  uint16_t HeltecV4Board::getBootVoltage() {
#ifdef RUNTIME_POWER_MANAGEMENT
    return boot_voltage_mv;
#else
    return 0;
#endif
  }

  uint8_t HeltecV4Board::getShutdownReason() const {
#ifdef RUNTIME_POWER_MANAGEMENT
    return previous_shutdown_reason;
#else
    return 0;
#endif
  }

  const char* HeltecV4Board::getShutdownReasonString(uint8_t reason) {
#ifdef RUNTIME_POWER_MANAGEMENT
    if (reason == SHUTDOWN_REASON_LOW_VOLTAGE) return "low voltage";
    return "none recorded";
#else
    (void)reason;
    return "Not available";
#endif
  }

#ifdef AUTO_SHUTDOWN_MILLIVOLTS
  void HeltecV4Board::enterLowVoltageRecoverySleep() {
    pinMode(P_LORA_PA_POWER, OUTPUT);
    digitalWrite(P_LORA_PA_POWER, LOW);
    rtc_gpio_hold_en((gpio_num_t)P_LORA_PA_POWER);

    pinMode(P_LORA_NSS, OUTPUT);
    digitalWrite(P_LORA_NSS, HIGH);
    rtc_gpio_hold_en((gpio_num_t)P_LORA_NSS);

    digitalWrite(PIN_ADC_CTRL, LOW);
    pinMode(PIN_VEXT_EN, OUTPUT);
    digitalWrite(PIN_VEXT_EN, !PIN_VEXT_EN_ACTIVE);

    Serial.flush();
    delay(50);

    esp_sleep_disable_wakeup_source(ESP_SLEEP_WAKEUP_ALL);
    esp_sleep_enable_timer_wakeup((uint64_t)LOW_VOLTAGE_RECOVERY_INTERVAL_SECS * 1000000ULL);
    esp_deep_sleep_start();
  }
#endif

  uint16_t HeltecV4Board::getBattMilliVolts()  {
    analogReadResolution(10);
    digitalWrite(PIN_ADC_CTRL, HIGH);
    delay(10);
    uint32_t raw = 0;
    for (int i = 0; i < 8; i++) {
      raw += analogRead(PIN_VBAT_READ);
    }
    raw = raw / 8;

    digitalWrite(PIN_ADC_CTRL, LOW);

    return (adc_mult * (3.3 / 1024.0) * raw) * 1000;
  }

  const char* HeltecV4Board::getManufacturerName() const {
#ifdef HELTEC_LORA_V4_TFT
    return loRaFEMControl.getFEMType() == KCT8103L_PA ? "Heltec V4.3 TFT" : "Heltec V4 TFT";
#else
    return loRaFEMControl.getFEMType() == KCT8103L_PA ? "Heltec V4.3 OLED" : "Heltec V4 OLED";
#endif
  }

  bool HeltecV4Board::setLoRaFemLnaEnabled(bool enable) {
    if (!loRaFEMControl.isLnaCanControl()) {
      return false;
    }

    loRaFEMControl.setLNAEnable(enable);
    loRaFEMControl.setRxModeEnable();
    return true;
  }

  bool HeltecV4Board::canControlLoRaFemLna() const {
    return loRaFEMControl.isLnaCanControl();
  }

  bool HeltecV4Board::isLoRaFemLnaEnabled() const {
    return loRaFEMControl.isLNAEnabled();
  }
