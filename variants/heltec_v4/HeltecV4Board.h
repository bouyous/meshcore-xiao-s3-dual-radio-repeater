#pragma once

#include <Arduino.h>
#include <helpers/RefCountedDigitalPin.h>
#include <helpers/ESP32Board.h>
#include <helpers/PowerStateMachine.h>
#include "LoRaFEMControl.h"

#ifndef ADC_MULTIPLIER
  #define ADC_MULTIPLIER 5.42
#endif

class HeltecV4Board : public ESP32Board {

protected:
  float adc_mult = ADC_MULTIPLIER;

#ifdef RUNTIME_POWER_MANAGEMENT
  mesh::PowerStateMachine power_state_machine;
  uint32_t next_power_sample_ms = 0;
  bool low_voltage_shutdown_pending = false;
  uint32_t low_voltage_shutdown_at_ms = 0;
  uint16_t boot_voltage_mv = 0;
  uint8_t previous_shutdown_reason = 0;
#endif

#ifdef AUTO_SHUTDOWN_MILLIVOLTS
  void enterLowVoltageRecoverySleep();
#endif

public:
  RefCountedDigitalPin periph_power;
  LoRaFEMControl loRaFEMControl;
  HeltecV4Board();

  void begin();
  void servicePowerManagement() override;
  bool getPowerStatus(mesh::MainBoard::PowerStatus& status) const override;
  uint16_t getBootVoltage() override;
  uint8_t getShutdownReason() const override;
  const char* getShutdownReasonString(uint8_t reason) override;
  void onBeforeTransmit(void) override;
  void onAfterTransmit(void) override;
  void powerOff() override;
  bool setLoRaFemLnaEnabled(bool enable) override;
  bool canControlLoRaFemLna() const override;
  bool isLoRaFemLnaEnabled() const override;
  uint16_t getBattMilliVolts() override;
  bool setAdcMultiplier(float multiplier) override {
    if (multiplier == 0.0f) {
      adc_mult = ADC_MULTIPLIER;
    } else {
      adc_mult = multiplier;
    }
    return true;
  }
  float getAdcMultiplier() const override { return adc_mult; }
  const char* getManufacturerName() const override;
};
