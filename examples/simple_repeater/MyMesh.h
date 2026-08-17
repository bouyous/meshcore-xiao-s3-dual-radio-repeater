#pragma once

#include <Arduino.h>
#include <Mesh.h>
#include <RTClib.h>
#include <target.h>

#if defined(NRF52_PLATFORM) || defined(STM32_PLATFORM)
  #include <InternalFileSystem.h>
#elif defined(RP2040_PLATFORM)
  #include <LittleFS.h>
#elif defined(ESP32)
  #include <SPIFFS.h>
  using File = fs::File;
  #ifndef FILE_O_READ
    #define FILE_O_READ FILE_READ
  #endif
  #ifndef FILE_O_WRITE
    #define FILE_O_WRITE FILE_WRITE
  #endif
#endif

#ifdef WITH_RS232_BRIDGE
#include "helpers/bridges/RS232Bridge.h"
#define WITH_BRIDGE
#endif

#ifdef WITH_ESPNOW_BRIDGE
#include "helpers/bridges/ESPNowBridge.h"
#define WITH_BRIDGE
#endif

#include <helpers/AdvertDataHelpers.h>
#if defined(P1_POWER_ALERTS)
#include <helpers/AlertRecipientList.h>
#endif
#include <helpers/ArduinoHelpers.h>
#include <helpers/ClientACL.h>
#include <helpers/CommonCLI.h>
#include <helpers/IdentityStore.h>
#if defined(P1_EVENT_LOG)
#include <helpers/GpsPowerGuard.h>
#endif
#include <helpers/SimpleMeshTables.h>
#include <helpers/StaticPoolPacketManager.h>
#include <helpers/StatsFormatHelper.h>
#include <helpers/TxtDataHelpers.h>
#include <helpers/RegionMap.h>
#include <helpers/RoutingPolicy.h>
#include "RateLimiter.h"

#ifdef WITH_BRIDGE
extern AbstractBridge* bridge;
#endif

struct RepeaterStats {
  uint16_t batt_milli_volts;
  uint16_t curr_tx_queue_len;
  int16_t  noise_floor;
  int16_t  last_rssi;
  uint32_t n_packets_recv;
  uint32_t n_packets_sent;
  uint32_t total_air_time_secs;
  uint32_t total_up_time_secs;
  uint32_t n_sent_flood, n_sent_direct;
  uint32_t n_recv_flood, n_recv_direct;
  uint16_t err_events;                // was 'n_full_events'
  int16_t  last_snr;   // x 4
  uint16_t n_direct_dups, n_flood_dups;
  uint32_t total_rx_air_time_secs;
  uint32_t n_recv_errors;
};

#ifndef MAX_CLIENTS
  #define MAX_CLIENTS           32
#endif

struct NeighbourInfo {
  mesh::Identity id;
  uint32_t advert_timestamp;
  uint32_t heard_timestamp;
  int8_t snr; // multiplied by 4, user should divide to get float value
};

#ifndef FIRMWARE_BUILD_DATE
  #define FIRMWARE_BUILD_DATE   "14 Aug 2026"
#endif

#ifndef FIRMWARE_VERSION
  #define FIRMWARE_VERSION   "v1.17.1"
#endif

#ifndef POWER_ALERT_NODE_LABEL
  #define POWER_ALERT_NODE_LABEL "P1"
#endif

#ifndef POWER_ALERT_CHANNEL_COMMAND
  #define POWER_ALERT_CHANNEL_COMMAND "!p1"
#endif

#define FIRMWARE_ROLE "repeater"

#define PACKET_LOG_FILE  "/packet_log"
#if defined(P1_EVENT_LOG)
#define P1_GPS_CONTINUOUS_FILE  "/p1_gps_continuous"
#define P1_GPS_POWER_GUARD_FILE      "/p1_gps_powerguard"
#define P1_GPS_POWER_GUARD_TEMP_FILE "/p1_gps_powerguard.tmp"
#endif
#if defined(P1_POWER_ALERTS)
#define P1_ALERT_RECIPIENTS_FILE      "/p1_alert_recipients"
#define P1_ALERT_RECIPIENTS_TEMP_FILE "/p1_alert_recipients.tmp"
#define P1_ALERT_CONFIG_FILE           "/p1_alert_config"
#define P1_ALERT_CONFIG_TEMP_FILE      "/p1_alert_config.tmp"
#define P1_ALERT_CHANNEL_FILE           "/p1_alert_channel"
#define P1_ALERT_CHANNEL_TEMP_FILE      "/p1_alert_channel.tmp"
#endif

