#pragma once

#include <Arduino.h>
#include <helpers/ESP32Board.h>

class XiaoS3WIOBoard : public ESP32Board {
public:
  XiaoS3WIOBoard() { }

  const char* getManufacturerName() const override {
#ifdef DUAL_SX1262_REPEATER
    return "Xiao S3 WIO Dual SX1262";
#else
    return "Xiao S3 WIO";
#endif
  }
};
