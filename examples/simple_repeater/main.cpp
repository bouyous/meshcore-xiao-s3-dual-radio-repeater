#include <Arduino.h>   // needed for PlatformIO
#include <Mesh.h>

#include "MyMesh.h"

#if defined(P1_EVENT_LOG_QSPI)
  #include <CustomLFS_QSPIFlash.h>
#endif

#ifdef DISPLAY_CLASS
  #include "UITask.h"
  static UITask ui_task(board, display);
#endif

#ifdef ETHERNET_ENABLED
  #define ETHERNET_CLI_BANNER "MeshCore Repeater CLI"
  #include <helpers/nrf52/EthernetCLI.h>
#endif

StdRNG fast_rng;
SimpleMeshTables tables;

MyMesh the_mesh(board, radio_driver, *new ArduinoMillis(), fast_rng, rtc_clock, tables);

#if defined(P1_EVENT_LOG_QSPI)
static bool migrateP1EventJournal(Adafruit_LittleFS& source,
                                  Adafruit_LittleFS& destination) {
  static constexpr const char* path = "/p1_events.bin";
  if (destination.exists(path)) return true;
  if (!source.exists(path)) return true;

  File input = source.open(path, FILE_O_READ);
  File output = destination.open(path, FILE_O_WRITE);
  if (!input || !output) {
    if (input) input.close();
    if (output) output.close();
    return false;
  }

  uint8_t buffer[120];  // exactly four packed journal records
  bool ok = true;
  while (input.available()) {
    const int count = input.read(buffer, sizeof(buffer));
    if (count <= 0 || output.write(buffer, count) != (size_t)count) {
      ok = false;
      break;
    }
  }
  output.flush();
  input.close();
  output.close();
  if (!ok) destination.remove(path);
  return ok;
}
#endif

void halt() {
  while (1) ;
}

static char command[160];
#ifdef ETHERNET_ENABLED
static char ethernet_command[160];
#endif

// For power saving
unsigned long POWERSAVING_FIRSTSLEEP_SECS = 120; // The first sleep (if enabled) from boot

#if defined(PIN_USER_BTN) && defined(_SEEED_SENSECAP_SOLAR_H_) && P1_BUTTON_POWEROFF_ENABLED
static unsigned long userBtnDownAt = 0;
#endif