class MyMesh : public mesh::Mesh, public CommonCLICallbacks {
  FILESYSTEM* _fs;
  uint32_t last_millis;
  uint64_t uptime_millis;
  unsigned long next_local_advert, next_flood_advert;
  bool _logging;
  NodePrefs _prefs;
  ClientACL  acl;
  CommonCLI _cli;
  uint8_t reply_data[MAX_PACKET_PAYLOAD];
  uint8_t reply_path[MAX_PATH_SIZE];
  uint8_t reply_path_len;
  TransportKeyStore key_store;
  RegionMap region_map, temp_map;
  RegionEntry* load_stack[8];
  RegionEntry* recv_pkt_region;
  TransportKey default_scope;
  RateLimiter discover_limiter, anon_limiter;
  uint32_t pending_discover_tag;
  unsigned long pending_discover_until;
  bool region_load_active;
  unsigned long dirty_contacts_expiry;
#if MAX_NEIGHBOURS
  NeighbourInfo neighbours[MAX_NEIGHBOURS];
#endif
  CayenneLPP telemetry;
  unsigned long set_radio_at, revert_radio_at;
  float pending_freq;
  float pending_bw;
  uint8_t pending_sf;
  uint8_t pending_cr;
  int  matching_peer_indexes[MAX_CLIENTS];
#if defined(WITH_RS232_BRIDGE)
  RS232Bridge bridge;
#elif defined(WITH_ESPNOW_BRIDGE)
  ESPNowBridge bridge;
#endif

  void putNeighbour(const mesh::Identity& id, uint32_t timestamp, float snr);
  uint8_t handleLoginReq(const mesh::Identity& sender, const uint8_t* secret, uint32_t sender_timestamp, const uint8_t* data, bool is_flood);
  uint8_t handleAnonRegionsReq(const mesh::Identity& sender, uint32_t sender_timestamp, const uint8_t* data);
  uint8_t handleAnonOwnerReq(const mesh::Identity& sender, uint32_t sender_timestamp, const uint8_t* data);
  uint8_t handleAnonClockReq(const mesh::Identity& sender, uint32_t sender_timestamp, const uint8_t* data);
  int handleRequest(ClientInfo* sender, uint32_t sender_timestamp, uint8_t* payload, size_t payload_len);
  mesh::Packet* createSelfAdvert();

  File openAppend(const char* fname);
#if defined(P1_EVENT_LOG)
  bool persistGpsContinuous(bool enabled);
  bool gpsContinuousPersisted() const;
  mesh::GpsPowerGuardMode gps_power_guard_mode =
      mesh::GpsPowerGuardMode::ECONOMY;
  bool loadGpsPowerGuard();
  bool saveGpsPowerGuard();
  void handleGpsPowerGuardCommand(const char* command, char* reply);
#endif
#if defined(P1_POWER_ALERTS)
  enum class AlertDestination : uint8_t {
    PRIVATE = 0,
    PUBLIC,
    CUSTOM_CHANNEL
  };
  static constexpr size_t MAX_ALERT_RECIPIENTS = 4;
  static constexpr size_t MAX_ALERT_TEXT_LEN = 140;
  static constexpr size_t MAX_ALERT_CHANNEL_NAME_LEN = 31;
  static constexpr size_t MAX_ALERT_CHANNEL_KEY_LEN = 64;
  mesh::AlertRecipientList<MAX_ALERT_RECIPIENTS> alert_recipients;
  AlertDestination alert_destination = AlertDestination::PRIVATE;
  mesh::GroupChannel public_alert_channel = {};
  mesh::GroupChannel custom_alert_channel = {};
  char custom_alert_channel_name[MAX_ALERT_CHANNEL_NAME_LEN + 1] = {};
  char custom_alert_channel_key[MAX_ALERT_CHANNEL_KEY_LEN + 1] = {};
  bool custom_alert_channel_configured = false;
  uint32_t pending_alert_ack[MAX_ALERT_RECIPIENTS] = {};
  bool pending_alert_waiting[MAX_ALERT_RECIPIENTS] = {};
  char pending_alert_text[MAX_ALERT_TEXT_LEN + 1] = {};
  uint32_t pending_alert_timestamp[MAX_ALERT_RECIPIENTS] = {};
  uint32_t pending_alert_retry_at = 0;
  uint32_t pending_alert_finish_at = 0;
  bool pending_alert_active = false;
  bool pending_alert_retried = false;
  bool pending_alert_channel_waiting = false;
  uint32_t pending_alert_channel_timestamp = 0;
  bool deferred_alert_pending = false;
  char deferred_alert_text[MAX_ALERT_TEXT_LEN + 1] = {};
  bool power_state_initialized = false;
  char previous_power_state[16] = {};
  uint16_t early_alert_mv = P1_EARLY_ALERT_MV;
  uint16_t early_alert_clear_mv = P1_EARLY_ALERT_CLEAR_MV;
  bool early_alert_latched = false;
  uint32_t last_channel_cli_at = 0;

