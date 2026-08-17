#ifndef _SEEED_SENSECAP_SOLAR_H_
#define _SEEED_SENSECAP_SOLAR_H_

/** Master clock frequency */
#define VARIANT_MCK             (64000000ul)

#define USE_LFXO                // Board uses 32khz crystal for LF

/*----------------------------------------------------------------------------
 *        Headers
 *----------------------------------------------------------------------------*/

#include "WVariant.h"

#define PINS_COUNT              (33)
#define NUM_DIGITAL_PINS        (33)
#define NUM_ANALOG_INPUTS       (8)
#define NUM_ANALOG_OUTPUTS      (0)

// LEDs
#define PIN_LED                 (12)
#define LED_PWR                 (PINS_COUNT)

#define LED_BUILTIN             (PIN_LED)

#define LED_RED                 (PINS_COUNT)
#define LED_WHITE               (11)
#define LED_BLUE                (12)    // LoRa TX indicator

#define LED_STATE_ON            (1)     // State when LED is litted

// Buttons
#define PIN_BUTTON1             (13)
#define PIN_BUTTON2             (20)
#define PIN_USER_BTN            PIN_BUTTON1

// The upstream repeater treated 1.5 s LOW on PIN_BUTTON1 as an intentional
// power-off.  A field unit has now produced that condition without a user
// action, leaving the solar wake path disarmed.  Keep physical power-off
// disabled in the recovery profile until both carrier button nets have been
// verified.  CLI shutdown remains available.
#ifndef P1_BUTTON_POWEROFF_ENABLED
#define P1_BUTTON_POWEROFF_ENABLED (0)
#endif
#ifndef P1_BUTTON_POWEROFF_HOLD_MS
#define P1_BUTTON_POWEROFF_HOLD_MS (5000UL)
#endif

// Wio-SX1262/SX1262 accepts at most +22 dBm.  The generic repeater CLI used
// to accept +30 dBm and silently ignore RadioLib's error return.
#ifndef MAX_LORA_TX_POWER
#define MAX_LORA_TX_POWER       (22)
#endif

#define VBAT_ENABLE             (19)    // Output LOW to enable reading of the BAT voltage.

// Analog pins
#define BATTERY_PIN             (16)    // Read the BAT voltage.
#define AREF_VOLTAGE            (3.0F)
#define ADC_MULTIPLIER          (3.004F) // Official 1M/499k divider, (1000+499)/499
#define ADC_RESOLUTION          (12)

// nRF52 power management settings
#define PWRMGT_VOLTAGE_BOOTLOCK (3300) // Won't boot below this voltage (mV)
#define PWRMGT_LPCOMP_AIN       (7)    // AIN7 = P0.31 = BATTERY_PIN
#define PWRMGT_LPCOMP_REFSEL    (2)    // 3/8 VDD; nominal rising wake ~3.77V

// Runtime battery protection. Values are compile-time overrides so field
// testing can tune policy without changing the implementation.
#ifndef PWR_LOW_WARNING_MV
#define PWR_LOW_WARNING_MV       (3500)
#endif
#ifndef PWR_LOW_HYSTERESIS_MV
#define PWR_LOW_HYSTERESIS_MV    (50)
#endif
#ifndef PWR_SHUTDOWN_MV
#define PWR_SHUTDOWN_MV          (3300)
#endif
#ifndef PWR_CRITICAL_CLEAR_MV
#define PWR_CRITICAL_CLEAR_MV    (3350)
#endif
#ifndef PWR_SHUTDOWN_DELAY_SEC
#define PWR_SHUTDOWN_DELAY_SEC   (600)
#endif
#ifndef PWR_SAMPLE_INTERVAL_SEC
#define PWR_SAMPLE_INTERVAL_SEC  (30)
#endif

// Independent pre-shutdown warning. At 3.35 V the four-cell pack still has
// enough reserve for an encrypted LoRa alert before the 3.30 V decision.
#ifndef P1_EARLY_ALERT_MV
#define P1_EARLY_ALERT_MV        (3350)
#endif
#ifndef P1_EARLY_ALERT_CLEAR_MV
#define P1_EARLY_ALERT_CLEAR_MV  (3400)
#endif
#ifndef P1_ALERT_SHUTDOWN_GRACE_SEC
#define P1_ALERT_SHUTDOWN_GRACE_SEC (12)
#endif

// Serial interfaces
#define PIN_SERIAL1_RX          (7)
#define PIN_SERIAL1_TX          (6)

// SPI Interfaces
#define SPI_INTERFACES_COUNT    (1)

#define PIN_SPI_MISO            (9)
#define PIN_SPI_MOSI            (10)
#define PIN_SPI_SCK             (8)

// Lora SPI is on SPI0
#define  P_LORA_SCLK            PIN_SPI_SCK
#define  P_LORA_MISO            PIN_SPI_MISO
#define  P_LORA_MOSI            PIN_SPI_MOSI

// Wire Interfaces
#define WIRE_INTERFACES_COUNT   (1)

#define PIN_WIRE_SDA            (14)
#define PIN_WIRE_SCL            (15)

// GPS L76KB
#define GPS_BAUDRATE            9600
#define GPS_THREAD_INTERVAL     50
#define PIN_GPS_TX              PIN_SERIAL1_RX
#define PIN_GPS_RX              PIN_SERIAL1_TX
#define PIN_GPS_STANDBY         (0)
#define GPS_EN                  (18)

// Daily GPS policy for a fixed repeater. The first one-hour window begins at
// boot, then repeats every 24 hours. Battery economy/critical states override
// the schedule and keep the GPS off.
#ifndef GPS_SCHEDULE_PERIOD_SEC
#define GPS_SCHEDULE_PERIOD_SEC  (24UL * 60UL * 60UL)
#endif
#ifndef GPS_SCHEDULE_WINDOW_SEC
#define GPS_SCHEDULE_WINDOW_SEC  (60UL * 60UL)
#endif
#define GPS_SCHEDULE_FORCE_ENABLE 1

#ifdef PWR_TEST_STANDBY_WAKE_MV
#ifndef PWR_TEST_STANDBY_SAMPLE_SEC
#define PWR_TEST_STANDBY_SAMPLE_SEC 30
#endif
#ifndef PWR_TEST_STANDBY_CONFIRM_SAMPLES
#define PWR_TEST_STANDBY_CONFIRM_SAMPLES 3
#endif
#endif

// QSPI Pins
#define PIN_QSPI_SCK            (21)
#define PIN_QSPI_CS             (22)
#define PIN_QSPI_IO0            (23)
#define PIN_QSPI_IO1            (24)
#define PIN_QSPI_IO2            (25)
#define PIN_QSPI_IO3            (26)

#define EXTERNAL_FLASH_DEVICES P25Q16H
#define EXTERNAL_FLASH_USE_QSPI

#endif
