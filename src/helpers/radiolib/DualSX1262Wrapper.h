#pragma once

#include <Arduino.h>
#include <Mesh.h>
#include <Packet.h>
#include "CustomSX1262.h"
#include "SX126xReset.h"

class DualSX1262Wrapper : public mesh::Radio {
public:
  enum Port : uint8_t {
    PortValley = 0,
    PortBackhaul = 1,
    PortNone = 255,
  };

  DualSX1262Wrapper(CustomSX1262& valley, CustomSX1262& backhaul, mesh::MainBoard& board);

  bool stdInit(SPIClass* spi = nullptr);
  void begin() override;
  void powerOff();

  int recvRaw(uint8_t* bytes, int sz) override;
  uint32_t getEstAirtimeFor(int len_bytes) override;
  float packetScore(float snr, int packet_len) override;
  bool startSendRaw(const uint8_t* bytes, int len) override;
  bool isSendComplete() override;
  void onSendFinished() override;
  void loop() override;

  int getNoiseFloor() const override;
  void triggerNoiseFloorCalibrate(int threshold) override;
  void setCADEnabled(bool enable) override { _cad_enabled = enable; }
  void resetAGC() override;
  bool isInRecvMode() const override;
  bool isReceiving() override;
  float getLastRSSI() const override { return _last_rssi; }
  float getLastSNR() const override { return _last_snr; }

  void setParams(float freq, float bw, uint8_t sf, uint8_t cr);
  uint32_t getRngSeed();
  void setTxPower(int8_t dbm);
  void setCommonTxPower(int8_t dbm);
  void loadConfig();
  bool handleCommand(const char* command, char* reply, size_t reply_size);

  uint32_t getPacketsRecv() const { return _n_recv; }
  uint32_t getPacketsRecvErrors() const { return _n_recv_errors; }
  uint32_t getPacketsSent() const { return _n_sent; }
  void resetStats();

  bool setRxBoostedGainMode(bool en);
  bool getRxBoostedGainMode() const;

  Port getLastRxPort() const { return _last_rx_port; }
  Port getLastTxPort() const { return _last_tx_port; }

private:
  enum RadioState : uint8_t {
    StateIdle = 0,
    StateRx = 1,
    StateTxWait = 2,
  };

  struct RadioSlot {
    CustomSX1262* radio;
    const char* name;
    RadioState state;
    bool enabled;
    int8_t tx_power_dbm;
    int16_t noise_floor;
    uint16_t num_floor_samples;
    int32_t floor_sample_sum;
    uint32_t rx_count;
    uint32_t tx_count;
    uint32_t rx_errors;
    uint32_t duplicate_count;
    float last_rssi;
    float last_snr;
  };

  struct RecentIngress {
    uint32_t signature;
    uint32_t seen_ms;
    Port port;
    bool valid;
  };

  static volatile bool valley_flag;
  static volatile bool backhaul_flag;
  static void onValleyAction();
  static void onBackhaulAction();

  mesh::MainBoard* _board;
  RadioSlot _slots[2];

  float _last_rssi;
  float _last_snr;
  Port _last_rx_port;
  Port _last_tx_port;

  uint32_t _n_recv;
  uint32_t _n_recv_errors;
  uint32_t _n_sent;

  uint8_t _tx_buf[MAX_TRANS_UNIT];
  int _tx_len;
  Port _tx_port;
  Port _tx_second_port;
  bool _tx_waiting;
  uint16_t _inter_tx_guard_ms;
  int8_t _common_tx_power_dbm;
  bool _common_power_pending;

  int16_t _threshold;
  bool _cad_enabled;
  RecentIngress _recent[12];

  uint8_t _scratch_rx[MAX_TRANS_UNIT];
  uint8_t _pending_rx[MAX_TRANS_UNIT];
  int _pending_rx_len;
  Port _pending_rx_port;
  float _pending_rx_rssi;
  float _pending_rx_snr;

  bool initOne(Port port);
  void startRecv(Port port);
  void startRecvAll();
  void idleOne(Port port);
  void idleAll();
  bool isFlagSet(Port port) const;
  void clearFlag(Port port);
  bool readFrame(Port port, uint8_t* bytes, int sz, int& len, float& rssi, float& snr);
  int acceptFrame(Port port, uint8_t* bytes, int len, float rssi, float snr);
  void storePending(Port port, const uint8_t* bytes, int len, float rssi, float snr);
  bool startTxOn(Port port);
  bool isReceivingPacket(Port port) const;
  bool isChannelActive(Port port);
  bool applyPortPower(Port port, int8_t dbm);
  bool setPortEnabled(Port port, bool enabled);
  bool setPortTxPower(Port port, int8_t dbm);
  void saveConfig();
  void resetConfig();
  void formatPortConfig(Port port, char* reply, size_t reply_size) const;
  void formatPortStats(Port port, char* reply, size_t reply_size) const;
  static Port parsePort(const char* value);
  static bool parseInt(const char* value, int& result);
  static bool parseBool(const char* value, bool& result);

  uint32_t computeSignature(const uint8_t* bytes, int len) const;
  bool isRecentDuplicate(uint32_t signature) const;
  void rememberIngress(uint32_t signature, Port port);
  Port findIngress(uint32_t signature) const;
  Port chooseTxPort(const uint8_t* bytes, int len, Port* second_port);

  static uint16_t preambleLengthForSF(uint8_t sf) { return sf <= 8 ? 32 : 16; }
  void updatePreamble(uint8_t sf);
  void updateReceiveTimeouts(uint8_t sf, float bw, uint8_t cr);
  float packetScoreInt(float snr, int sf, int packet_len);
  void sampleNoiseFloor(RadioSlot& slot);
};

class DualSX1262NoiseListener : public mesh::RNG {
  DualSX1262Wrapper* _radio;

public:
  DualSX1262NoiseListener(DualSX1262Wrapper& radio) : _radio(&radio) { }

  void random(uint8_t* dest, size_t sz) override {
    uint32_t seed = _radio->getRngSeed();
    for (size_t i = 0; i < sz; i++) {
      seed = 1664525UL * seed + 1013904223UL;
      dest[i] = (uint8_t)((seed >> 16) ^ ::random(0, 256));
    }
  }
};