void setup() {
  Serial.begin(115200);
  delay(1000);

  board.begin();

  FILESYSTEM* fs;
#if defined(NRF52_PLATFORM) || defined(STM32_PLATFORM)
  const bool filesystem_ready = InternalFS.begin();
  fs = &InternalFS;
  IdentityStore store(InternalFS, "");
#elif defined(ESP32)
  const bool filesystem_ready = SPIFFS.begin(true);
  fs = &SPIFFS;
  IdentityStore store(SPIFFS, "/identity");
#elif defined(RP2040_PLATFORM)
  const bool filesystem_ready = LittleFS.begin();
  fs = &LittleFS;
  IdentityStore store(LittleFS, "/identity");
  store.begin();
#else
  #error "need to define filesystem"
#endif

  if (!filesystem_ready) {
    Serial.println("FATAL: filesystem init failed");
    halt();
  }

#if defined(P1_EVENT_LOG)
  Adafruit_LittleFS* journal_fs = fs;
#if defined(P1_EVENT_LOG_QSPI)
  // InternalFS writes on nRF52 can wait forever for a missed SoftDevice flash
  // event.  The P1's dedicated P25Q16H QSPI device has bounded operation
  // timeouts, so keep high-frequency diagnostics away from MCU flash.
  if (QSPIFlash.begin()) {
    journal_fs = &QSPIFlash;
    Serial.println("P1 event journal: external QSPI");
    if (!migrateP1EventJournal(*fs, QSPIFlash)) {
      Serial.println("ERROR: legacy P1 journal migration failed");
    }
  } else {
    journal_fs = nullptr;
    Serial.println("ERROR: QSPI event journal unavailable; logging disabled");
  }
#endif
  if (journal_fs && p1_event_journal.begin(*journal_fs, rtc_clock)) {
    board.setEventJournal(&p1_event_journal);
    sensors.setEventSink(&p1_event_journal);
    p1_event_journal.recordBoot(board.getResetReason(),
                                board.getShutdownReason(),
                                board.getBootVoltage(),
                                board.isExternalPowered());
    uint8_t released_buttons = 0;
#ifdef PIN_BUTTON1
    if (digitalRead(PIN_BUTTON1) == HIGH) released_buttons |= 0x01;
#endif
#ifdef PIN_BUTTON2
    if (digitalRead(PIN_BUTTON2) == HIGH) released_buttons |= 0x02;
#else
    released_buttons |= 0x02;
#endif
    p1_event_journal.recordButtonState(released_buttons);
  } else {
    Serial.println("ERROR: P1 event journal unavailable");
  }
#endif

#ifdef HAS_EXTERNAL_WATCHDOG
  external_watchdog.begin();
#endif

#if defined(MESH_DEBUG) && defined(NRF52_PLATFORM)
  // give some extra time for serial to settle so
  // boot debug messages can be seen on terminal
  delay(5000);
#endif

#ifdef DISPLAY_CLASS
  if (display.begin()) {
    display.startFrame();
    display.setCursor(0, 0);
    display.print("Please wait...");
    display.endFrame();
  }
#endif

  if (!radio_init()) {
    MESH_DEBUG_PRINTLN("Radio init failed!");
#if defined(P1_EVENT_LOG)
    p1_event_journal.recordInitError(P1EventJournal::INIT_ERROR_RADIO);
#endif
    halt();
  }

  fast_rng.begin(radio_driver.getRngSeed());

  if (!store.load("_main", the_mesh.self_id)) {
    MESH_DEBUG_PRINTLN("Generating new keypair");
    the_mesh.self_id = radio_new_identity();   // create new random identity
    int count = 0;
    while (count < 10 && (the_mesh.self_id.pub_key[0] == 0x00 || the_mesh.self_id.pub_key[0] == 0xFF)) {  // reserved id hashes
      the_mesh.self_id = radio_new_identity(); count++;
    }
    store.save("_main", the_mesh.self_id);
  }

  Serial.print("Repeater ID: ");
  mesh::Utils::printHex(Serial, the_mesh.self_id.pub_key, PUB_KEY_SIZE); Serial.println();

  command[0] = 0;
#ifdef ETHERNET_ENABLED
  ethernet_command[0] = 0;
#endif

#ifndef DISABLE_SENSOR_DISCOVERY
  if (!sensors.begin()) {
#if defined(P1_EVENT_LOG)
    p1_event_journal.recordInitError(P1EventJournal::INIT_ERROR_SENSORS);
#endif
  }
#endif

  the_mesh.begin(fs);

#if defined(P1_EVENT_LOG)
  NodePrefs* radio_prefs = the_mesh.getNodePrefs();
  p1_event_journal.recordRadioReady(radio_prefs->freq,
                                    radio_prefs->tx_power_dbm,
                                    radio_prefs->sf,
                                    radio_prefs->cr);
  Serial.printf("Radio ready: %.6f MHz, TX %d dBm, SF%u, CR%u\n",
                radio_prefs->freq, radio_prefs->tx_power_dbm,
                radio_prefs->sf, radio_prefs->cr);
#endif

#ifdef DISPLAY_CLASS
  ui_task.begin(the_mesh.getNodePrefs(), FIRMWARE_BUILD_DATE, FIRMWARE_VERSION);
#endif

#ifdef ETHERNET_ENABLED
  ethernet_start_task();
#endif

  // send out initial zero hop Advertisement to the mesh
#if ENABLE_ADVERT_ON_BOOT == 1
  the_mesh.sendSelfAdvertisement(16000, false);
#endif

  board.onBootComplete();
#if defined(P1_EVENT_LOG)
  p1_event_journal.recordBootComplete(board.getBattMilliVolts());
#endif
#if defined(P1_POWER_ALERTS)
  the_mesh.sendBootAlert(
      board.getBattMilliVolts(),
      board.getResetReasonString(board.getResetReason()),
      board.getShutdownReasonString(board.getShutdownReason()));
#endif
}

