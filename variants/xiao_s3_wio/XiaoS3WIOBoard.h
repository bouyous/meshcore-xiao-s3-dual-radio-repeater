#pragma once

#include <Arduino.h>
#include <helpers/ESP32Board.h>
#include <helpers/PowerStateMachine.h>

class XiaoS3WIOBoard : public ESP32Board {
protected:
#ifdef RUNTIME_POWER_MANAGEMENT
  mesh::PowerStateMachine power_state_machine;
  uint32_t next_power_sample_ms = 0;
  bool low_voltage_shutdown_pending = false;
  uint32_t low_voltage_shutdown_at_ms = 0;
  uint16_t boot_voltage_mv = 0;
  uint8_t previous_shutdown_reason = 0;
  float battery_divider_multiplier = XIAO_VBAT_DIVIDER_MULTIPLIER;

  void enterLowVoltageRecoverySleep();
  void releaseRadioPinHolds();
#endif

public:
  XiaoS3WIOBoard();

  void begin();
  void servicePowerManagement() override;
  bool getPowerStatus(mesh::MainBoard::PowerStatus& status) const override;
  uint16_t getBattMilliVolts() override;
  uint16_t getBootVoltage() override;
  uint8_t getShutdownReason() const override;
  const char* getShutdownReasonString(uint8_t reason) override;
  void powerOffLowVoltage() override;
  bool setAdcMultiplier(float multiplier) override;
  float getAdcMultiplier() const override;

  const char* getManufacturerName() const override {
#ifdef DUAL_SX1262_REPEATER
    return "Xiao S3 WIO Dual SX1262";
#else
    return "Xiao S3 WIO";
#endif
  }
};