  bool loadAlertRecipients();
  bool saveAlertRecipients();
  bool loadAlertConfig();
  bool saveAlertConfig();
  bool loadAlertChannelConfig();
  bool saveAlertChannelConfig();
  bool configureAlertChannel(mesh::GroupChannel& channel,
                             const char* encoded_key,
                             char* normalized_key = nullptr,
                             size_t normalized_key_size = 0);
  bool sendGroupText(const mesh::GroupChannel& channel, const char* text,
                     uint32_t timestamp, uint32_t delay_millis = 0,
                     const mesh::Packet* reply_to = nullptr);
  bool sendAlertToChannel(const char* text);
  bool sendChannelCliReply(const char* text,
                           const mesh::Packet* incoming_packet,
                           uint32_t incoming_timestamp);
  void handleChannelCli(const char* command, char* reply, size_t reply_size);
  uint8_t alertDestinationCount() const;
  const char* alertDestinationName() const;
  bool sendAlertTo(size_t index, const char* text, uint8_t attempt);
  uint8_t startOperationalAlert(const char* text, bool replace_active = false);
  void serviceOperationalAlerts();
  void handleAlertCommand(const char* command, char* reply);
#endif
  bool isLooped(const mesh::Packet* packet, const uint8_t max_counters[]);

protected:
  float getAirtimeBudgetFactor() const override {
    return _prefs.airtime_factor;
  }

  bool allowPacketForward(const mesh::Packet* packet) override;
  const char* getLogDateTime() override;
  void logRxRaw(float snr, float rssi, const uint8_t raw[], int len) override;

  void logRx(mesh::Packet* pkt, int len, float score) override;
  void logTx(mesh::Packet* pkt, int len) override;
  void logTxFail(mesh::Packet* pkt, int len) override;
  int calcRxDelay(float score, uint32_t air_time) const override;

  uint32_t getRetransmitDelay(const mesh::Packet* packet) override;
  uint32_t getDirectRetransmitDelay(const mesh::Packet* packet) override;

  int getInterferenceThreshold() const override {
    return _prefs.interference_threshold;
  }
  bool getCADEnabled() const override {
    return _prefs.cad_enabled;
  }
  int getAGCResetInterval() const override {
    return ((int)_prefs.agc_reset_interval) * 4000;   // milliseconds
  }
  uint8_t getExtraAckTransmitCount() const override {
    return _prefs.multi_acks;
  }

#if ENV_INCLUDE_GPS == 1
  void applyGpsPrefs() {
  #ifdef GPS_SCHEDULE_FORCE_ENABLE
    if (!_prefs.gps_enabled) {
      _prefs.gps_enabled = 1;
      savePrefs();
    }
  #endif
    sensors.setSettingValue("gps", _prefs.gps_enabled?"1":"0");
  }
#endif

  mesh::DispatcherAction onRecvPacket(mesh::Packet* pkt) override;

