#include <Arduino.h>
#include <Wire.h>

#include "SenseCapSolarBoard.h"
#if defined(P1_EVENT_LOG)
#include "P1EventJournal.h"
#endif

static const mesh::PowerStateConfig runtime_power_config = {
  .warning_mv = PWR_LOW_WARNING_MV,
  .shutdown_mv = PWR_SHUTDOWN_MV,
  .critical_clear_mv = PWR_CRITICAL_CLEAR_MV,
  .warning_hysteresis_mv = PWR_LOW_HYSTERESIS_MV,
  .shutdown_delay_ms = (uint32_t)PWR_SHUTDOWN_DELAY_SEC * 1000U,
  .valid_min_mv = 1000,
  .valid_max_mv = 5000
};

SenseCapSolarBoard::SenseCapSolarBoard()
  : NRF52Board("SENSECAP_SOLAR_OTA"), power_state_machine(runtime_power_config) {}

void SenseCapSolarBoard::configureBatterySense(bool enabled) {
  const uint32_t pin = g_ADigitalPinMap[VBAT_ENABLE];
  // P0.14 is sink-only in Seeed's official divider. S0D1 makes logical HIGH
  // high-impedance instead of driving the battery divider toward VDD.
  nrf_gpio_cfg(pin, NRF_GPIO_PIN_DIR_OUTPUT, NRF_GPIO_PIN_INPUT_DISCONNECT,
               NRF_GPIO_PIN_NOPULL, NRF_GPIO_PIN_S0D1, NRF_GPIO_PIN_NOSENSE);
  if (enabled) nrf_gpio_pin_clear(pin);
  else nrf_gpio_pin_set(pin);
}

uint16_t SenseCapSolarBoard::sampleBatteryMilliVolts() {
  // The sense divider is switched by VBAT_ENABLE. It must remain enabled in
  // SYSTEMOFF because the same divided signal feeds LPCOMP.
  configureBatterySense(true);
  analogReadResolution(ADC_RESOLUTION);
  analogReference(AR_INTERNAL_3_0);
  analogSampleTime(40);
  delay(5);

  // Discard the first conversion after enabling this high-impedance divider.
  (void)analogRead(BATTERY_PIN);

  // A median rejects isolated ADC/load transients without allowing a single
  // LoRa TX voltage dip to start the ten-minute critical timer.
  uint16_t samples[7];
  for (uint8_t i = 0; i < 7; i++) {
    uint32_t raw = analogRead(BATTERY_PIN);
    samples[i] = (uint16_t)((raw * ADC_MULTIPLIER * AREF_VOLTAGE) / 4.096F);
    delay(2);
  }
  for (uint8_t i = 1; i < 7; i++) {
    uint16_t value = samples[i];
    int8_t j = (int8_t)i - 1;
    while (j >= 0 && samples[j] > value) {
      samples[j + 1] = samples[j];
      j--;
    }
    samples[j + 1] = value;
  }
  return samples[3];
}

#ifdef NRF52_POWER_MANAGEMENT
const PowerMgtConfig power_config = {
  .lpcomp_ain_channel = PWRMGT_LPCOMP_AIN,
  .lpcomp_refsel = PWRMGT_LPCOMP_REFSEL,
  .voltage_bootlock = PWRMGT_VOLTAGE_BOOTLOCK
};

void SenseCapSolarBoard::initiateShutdown(uint8_t reason) {
  bool enable_lpcomp = (reason == SHUTDOWN_REASON_LOW_VOLTAGE ||
                        reason == SHUTDOWN_REASON_BOOT_PROTECT);

  // This also covers boot protection, which occurs before radio_init(). The
  // generic nRF52 shutdown path deliberately initializes SPI as needed before
  // putting the radio and GPS into their quiescent states.
  if (!peripherals_shutdown) {
    shutdownPeripherals();
    peripherals_shutdown = true;
  }

#if defined(P1_EVENT_LOG)
  if (event_journal && event_journal->isReady()) {
    event_journal->recordShutdown(
        reason, power_state_machine.lastValidMilliVolts(),
        power_state_machine.criticalDurationSeconds(millis()));
  }
#endif

#ifdef LED_WHITE
  digitalWrite(LED_WHITE, LOW);
#endif
#ifdef LED_BLUE
  digitalWrite(LED_BLUE, LOW);
#endif
#ifdef P_LORA_TX_LED
  digitalWrite(P_LORA_TX_LED, LOW);
#endif

  configureBatterySense(enable_lpcomp);
  // Disconnect the digital input buffer; AIN7 remains available to LPCOMP and
  // avoids leakage when the divided voltage sits in the GPIO transition band.
  nrf_gpio_cfg(g_ADigitalPinMap[BATTERY_PIN], NRF_GPIO_PIN_DIR_INPUT,
               NRF_GPIO_PIN_INPUT_DISCONNECT, NRF_GPIO_PIN_NOPULL,
               NRF_GPIO_PIN_S0S1, NRF_GPIO_PIN_NOSENSE);

  if (enable_lpcomp) {
    #ifdef PWR_TEST_STANDBY_WAKE_MV
    enterTestRecoveryStandby(reason);
    return;
    #else
    bool voltage_wake_ready = false;
    for (uint8_t attempt = 0; attempt < 3 && !voltage_wake_ready; attempt++) {
      voltage_wake_ready = configureVoltageWake(power_config.lpcomp_ain_channel,
                                                 power_config.lpcomp_refsel);
    }
    if (!voltage_wake_ready) {
      // Avoid a battery-draining reset loop. configureVoltageWake() arms VBUS
      // first; also retain button wake as a physical recovery path, then enter
      // a degraded SYSTEMOFF rather than running toward brownout.
      MESH_DEBUG_PRINTLN("PWRMGT: LPCOMP unavailable; VBUS/button wake only");
#ifdef PIN_USER_BTN
      nrf_gpio_cfg_sense_input(g_ADigitalPinMap[PIN_USER_BTN],
                               NRF_GPIO_PIN_PULLUP, NRF_GPIO_PIN_SENSE_LOW);
#elif defined(PIN_BUTTON1)
      nrf_gpio_cfg_sense_input(g_ADigitalPinMap[PIN_BUTTON1],
                               NRF_GPIO_PIN_PULLUP, NRF_GPIO_PIN_SENSE_LOW);
#endif
    }
    #endif
  }

  enterSystemOff(reason);
}
#endif // NRF52_POWER_MANAGEMENT

