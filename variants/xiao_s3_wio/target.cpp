#include <Arduino.h>
#include "target.h"

XiaoS3WIOBoard board;

#ifdef DUAL_SX1262_REPEATER

static SPIClass spi;
CustomSX1262 valley_radio = new Module(P_LORA_NSS, P_LORA_DIO_1, P_LORA_RESET, P_LORA_BUSY, spi);
CustomSX1262 backhaul_radio = new Module(P_LORA2_NSS, P_LORA2_DIO_1, P_LORA2_RESET, P_LORA2_BUSY, spi);
DualSX1262Wrapper radio_driver(valley_radio, backhaul_radio, board);

#else

#if defined(P_LORA_SCLK)
  static SPIClass spi;
  RADIO_CLASS radio = new Module(P_LORA_NSS, P_LORA_DIO_1, P_LORA_RESET, P_LORA_BUSY, spi);
#else
  RADIO_CLASS radio = new Module(P_LORA_NSS, P_LORA_DIO_1, P_LORA_RESET, P_LORA_BUSY);
#endif

WRAPPER_CLASS radio_driver(radio, board);

#endif

ESP32RTCClock fallback_clock;
AutoDiscoverRTCClock rtc_clock(fallback_clock);
EnvironmentSensorManager sensors;

#ifdef DISPLAY_CLASS
  DISPLAY_CLASS display;
  MomentaryButton user_btn(PIN_USER_BTN, 1000, true);
#endif

bool radio_init() {
  fallback_clock.begin();
  rtc_clock.begin(Wire);
  pinMode(21, INPUT);
  pinMode(48, OUTPUT);

  #if defined(P_LORA_SCLK)
  spi.begin(P_LORA_SCLK, P_LORA_MISO, P_LORA_MOSI);
  #ifdef DUAL_SX1262_REPEATER
  return radio_driver.stdInit(&spi);
  #else
  return radio.std_init(&spi);
  #endif
#else
  return radio.std_init();
#endif
}

mesh::LocalIdentity radio_new_identity() {
#ifdef DUAL_SX1262_REPEATER
  DualSX1262NoiseListener rng(radio_driver);
#else
  RadioNoiseListener rng(radio);
#endif
  return mesh::LocalIdentity(&rng);  // create new random identity
}