  void onAnonDataRecv(mesh::Packet* packet, const uint8_t* secret, const mesh::Identity& sender, uint8_t* data, size_t len) override;
  int searchPeersByHash(const uint8_t* hash) override;
  void getPeerSharedSecret(uint8_t* dest_secret, int peer_idx) override;
  void onAdvertRecv(mesh::Packet* packet, const mesh::Identity& id, uint32_t timestamp, const uint8_t* app_data, size_t app_data_len);
  void onPeerDataRecv(mesh::Packet* packet, uint8_t type, int sender_idx, const uint8_t* secret, uint8_t* data, size_t len) override;
  bool onPeerPathRecv(mesh::Packet* packet, int sender_idx, const uint8_t* secret, uint8_t* path, uint8_t path_len, uint8_t extra_type, uint8_t* extra, uint8_t extra_len) override;
  void onControlDataRecv(mesh::Packet* packet) override;
#if defined(P1_POWER_ALERTS)
  void onAckRecv(mesh::Packet* packet, uint32_t ack_crc) override;
  int searchChannelsByHash(const uint8_t* hash,
                           mesh::GroupChannel channels[],
                           int max_matches) override;
  void onGroupDataRecv(mesh::Packet* packet, uint8_t type,
                       const mesh::GroupChannel& channel, uint8_t* data,
                       size_t len) override;
#endif

  void sendFloodReply(mesh::Packet* packet, unsigned long delay_millis, uint8_t path_hash_size);

public:
  MyMesh(mesh::MainBoard& board, mesh::Radio& radio, mesh::MillisecondClock& ms, mesh::RNG& rng, mesh::RTCClock& rtc, mesh::MeshTables& tables);

  void begin(FILESYSTEM* fs);
  void sendNodeDiscoverReq();
  const char* getFirmwareVer() override { return FIRMWARE_VERSION; }
  const char* getBuildDate() override { return FIRMWARE_BUILD_DATE; }
  const char* getRole() override { return FIRMWARE_ROLE; }
  const char* getNodeName() { return _prefs.node_name; }
  NodePrefs* getNodePrefs() {
    return &_prefs;
  }

  void savePrefs() override {
    _cli.savePrefs(_fs);
  }

  void sendFloodScoped(const TransportKey& scope, mesh::Packet* pkt, uint32_t delay_millis, uint8_t path_hash_size);

  // CommonCLICallbacks
  void applyTempRadioParams(float freq, float bw, uint8_t sf, uint8_t cr, int timeout_mins) override;
  bool formatFileSystem() override;
  void sendSelfAdvertisement(int delay_millis, bool flood) override;
  void updateAdvertTimer() override;
  void updateFloodAdvertTimer() override;

  void setLoggingOn(bool enable) override { _logging = enable; }

  void eraseLogFile() override {
    _fs->remove(PACKET_LOG_FILE);
  }

  void dumpLogFile() override;
  void setTxPower(int8_t power_dbm) override;
  void formatNeighborsReply(char *reply) override;
  void removeNeighbor(const uint8_t* pubkey, int key_len) override;
  void formatStatsReply(char *reply) override;
  void formatRadioStatsReply(char *reply) override;
  void formatPacketStatsReply(char *reply) override;
  void startRegionsLoad() override;
  bool saveRegions() override;
  void onDefaultRegionChanged(const RegionEntry* r) override;

  mesh::LocalIdentity& getSelfId() override { return self_id; }

  void saveIdentity(const mesh::LocalIdentity& new_id) override;
  void clearStats() override;

#if defined(P1_POWER_ALERTS)
  void notifyPowerStatus(const mesh::MainBoard::PowerStatus& status);
  void sendBootAlert(uint16_t battery_mv, const char* reset_reason,
                     const char* previous_shutdown_reason);
  void sendGpsFixAlert(uint32_t acquisition_seconds, int32_t satellites);
#endif
#if defined(P1_EVENT_LOG)
  bool shouldGpsPowerSave(const mesh::MainBoard::PowerStatus& status) const {
    return mesh::GpsPowerGuard::shouldSuppress(gps_power_guard_mode,
                                               status.state);
  }
#endif

  void handleCommand(uint32_t sender_timestamp, char* command, char* reply);
  void loop();

#if defined(WITH_BRIDGE)
  void setBridgeState(bool enable) override {
    if (enable == bridge.isRunning()) return;
    if (enable)
    {
      bridge.begin();
    }
    else 
    {
      bridge.end();
    }
  }

  void restartBridge() override {
    if (!bridge.isRunning()) return;
    bridge.end();
    bridge.begin();
  }
#endif

  // To check if there is pending work
  bool hasPendingWork() const;

  bool setRxBoostedGain(bool enable) override;

  #if defined(USE_LR2021)
  virtual bool configSideDetectors(const uint8_t sideDetSFs[], uint8_t num, float bw) override;
  #endif

};