void SenseCapSolarBoard::begin() {
  NRF52BoardDCDC::begin();

  pinMode(BATTERY_PIN, INPUT);
  configureBatterySense(true);
  analogReadResolution(12);
  analogReference(AR_INTERNAL_3_0);
  delay(50);

#ifdef PIN_USER_BTN
  pinMode(PIN_USER_BTN, INPUT_PULLUP);
#elif defined(PIN_BUTTON1)
  pinMode(PIN_BUTTON1, INPUT_PULLUP);
#endif

#if defined(PIN_WIRE_SDA) && defined(PIN_WIRE_SCL)
  Wire.setPins(PIN_WIRE_SDA, PIN_WIRE_SCL);
#endif

  Wire.begin();

#ifdef LED_WHITE
  pinMode(LED_WHITE, OUTPUT);
  digitalWrite(LED_WHITE, LOW);
#endif
#ifdef LED_BLUE
  pinMode(LED_BLUE, OUTPUT);
  digitalWrite(LED_BLUE, LOW);
#endif

#ifdef P_LORA_TX_LED
  pinMode(P_LORA_TX_LED, OUTPUT);
  digitalWrite(P_LORA_TX_LED, LOW);
#endif

#ifdef NRF52_POWER_MANAGEMENT
  checkBootVoltage(&power_config);
  power_state_machine.update(millis(), getBootVoltage(), isExternalPowered());
  next_power_sample_ms = millis() + (uint32_t)PWR_SAMPLE_INTERVAL_SEC * 1000U;
#endif

  delay(10);   // give sx1262 some time to power up
}

#ifdef PWR_TEST_STANDBY_WAKE_MV
void SenseCapSolarBoard::enterTestRecoveryStandby(uint8_t reason) {
  // TEST PROFILE ONLY: the LPCOMP ladder cannot express 3.45 V with the
  // 1M/499k divider. Keep the CPU in low-power SYSTEM ON, with LoRa/GPS/LEDs
  // already off, and periodically sample VBAT. This validates solar recovery
  // quickly but is intentionally not the final SYSTEMOFF qualification path.
  recordShutdownReason(reason);
  configureBatterySense(true);
  NRF_POWER->TASKS_LOWPWR = 1;
  const uint32_t standby_started_ms = millis();

  uint8_t confirmed_samples = 0;
  while (true) {
    const uint16_t battery_mv = sampleBatteryMilliVolts();
    if (isExternalPowered() ||
        (battery_mv >= PWR_TEST_STANDBY_WAKE_MV && battery_mv <= 5000U)) {
      confirmed_samples++;
    } else {
      confirmed_samples = 0;
    }

    if (confirmed_samples >= PWR_TEST_STANDBY_CONFIRM_SAMPLES) {
      MESH_DEBUG_PRINTLN("PWRMGT: Test standby recovered at %u mV", battery_mv);
#if defined(P1_EVENT_LOG)
      if (event_journal && event_journal->isReady()) {
        event_journal->recordRecoveryReset(
            battery_mv, (uint32_t)(millis() - standby_started_ms) / 1000U);
      }
#endif
      NVIC_SystemReset();
    }

    delay((uint32_t)PWR_TEST_STANDBY_SAMPLE_SEC * 1000U);
  }
}
#endif

void SenseCapSolarBoard::servicePowerManagement() {
#ifdef NRF52_POWER_MANAGEMENT
  const uint32_t now = millis();
  if ((int32_t)(now - next_power_sample_ms) < 0) return;
  next_power_sample_ms = now + (uint32_t)PWR_SAMPLE_INTERVAL_SEC * 1000U;

  const uint16_t battery_mv = sampleBatteryMilliVolts();
  const mesh::PowerState previous_state = power_state_machine.state();
  const mesh::PowerAction action =
      power_state_machine.update(now, battery_mv, isExternalPowered());

#if defined(P1_EVENT_LOG)
  if (action != mesh::PowerAction::SHUTDOWN && event_journal &&
      event_journal->isReady() && power_state_machine.state() != previous_state) {
    event_journal->recordPowerState(
        power_state_machine.state(), battery_mv,
        power_state_machine.criticalDurationSeconds(now));
  }
#endif

  if (action == mesh::PowerAction::SHUTDOWN) {
    MESH_DEBUG_PRINTLN("PWRMGT: Runtime low voltage persisted for %lu s at %u mV",
      (unsigned long)power_state_machine.criticalDurationSeconds(now), battery_mv);
    initiateShutdown(SHUTDOWN_REASON_LOW_VOLTAGE);
  }
#endif
}

bool SenseCapSolarBoard::getPowerStatus(mesh::MainBoard::PowerStatus& status) const {
#ifdef NRF52_POWER_MANAGEMENT
  status.state = mesh::PowerStateMachine::stateName(power_state_machine.state());
  #ifdef PWR_TEST_STANDBY_WAKE_MV
  status.wake_threshold = "test standby ADC 3450 mV; not LPCOMP SYSTEMOFF";
  #else
  status.wake_threshold = "3/8 VDD x 3.004; calibrate unit";
  #endif
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