void loop() {
  // Handle Serial CLI
  int len = strlen(command);
  while (Serial.available() && len < sizeof(command)-1) {
    char c = Serial.read();
    if (c != '\n') {
      command[len++] = c;
      command[len] = 0;
      Serial.print(c);
    }
    if (c == '\r') break;
  }
  if (len == sizeof(command)-1) {  // command buffer full
    command[sizeof(command)-1] = '\r';
  }

  if (len > 0 && command[len - 1] == '\r') {  // received complete line
    Serial.print('\n');
    command[len - 1] = 0;  // replace newline with C string null terminator
    char reply[160];
    reply[0] = 0;
#ifdef ETHERNET_ENABLED
    if (!ethernet_handle_command(command, reply)) {
      the_mesh.handleCommand(0, command, reply);
    }
#else
    the_mesh.handleCommand(0, command, reply);  // NOTE: there is no sender_timestamp via serial!
#endif
    if (reply[0]) {
      Serial.print("  -> "); Serial.println(reply);
    }

    command[0] = 0;  // reset command buffer
  }

#ifdef ETHERNET_ENABLED
  ethernet_loop_maintain();
  if (ethernet_read_line(ethernet_command, sizeof(ethernet_command))) {
    char reply[160];
    reply[0] = 0;
    if (!ethernet_handle_command(ethernet_command, reply)) {
      the_mesh.handleCommand(0, ethernet_command, reply);
    }
    ethernet_send_reply(reply);
    ethernet_command[0] = 0;
  }
#endif

#if defined(PIN_USER_BTN) && defined(_SEEED_SENSECAP_SOLAR_H_) && \
    P1_BUTTON_POWEROFF_ENABLED && !defined(DISPLAY_CLASS)
  // Hold the user button to power off the SenseCAP Solar repeater.
  int btnState = digitalRead(PIN_USER_BTN);
  if (btnState == LOW) {
    if (userBtnDownAt == 0) {
      userBtnDownAt = millis();
    } else if ((unsigned long)(millis() - userBtnDownAt) >= P1_BUTTON_POWEROFF_HOLD_MS) {
      Serial.println("Powering off...");
      board.powerOff();  // does not return
    }
  } else {
    userBtnDownAt = 0;
  }
#endif

  the_mesh.loop();
  board.servicePowerManagement();
  mesh::MainBoard::PowerStatus power_status;
  if (board.getPowerStatus(power_status)) {
#if defined(P1_POWER_ALERTS)
    the_mesh.notifyPowerStatus(power_status);
#endif
    sensors.setPowerSaveMode(power_status.power_saving);
  }
  sensors.loop();
  if (sensors.consumeFreshLocation()) {
    // One flood advert per scheduled GPS window, after the first valid fix,
    // so the daily acquisition is actually propagated through the mesh.
    NodePrefs* prefs = the_mesh.getNodePrefs();
    // Do not erase internal flash for normal GPS wander.  Roughly 0.001 deg
    // is 70-110 m in France: large enough to persist a genuine relocation,
    // while ordinary fixes remain RAM-only and are still advertised.
    const bool location_changed =
        fabs(prefs->node_lat - sensors.node_lat) > 0.001 ||
        fabs(prefs->node_lon - sensors.node_lon) > 0.001 ||
        prefs->advert_loc_policy != ADVERT_LOC_PREFS;
    prefs->node_lat = sensors.node_lat;
    prefs->node_lon = sensors.node_lon;
    prefs->advert_loc_policy = ADVERT_LOC_PREFS;
    if (location_changed) the_mesh.savePrefs();
    the_mesh.sendSelfAdvertisement(1500, true);
  }
#ifdef DISPLAY_CLASS
  ui_task.loop();
#endif
  rtc_clock.tick();

#ifdef HAS_EXTERNAL_WATCHDOG
  external_watchdog.loop();
#endif
  if (the_mesh.getNodePrefs()->powersaving_enabled && !the_mesh.hasPendingWork()) {
#if defined(NRF52_PLATFORM)
    board.sleep(0); // nrf ignores seconds param, sleeps whenever possible
#else
    if (the_mesh.millisHasNowPassed(POWERSAVING_FIRSTSLEEP_SECS * 1000)) { // To check if it is time to sleep
      board.sleep(30); // Sleep. Wake up after a while or when receiving a LoRa packet
    }
#endif
  }
}
