#pragma once

#include <MeshCore.h>
#include <Arduino.h>
#include <helpers/NRF52Board.h>
#include <helpers/PowerStateMachine.h>

#if defined(P1_EVENT_LOG)
class P1EventJournal;
#endif

class SenseCapSolarBoard : public NRF52BoardDCDC {
protected:
#ifdef NRF52_POWER_MANAGEMENT
  void initiateShutdown(uint8_t reason) override;
#endif

  mesh::PowerStateMachine power_state_machine;
  uint32_t next_power_sample_ms = 0;
  bool peripherals_shutdown = false;
#if defined(P1_EVENT_LOG)
  P1EventJournal* event_journal = nullptr;
#endif

  uint16_t sampleBatteryMilliVolts();
  void configureBatterySense(bool enabled);
  #ifdef PWR_TEST_STANDBY_WAKE_MV
  void enterTestRecoveryStandby(uint8_t reason);
  #endif

public:
  SenseCapSolarBoard();
  void begin();
  void servicePowerManagement() override;
#if defined(P1_EVENT_LOG)
  void setEventJournal(P1EventJournal* journal) { event_journal = journal; }
#endif
  bool getPowerStatus(mesh::MainBoard::PowerStatus& status) const override;
  bool getBatteryTemperature(float& temperature_c) const override {
    // The pack NTC is connected to the autonomous CN3165 TEMP input, but no
    // documented carrier trace exposes that node to an nRF52840 ADC input.
    (void)temperature_c;
    return false;
  }
  const char* getChargeTemperatureGuardStatus() const override {
    return "CN3165 NTC autonomous; battery NTC not routed to MCU";
  }

#if defined(P_LORA_TX_LED)
  void onBeforeTransmit() override {
    digitalWrite(P_LORA_TX_LED, HIGH);   // turn TX LED on
  }
  void onAfterTransmit() override {
    digitalWrite(P_LORA_TX_LED, LOW);   // turn TX LED off
  }
#endif

  uint16_t getBattMilliVolts() override {
    return sampleBatteryMilliVolts();
  }

  const char* getManufacturerName() const override {
    return "Seeed SenseCap Solar";
  }

  void powerOff() override {
    digitalWrite(LED_WHITE, LOW);
    digitalWrite(LED_BLUE, LOW);

#ifdef PIN_USER_BTN
    while (digitalRead(PIN_USER_BTN) == LOW);
    // Keep pull-up enabled in system-off so the wake line doesn't float low.
    nrf_gpio_cfg_sense_input(digitalPinToInterrupt(g_ADigitalPinMap[PIN_USER_BTN]), NRF_GPIO_PIN_PULLUP, NRF_GPIO_PIN_SENSE_LOW);
#elif defined(PIN_BUTTON1)
    while (digitalRead(PIN_BUTTON1) == LOW);
    // Keep pull-up enabled in system-off so the wake line doesn't float low.
    nrf_gpio_cfg_sense_input(digitalPinToInterrupt(g_ADigitalPinMap[PIN_BUTTON1]), NRF_GPIO_PIN_PULLUP, NRF_GPIO_PIN_SENSE_LOW);
#endif

#ifdef NRF52_POWER_MANAGEMENT
    initiateShutdown(SHUTDOWN_REASON_USER);
#else
    NRF52Board::powerOff();
#endif
  }
};
