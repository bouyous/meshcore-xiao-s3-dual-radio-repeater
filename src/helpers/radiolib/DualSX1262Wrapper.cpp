#include "DualSX1262Wrapper.h"

#include <Preferences.h>
#include <stdlib.h>
#include <string.h>

#define DUAL_STATE_IDLE       0
#define DUAL_STATE_RX         1
#define DUAL_STATE_TX_WAIT    2

#define NUM_NOISE_FLOOR_SAMPLES 64
#define SAMPLING_THRESHOLD 14

#ifndef DUAL_SX1262_INGRESS_WINDOW_MS
#define DUAL_SX1262_INGRESS_WINDOW_MS 60000UL
#endif

#ifndef DUAL_SX1262_DUP_WINDOW_MS
#define DUAL_SX1262_DUP_WINDOW_MS 5000UL
#endif

#ifndef DUAL_SX1262_INTER_TX_GUARD_MS
#define DUAL_SX1262_INTER_TX_GUARD_MS 10
#endif

#ifndef DUAL_SX1262_LOCAL_TX_BOTH
#define DUAL_SX1262_LOCAL_TX_BOTH 1
#endif

#ifndef DUAL_SX1262_FORWARD_TX_BOTH
#define DUAL_SX1262_FORWARD_TX_BOTH 1
#endif

volatile bool DualSX1262Wrapper::valley_flag = false;
volatile bool DualSX1262Wrapper::backhaul_flag = false;

void DualSX1262Wrapper::onValleyAction() {
  valley_flag = true;
}

void DualSX1262Wrapper::onBackhaulAction() {
  backhaul_flag = true;
}

DualSX1262Wrapper::DualSX1262Wrapper(CustomSX1262& valley,
                                     CustomSX1262& backhaul,
                                     mesh::MainBoard& board)
    : _board(&board),
      _slots{{&valley, "VALLEY", StateIdle, true, LORA_TX_POWER, 0, 0, 0, 0, 0, 0, 0, 0, 0},
             {&backhaul, "BACKHAUL", StateIdle, true, LORA_TX_POWER, 0, 0, 0, 0, 0, 0, 0, 0, 0}},
      _last_rssi(0),
      _last_snr(0),
      _last_rx_port(PortNone),
      _last_tx_port(PortNone),
      _n_recv(0),
      _n_recv_errors(0),
      _n_sent(0),
      _tx_len(0),
      _tx_port(PortNone),
      _tx_second_port(PortNone),
      _tx_waiting(false),
      _inter_tx_guard_ms(DUAL_SX1262_INTER_TX_GUARD_MS),
      _common_tx_power_dbm(LORA_TX_POWER),
      _common_power_pending(false),
      _threshold(0),
      _cad_enabled(false),
      _pending_rx_len(0),
      _pending_rx_port(PortNone),
      _pending_rx_rssi(0),
      _pending_rx_snr(0) {
  memset(_recent, 0, sizeof(_recent));
}

bool DualSX1262Wrapper::stdInit(SPIClass* spi) {
#if defined(P_LORA_SCLK)
  if (spi) {
    spi->begin(P_LORA_SCLK, P_LORA_MISO, P_LORA_MOSI);
  }
#endif

  pinMode(P_LORA_NSS, OUTPUT);
  digitalWrite(P_LORA_NSS, HIGH);
  pinMode(P_LORA2_NSS, OUTPUT);
  digitalWrite(P_LORA2_NSS, HIGH);
  delay(5);

  Serial.println("Dual SX1262 repeater mode");

  bool ok = true;
  ok = initOne(PortValley) && ok;
  ok = initOne(PortBackhaul) && ok;
  return ok;
}

bool DualSX1262Wrapper::initOne(Port port) {
#ifdef SX126X_DIO3_TCXO_VOLTAGE
  float tcxo = SX126X_DIO3_TCXO_VOLTAGE;
#else
  float tcxo = 1.6f;
#endif

#ifdef LORA_CR
  uint8_t cr = LORA_CR;
#else
  uint8_t cr = 5;
#endif

#ifdef SX126X_USE_REGULATOR_LDO
  constexpr bool useRegulatorLDO = SX126X_USE_REGULATOR_LDO;
#else
  constexpr bool useRegulatorLDO = false;
#endif

  CustomSX1262* radio = _slots[port].radio;
  int status = radio->begin(LORA_FREQ,
                            LORA_BW,
                            LORA_SF,
                            cr,
                            RADIOLIB_SX126X_SYNC_WORD_PRIVATE,
                            LORA_TX_POWER,
                            16,
                            tcxo,
                            useRegulatorLDO);
  if (status == RADIOLIB_ERR_SPI_CMD_FAILED || status == RADIOLIB_ERR_SPI_CMD_INVALID) {
    tcxo = 0.0f;
    status = radio->begin(LORA_FREQ,
                          LORA_BW,
                          LORA_SF,
                          cr,
                          RADIOLIB_SX126X_SYNC_WORD_PRIVATE,
                          LORA_TX_POWER,
                          16,
                          tcxo,
                          useRegulatorLDO);
  }
  if (status != RADIOLIB_ERR_NONE) {
    Serial.print("ERROR: ");
    Serial.print(_slots[port].name);
    Serial.print(" radio init failed: ");
    Serial.println(status);
    return false;
  }

  radio->setCRC(1);

#ifdef SX126X_CURRENT_LIMIT
  radio->setCurrentLimit(SX126X_CURRENT_LIMIT);
#endif
#ifdef SX126X_DIO2_AS_RF_SWITCH
  radio->setDio2AsRfSwitch(SX126X_DIO2_AS_RF_SWITCH);
#endif
#ifdef SX126X_RX_BOOSTED_GAIN
  radio->setRxBoostedGainMode(SX126X_RX_BOOSTED_GAIN);
#endif
#ifdef DUAL_SX1262_USE_RF_SWITCH_PINS
  #ifndef SX126X_RXEN
    #define SX126X_RXEN RADIOLIB_NC
  #endif
  #ifndef SX126X_TXEN
    #define SX126X_TXEN RADIOLIB_NC
  #endif
  radio->setRfSwitchPins(SX126X_RXEN, SX126X_TXEN);
#endif

#ifdef SX126X_REGISTER_PATCH
  uint8_t r_data = 0;
  radio->readRegister(0x8B5, &r_data, 1);
  r_data |= 0x01;
  radio->writeRegister(0x8B5, &r_data, 1);
#endif

  Serial.print(_slots[port].name);
  Serial.println(" radio init OK");

  return true;
}

void DualSX1262Wrapper::begin() {
  _slots[PortValley].radio->setPacketReceivedAction(onValleyAction);
  _slots[PortBackhaul].radio->setPacketReceivedAction(onBackhaulAction);

  _last_rx_port = PortNone;
  _last_tx_port = PortNone;
  _last_rssi = 0;
  _last_snr = 0;
  _threshold = 0;
  _cad_enabled = false;
  _pending_rx_len = 0;
  _pending_rx_port = PortNone;
  valley_flag = false;
  backhaul_flag = false;
  memset(_recent, 0, sizeof(_recent));

  for (int i = 0; i < 2; i++) {
    _slots[i].state = StateIdle;
    _slots[i].noise_floor = 0;
    _slots[i].num_floor_samples = 0;
    _slots[i].floor_sample_sum = 0;
    _slots[i].rx_count = 0;
    _slots[i].tx_count = 0;
    _slots[i].rx_errors = 0;
    _slots[i].duplicate_count = 0;
    _slots[i].last_rssi = 0;
    _slots[i].last_snr = 0;
  }

  updatePreamble(_slots[PortValley].radio->spreadingFactor);
#ifdef LORA_CR
  updateReceiveTimeouts(_slots[PortValley].radio->spreadingFactor, LORA_BW, LORA_CR);
#else
  updateReceiveTimeouts(_slots[PortValley].radio->spreadingFactor, LORA_BW, 5);
#endif
  startRecvAll();
}

void DualSX1262Wrapper::powerOff() {
  _slots[PortValley].radio->sleep(false);
  _slots[PortBackhaul].radio->sleep(false);
}

void DualSX1262Wrapper::startRecv(Port port) {
  if (port == PortNone || _tx_waiting || !_slots[port].enabled) {
    return;
  }

  RadioSlot& slot = _slots[port];
  if (slot.state == StateRx) {
    return;
  }

  int err = slot.radio->startReceive();
  if (err == RADIOLIB_ERR_NONE) {
    slot.state = StateRx;
  } else {
    MESH_DEBUG_PRINTLN("DualSX1262Wrapper: %s startReceive(%d)", slot.name, err);
  }
}

void DualSX1262Wrapper::startRecvAll() {
  startRecv(PortValley);
  startRecv(PortBackhaul);
}

void DualSX1262Wrapper::idleOne(Port port) {
  if (port == PortNone) {
    return;
  }
  _slots[port].radio->standby();
  _slots[port].state = StateIdle;
}

void DualSX1262Wrapper::idleAll() {
  idleOne(PortValley);
  idleOne(PortBackhaul);
}

bool DualSX1262Wrapper::isFlagSet(Port port) const {
  if (port == PortValley) return valley_flag;
  if (port == PortBackhaul) return backhaul_flag;
  return false;
}

void DualSX1262Wrapper::clearFlag(Port port) {
  if (port == PortValley) {
    valley_flag = false;
  } else if (port == PortBackhaul) {
    backhaul_flag = false;
  }
}

int DualSX1262Wrapper::recvRaw(uint8_t* bytes, int sz) {
  if (_tx_waiting) {
    return 0;
  }

  if (_pending_rx_len > 0) {
    int len = min(_pending_rx_len, sz);
    memcpy(bytes, _pending_rx, len);
    Port port = _pending_rx_port;
    float rssi = _pending_rx_rssi;
    float snr = _pending_rx_snr;
    _pending_rx_len = 0;
    _pending_rx_port = PortNone;
    return acceptFrame(port, bytes, len, rssi, snr);
  }

  bool valley_ready = isFlagSet(PortValley) && _slots[PortValley].enabled;
  bool backhaul_ready = isFlagSet(PortBackhaul) && _slots[PortBackhaul].enabled;

  int valley_len = 0;
  int backhaul_len = 0;
  float valley_rssi = 0;
  float valley_snr = 0;
  float backhaul_rssi = 0;
  float backhaul_snr = 0;

  bool valley_ok = valley_ready && readFrame(PortValley, bytes, sz, valley_len, valley_rssi, valley_snr);
  bool backhaul_ok = backhaul_ready && readFrame(PortBackhaul, _scratch_rx, sizeof(_scratch_rx),
                                                  backhaul_len, backhaul_rssi, backhaul_snr);

  if (valley_ok && backhaul_ok) {
    uint32_t valley_signature = computeSignature(bytes, valley_len);
    uint32_t backhaul_signature = computeSignature(_scratch_rx, backhaul_len);
    if (valley_signature == backhaul_signature) {
      bool prefer_backhaul = backhaul_snr > valley_snr ||
                             (backhaul_snr == valley_snr && backhaul_rssi > valley_rssi);
      if (prefer_backhaul) {
        _slots[PortValley].duplicate_count++;
        int len = min(backhaul_len, sz);
        memcpy(bytes, _scratch_rx, len);
        return acceptFrame(PortBackhaul, bytes, len, backhaul_rssi, backhaul_snr);
      }

      _slots[PortBackhaul].duplicate_count++;
      return acceptFrame(PortValley, bytes, valley_len, valley_rssi, valley_snr);
    }

    storePending(PortBackhaul, _scratch_rx, backhaul_len, backhaul_rssi, backhaul_snr);
    return acceptFrame(PortValley, bytes, valley_len, valley_rssi, valley_snr);
  }

  if (valley_ok) {
    return acceptFrame(PortValley, bytes, valley_len, valley_rssi, valley_snr);
  }

  if (backhaul_ok) {
    int len = min(backhaul_len, sz);
    memcpy(bytes, _scratch_rx, len);
    return acceptFrame(PortBackhaul, bytes, len, backhaul_rssi, backhaul_snr);
  }

  startRecvAll();
  return 0;
}

bool DualSX1262Wrapper::readFrame(Port port, uint8_t* bytes, int sz, int& len, float& rssi, float& snr) {
  if (!isFlagSet(port) || !_slots[port].enabled) {
    return false;
  }

  clearFlag(port);

  RadioSlot& slot = _slots[port];
  len = slot.radio->getPacketLength();
  if (len <= 0) {
    slot.state = StateIdle;
    startRecv(port);
    return false;
  }

  if (len > sz) {
    len = sz;
  }

  int err = slot.radio->readData(bytes, len);
  slot.state = StateIdle;
  if (err != RADIOLIB_ERR_NONE) {
    MESH_DEBUG_PRINTLN("DualSX1262Wrapper: %s readData(%d)", slot.name, err);
    _n_recv_errors++;
    slot.rx_errors++;
    startRecv(port);
    return false;
  }

  rssi = slot.radio->getRSSI();
  snr = slot.radio->getSNR();
  slot.rx_count++;
  slot.last_rssi = rssi;
  slot.last_snr = snr;

  startRecv(port);
  return true;
}

int DualSX1262Wrapper::acceptFrame(Port port, uint8_t* bytes, int len, float rssi, float snr) {
  uint32_t signature = computeSignature(bytes, len);
  if (isRecentDuplicate(signature)) {
    _slots[port].duplicate_count++;
    return 0;
  }

  rememberIngress(signature, port);
  _last_rssi = rssi;
  _last_snr = snr;
  _last_rx_port = port;
  _n_recv++;
  return len;
}

void DualSX1262Wrapper::storePending(Port port, const uint8_t* bytes, int len, float rssi, float snr) {
  _pending_rx_len = min(len, (int)sizeof(_pending_rx));
  memcpy(_pending_rx, bytes, _pending_rx_len);
  _pending_rx_port = port;
  _pending_rx_rssi = rssi;
  _pending_rx_snr = snr;
}

uint32_t DualSX1262Wrapper::getEstAirtimeFor(int len_bytes) {
  uint32_t airtime_ms = _slots[PortValley].radio->getTimeOnAir(len_bytes) / 1000;
#if DUAL_SX1262_LOCAL_TX_BOTH || DUAL_SX1262_FORWARD_TX_BOTH
  if (_slots[PortValley].enabled && _slots[PortBackhaul].enabled) {
    return (airtime_ms * 2) + _inter_tx_guard_ms;
  }
  return airtime_ms;
#else
  return airtime_ms;
#endif
}

bool DualSX1262Wrapper::startSendRaw(const uint8_t* bytes, int len) {
  if (len <= 0 || len > MAX_TRANS_UNIT || _tx_waiting) {
    return false;
  }

  memcpy(_tx_buf, bytes, len);
  _tx_len = len;
  _tx_second_port = PortNone;
  _tx_port = chooseTxPort(bytes, len, &_tx_second_port);
  if (_tx_port == PortNone) {
    return false;
  }

  _board->onBeforeTransmit();
  idleAll();
  delay(_inter_tx_guard_ms);

  if (!startTxOn(_tx_port)) {
    _board->onAfterTransmit();
    startRecvAll();
    return false;
  }

  _tx_waiting = true;
  return true;
}

bool DualSX1262Wrapper::startTxOn(Port port) {
  if (port == PortNone || !_slots[port].enabled) {
    return false;
  }
  clearFlag(port);
  int err = _slots[port].radio->startTransmit(_tx_buf, _tx_len);
  if (err == RADIOLIB_ERR_NONE) {
    _slots[port].state = StateTxWait;
    _last_tx_port = port;
    return true;
  }

  MESH_DEBUG_PRINTLN("DualSX1262Wrapper: %s startTransmit(%d)", _slots[port].name, err);
  _slots[port].state = StateIdle;
  return false;
}

bool DualSX1262Wrapper::isSendComplete() {
  if (!_tx_waiting || _tx_port == PortNone) {
    return false;
  }

  if (!isFlagSet(_tx_port)) {
    return false;
  }

  clearFlag(_tx_port);

  if (_tx_second_port != PortNone) {
    _slots[_tx_port].radio->finishTransmit();
    _slots[_tx_port].state = StateIdle;
    _slots[_tx_port].tx_count++;
    _n_sent++;

    Port next = _tx_second_port;
    _tx_second_port = PortNone;
    _tx_port = next;
    delay(_inter_tx_guard_ms);

    if (startTxOn(next)) {
      return false;
    }

    _tx_port = PortNone;
    return true;
  }

  _slots[_tx_port].tx_count++;
  _n_sent++;
  return true;
}

void DualSX1262Wrapper::onSendFinished() {
  if (_tx_port != PortNone) {
    _slots[_tx_port].radio->finishTransmit();
    _slots[_tx_port].state = StateIdle;
  }
  _tx_waiting = false;
  _tx_port = PortNone;
  _tx_second_port = PortNone;
  _board->onAfterTransmit();
  if (_common_power_pending) {
    _common_power_pending = false;
    applyPortPower(PortValley, _slots[PortValley].tx_power_dbm);
    applyPortPower(PortBackhaul, _slots[PortBackhaul].tx_power_dbm);
  }
  startRecvAll();
}

void DualSX1262Wrapper::loop() {
  if (_tx_waiting) {
    return;
  }

  if (_slots[PortValley].enabled) sampleNoiseFloor(_slots[PortValley]);
  if (_slots[PortBackhaul].enabled) sampleNoiseFloor(_slots[PortBackhaul]);
}

void DualSX1262Wrapper::sampleNoiseFloor(RadioSlot& slot) {
  if (slot.state == StateRx && slot.num_floor_samples < NUM_NOISE_FLOOR_SAMPLES) {
    if (!slot.radio->isReceiving()) {
      int rssi = (int)slot.radio->getRSSI(false);
      if (slot.noise_floor == 0 || rssi < slot.noise_floor + SAMPLING_THRESHOLD) {
        slot.num_floor_samples++;
        slot.floor_sample_sum += rssi;
      }
    }
  } else if (slot.num_floor_samples >= NUM_NOISE_FLOOR_SAMPLES && slot.floor_sample_sum != 0) {
    slot.noise_floor = slot.floor_sample_sum / NUM_NOISE_FLOOR_SAMPLES;
    if (slot.noise_floor < -120) {
      slot.noise_floor = -120;
    }
    slot.floor_sample_sum = 0;
    MESH_DEBUG_PRINTLN("DualSX1262Wrapper: %s noise_floor = %d", slot.name, (int)slot.noise_floor);
  }
}

int DualSX1262Wrapper::getNoiseFloor() const {
  int a = _slots[PortValley].enabled ? _slots[PortValley].noise_floor : 0;
  int b = _slots[PortBackhaul].enabled ? _slots[PortBackhaul].noise_floor : 0;
  if (a == 0) return b;
  if (b == 0) return a;
  return (a + b) / 2;
}

void DualSX1262Wrapper::triggerNoiseFloorCalibrate(int threshold) {
  _threshold = threshold;
  for (int i = 0; i < 2; i++) {
    if (_slots[i].num_floor_samples >= NUM_NOISE_FLOOR_SAMPLES) {
      _slots[i].num_floor_samples = 0;
      _slots[i].floor_sample_sum = 0;
    }
  }
}

void DualSX1262Wrapper::resetAGC() {
  if (_tx_waiting || isFlagSet(PortValley) || isFlagSet(PortBackhaul) ||
      isReceivingPacket(PortValley) || isReceivingPacket(PortBackhaul)) {
    return;
  }

  if (_slots[PortValley].enabled) sx126xResetAGC((SX126x*)_slots[PortValley].radio);
  if (_slots[PortBackhaul].enabled) sx126xResetAGC((SX126x*)_slots[PortBackhaul].radio);

  for (int i = 0; i < 2; i++) {
    _slots[i].state = StateIdle;
    _slots[i].noise_floor = 0;
    _slots[i].num_floor_samples = 0;
    _slots[i].floor_sample_sum = 0;
  }
  startRecvAll();
}

bool DualSX1262Wrapper::isInRecvMode() const {
  if (_tx_waiting) return false;
  for (int i = 0; i < 2; i++) {
    if (_slots[i].enabled && _slots[i].state != StateRx) return false;
  }
  return _slots[PortValley].enabled || _slots[PortBackhaul].enabled;
}

bool DualSX1262Wrapper::isReceivingPacket(Port port) const {
  return _slots[port].enabled && _slots[port].radio->isReceiving();
}

bool DualSX1262Wrapper::isChannelActive(Port port) {
  RadioSlot& slot = _slots[port];
  if (!slot.enabled) {
    return false;
  }

  if (_threshold != 0 && slot.noise_floor != 0 &&
      slot.radio->getRSSI(false) > slot.noise_floor + _threshold) {
    return true;
  }

  if (_cad_enabled) {
    int16_t result = slot.radio->scanChannel();
    clearFlag(port);
    slot.state = StateIdle;
    startRecv(port);
    return result != RADIOLIB_CHANNEL_FREE;
  }

  return false;
}

bool DualSX1262Wrapper::isReceiving() {
  return isReceivingPacket(PortValley) ||
         isReceivingPacket(PortBackhaul) ||
         isChannelActive(PortValley) ||
         isChannelActive(PortBackhaul);
}

void DualSX1262Wrapper::setParams(float freq, float bw, uint8_t sf, uint8_t cr) {
  for (int i = 0; i < 2; i++) {
    _slots[i].radio->setFrequency(freq);
    _slots[i].radio->setSpreadingFactor(sf);
    _slots[i].radio->setBandwidth(bw);
    _slots[i].radio->setCodingRate(cr);
  }
  updatePreamble(sf);
  updateReceiveTimeouts(sf, bw, cr);
}

void DualSX1262Wrapper::updatePreamble(uint8_t sf) {
  uint16_t preamble = preambleLengthForSF(sf);
  _slots[PortValley].radio->setPreambleLength(preamble);
  _slots[PortBackhaul].radio->setPreambleLength(preamble);
}

void DualSX1262Wrapper::updateReceiveTimeouts(uint8_t sf, float bw, uint8_t cr) {
  const uint8_t preamble_symbols = preambleLengthForSF(sf);
  const uint32_t symbol_us = ((uint32_t)10000 << sf) / (bw * 10);
  const uint32_t sf_coeff_x4 = (sf == 5 || sf == 6) ? 25 : 17;
  const uint32_t preamble_us =
      (((preamble_symbols + 8) * 4 + sf_coeff_x4) * symbol_us) / 4;

  uint32_t total_us = _slots[PortValley].radio->getTimeOnAir(MAX_TRANS_UNIT);
  uint32_t payload_us = total_us > preamble_us ? total_us - preamble_us
                                               : 4000000UL - preamble_us;
  if (cr >= 5 && cr < 8) {
    payload_us = (payload_us * 8) / cr;
  }

  const uint32_t preamble_ms = (preamble_us + 999) / 1000;
  const uint32_t payload_ms = (payload_us + 999) / 1000;
  for (int i = 0; i < 2; i++) {
    _slots[i].radio->setPreambleMillis(preamble_ms);
    _slots[i].radio->setMaxPayloadMillis(payload_ms);
  }
}

uint32_t DualSX1262Wrapper::getRngSeed() {
  return _slots[PortValley].radio->random(0x7FFFFFFF) ^
         (_slots[PortBackhaul].radio->random(0x7FFFFFFF) << 1) ^
         micros();
}

void DualSX1262Wrapper::setTxPower(int8_t dbm) {
  _common_tx_power_dbm = dbm;
  if (_tx_waiting) {
    _slots[PortValley].tx_power_dbm = dbm;
    _slots[PortBackhaul].tx_power_dbm = dbm;
    _common_power_pending = true;
    return;
  }
  applyPortPower(PortValley, dbm);
  applyPortPower(PortBackhaul, dbm);
}

void DualSX1262Wrapper::setCommonTxPower(int8_t dbm) {
  setTxPower(dbm);
  saveConfig();
}

bool DualSX1262Wrapper::applyPortPower(Port port, int8_t dbm) {
  if (port == PortNone || _tx_waiting) return false;

  bool resume_rx = _slots[port].enabled;
  idleOne(port);
  int status = _slots[port].radio->setOutputPower(dbm);
  if (status == RADIOLIB_ERR_NONE) {
    _slots[port].tx_power_dbm = dbm;
  }
  if (resume_rx) startRecv(port);
  return status == RADIOLIB_ERR_NONE;
}

bool DualSX1262Wrapper::setPortTxPower(Port port, int8_t dbm) {
  if (dbm < -9 || dbm > 22 || !applyPortPower(port, dbm)) return false;
  saveConfig();
  return true;
}

bool DualSX1262Wrapper::setPortEnabled(Port port, bool enabled) {
  if (port == PortNone || _tx_waiting) return false;
  Port other = port == PortValley ? PortBackhaul : PortValley;
  if (!enabled && !_slots[other].enabled) return false;

  _slots[port].enabled = enabled;
  clearFlag(port);
  if (enabled) {
    applyPortPower(port, _slots[port].tx_power_dbm);
    startRecv(port);
  } else {
    idleOne(port);
  }
  saveConfig();
  return true;
}

void DualSX1262Wrapper::loadConfig() {
  Preferences prefs;
  if (prefs.begin("dual-sx1262", false)) {
    _slots[PortValley].enabled = prefs.getBool("ven", true);
    _slots[PortBackhaul].enabled = prefs.getBool("ben", true);

    int valley_power = prefs.getChar("vtx", _slots[PortValley].tx_power_dbm);
    int backhaul_power = prefs.getChar("btx", _slots[PortBackhaul].tx_power_dbm);
    if (valley_power >= -9 && valley_power <= 22) {
      _slots[PortValley].tx_power_dbm = valley_power;
    }
    if (backhaul_power >= -9 && backhaul_power <= 22) {
      _slots[PortBackhaul].tx_power_dbm = backhaul_power;
    }

    uint16_t guard = prefs.getUShort("guard", DUAL_SX1262_INTER_TX_GUARD_MS);
    if (guard >= 10 && guard <= 1000) _inter_tx_guard_ms = guard;
    prefs.end();
  }

  if (!_slots[PortValley].enabled && !_slots[PortBackhaul].enabled) {
    _slots[PortValley].enabled = true;
  }

  applyPortPower(PortValley, _slots[PortValley].tx_power_dbm);
  applyPortPower(PortBackhaul, _slots[PortBackhaul].tx_power_dbm);
  if (!_slots[PortValley].enabled) idleOne(PortValley);
  if (!_slots[PortBackhaul].enabled) idleOne(PortBackhaul);
  startRecvAll();
}

void DualSX1262Wrapper::saveConfig() {
  Preferences prefs;
  if (!prefs.begin("dual-sx1262", false)) return;
  prefs.putBool("ven", _slots[PortValley].enabled);
  prefs.putBool("ben", _slots[PortBackhaul].enabled);
  prefs.putChar("vtx", _slots[PortValley].tx_power_dbm);
  prefs.putChar("btx", _slots[PortBackhaul].tx_power_dbm);
  prefs.putUShort("guard", _inter_tx_guard_ms);
  prefs.end();
}

void DualSX1262Wrapper::resetConfig() {
  _inter_tx_guard_ms = DUAL_SX1262_INTER_TX_GUARD_MS;
  _slots[PortValley].enabled = true;
  _slots[PortBackhaul].enabled = true;
  applyPortPower(PortValley, _common_tx_power_dbm);
  applyPortPower(PortBackhaul, _common_tx_power_dbm);
  startRecvAll();
  saveConfig();
}

DualSX1262Wrapper::Port DualSX1262Wrapper::parsePort(const char* value) {
  if (strcmp(value, "valley") == 0) return PortValley;
  if (strcmp(value, "backhaul") == 0) return PortBackhaul;
  return PortNone;
}

bool DualSX1262Wrapper::parseInt(const char* value, int& result) {
  if (!value || !*value) return false;
  char* end = nullptr;
  long parsed = strtol(value, &end, 10);
  if (*end != 0) return false;
  result = (int)parsed;
  return true;
}

bool DualSX1262Wrapper::parseBool(const char* value, bool& result) {
  if (strcmp(value, "1") == 0 || strcmp(value, "on") == 0 || strcmp(value, "true") == 0) {
    result = true;
    return true;
  }
  if (strcmp(value, "0") == 0 || strcmp(value, "off") == 0 || strcmp(value, "false") == 0) {
    result = false;
    return true;
  }
  return false;
}

void DualSX1262Wrapper::formatPortConfig(Port port, char* reply, size_t reply_size) const {
  const RadioSlot& slot = _slots[port];
  snprintf(reply, reply_size, "{\"port\":\"%s\",\"enabled\":%d,\"tx_dbm\":%d}",
           slot.name, slot.enabled ? 1 : 0, slot.tx_power_dbm);
}

void DualSX1262Wrapper::formatPortStats(Port port, char* reply, size_t reply_size) const {
  const RadioSlot& slot = _slots[port];
  snprintf(reply, reply_size,
           "{\"port\":\"%s\",\"rx\":%lu,\"tx\":%lu,\"dup\":%lu,\"errors\":%lu,\"rssi\":%d,\"snr\":%.1f}",
           slot.name, (unsigned long)slot.rx_count, (unsigned long)slot.tx_count,
           (unsigned long)slot.duplicate_count, (unsigned long)slot.rx_errors,
           (int)slot.last_rssi, slot.last_snr);
}

bool DualSX1262Wrapper::handleCommand(const char* command, char* reply, size_t reply_size) {
  if (!command || !reply || reply_size == 0) return false;

  if (strcmp(command, "dualradio help") == 0) {
    snprintf(reply, reply_size,
             "get dualradio|valley|backhaul; stats valley|backhaul; set <port> tx|enabled <v>; set dualradio guard <ms>");
    return true;
  }
  if (strcmp(command, "clear dualradio.stats") == 0) {
    resetStats();
    snprintf(reply, reply_size, "OK - dual-radio stats reset");
    return true;
  }
  if (strcmp(command, "reset dualradio") == 0) {
    if (_tx_waiting) {
      snprintf(reply, reply_size, "Error, radio busy");
    } else {
      resetConfig();
      snprintf(reply, reply_size, "OK - dual-radio defaults restored");
    }
    return true;
  }

  char op[16] = {0};
  char target[16] = {0};
  char field[16] = {0};
  char value[16] = {0};
  int count = sscanf(command, "%15s %15s %15s %15s", op, target, field, value);

  if (count == 2 && strcmp(op, "get") == 0 && strcmp(target, "dualradio") == 0) {
    snprintf(reply, reply_size,
             "{\"valley\":{\"enabled\":%d,\"tx\":%d},\"backhaul\":{\"enabled\":%d,\"tx\":%d},\"guard_ms\":%u}",
             _slots[PortValley].enabled ? 1 : 0, _slots[PortValley].tx_power_dbm,
             _slots[PortBackhaul].enabled ? 1 : 0, _slots[PortBackhaul].tx_power_dbm,
             _inter_tx_guard_ms);
    return true;
  }

  Port port = parsePort(target);
  if (count == 2 && strcmp(op, "get") == 0 && port != PortNone) {
    formatPortConfig(port, reply, reply_size);
    return true;
  }
  if (count == 2 && strcmp(op, "stats") == 0 && port != PortNone) {
    formatPortStats(port, reply, reply_size);
    return true;
  }

  if (count == 4 && strcmp(op, "set") == 0 && strcmp(target, "dualradio") == 0 &&
      strcmp(field, "guard") == 0) {
    int guard = 0;
    if (!parseInt(value, guard) || guard < 10 || guard > 1000) {
      snprintf(reply, reply_size, "Error, guard must be 10-1000 ms");
    } else if (_tx_waiting) {
      snprintf(reply, reply_size, "Error, radio busy");
    } else {
      _inter_tx_guard_ms = (uint16_t)guard;
      saveConfig();
      snprintf(reply, reply_size, "OK - guard=%d ms", guard);
    }
    return true;
  }

  if (count == 4 && strcmp(op, "set") == 0 && port != PortNone && strcmp(field, "tx") == 0) {
    int power = 0;
    if (!parseInt(value, power) || power < -9 || power > 22) {
      snprintf(reply, reply_size, "Error, tx must be -9 to 22 dBm");
    } else if (!setPortTxPower(port, (int8_t)power)) {
      snprintf(reply, reply_size, "Error, radio busy or power rejected");
    } else {
      snprintf(reply, reply_size, "OK - %s tx=%d dBm", _slots[port].name, power);
    }
    return true;
  }

  if (count == 4 && strcmp(op, "set") == 0 && port != PortNone && strcmp(field, "enabled") == 0) {
    bool enabled = false;
    if (!parseBool(value, enabled)) {
      snprintf(reply, reply_size, "Error, enabled must be on/off or 1/0");
    } else if (!setPortEnabled(port, enabled)) {
      snprintf(reply, reply_size, "Error, radio busy or last port cannot be disabled");
    } else {
      snprintf(reply, reply_size, "OK - %s enabled=%d", _slots[port].name, enabled ? 1 : 0);
    }
    return true;
  }

  return false;
}

void DualSX1262Wrapper::resetStats() {
  _n_recv = 0;
  _n_recv_errors = 0;
  _n_sent = 0;
  for (int i = 0; i < 2; i++) {
    _slots[i].rx_count = 0;
    _slots[i].tx_count = 0;
    _slots[i].rx_errors = 0;
    _slots[i].duplicate_count = 0;
  }
}

bool DualSX1262Wrapper::setRxBoostedGainMode(bool en) {
  bool valley_ok = _slots[PortValley].radio->setRxBoostedGainMode(en) == RADIOLIB_ERR_NONE;
  bool backhaul_ok = _slots[PortBackhaul].radio->setRxBoostedGainMode(en) == RADIOLIB_ERR_NONE;
  return valley_ok && backhaul_ok;
}

bool DualSX1262Wrapper::getRxBoostedGainMode() const {
  return _slots[PortValley].radio->getRxBoostedGainMode() &&
         _slots[PortBackhaul].radio->getRxBoostedGainMode();
}

uint32_t DualSX1262Wrapper::computeSignature(const uint8_t* bytes, int len) const {
  const uint32_t fnv_offset = 2166136261UL;
  const uint32_t fnv_prime = 16777619UL;
  uint32_t h = fnv_offset;

  auto mix = [&](uint8_t v) {
    h ^= v;
    h *= fnv_prime;
  };

  if (len <= 0) {
    return h;
  }

  int i = 0;
  uint8_t header = bytes[i++];
  mix(header & ~PH_ROUTE_MASK);
  mix(header & PH_ROUTE_MASK);

  uint8_t route = header & PH_ROUTE_MASK;
  if (route == ROUTE_TYPE_TRANSPORT_FLOOD || route == ROUTE_TYPE_TRANSPORT_DIRECT) {
    for (int j = 0; j < 4 && i < len; j++) {
      mix(bytes[i++]);
    }
  }

  if (i >= len) {
    return h;
  }

  uint8_t path_len = bytes[i++];
  uint8_t hash_size = (path_len >> 6) + 1;
  uint8_t hash_count = path_len & 63;
  int path_bytes = hash_size * hash_count;
  if (path_bytes < 0 || i + path_bytes > len) {
    path_bytes = 0;
  }
  i += path_bytes;

  for (; i < len; i++) {
    mix(bytes[i]);
  }
  return h;
}

bool DualSX1262Wrapper::isRecentDuplicate(uint32_t signature) const {
  uint32_t now = millis();
  for (const auto& entry : _recent) {
    if (entry.valid && entry.signature == signature &&
        (uint32_t)(now - entry.seen_ms) < DUAL_SX1262_DUP_WINDOW_MS) {
      return true;
    }
  }
  return false;
}

void DualSX1262Wrapper::rememberIngress(uint32_t signature, Port port) {
  uint32_t now = millis();

  for (auto& entry : _recent) {
    if (entry.valid && entry.signature == signature) {
      entry.seen_ms = now;
      entry.port = port;
      return;
    }
  }

  int replace = 0;
  uint32_t oldest_age = 0;
  for (int i = 0; i < (int)(sizeof(_recent) / sizeof(_recent[0])); i++) {
    if (!_recent[i].valid) {
      replace = i;
      break;
    }
    uint32_t age = now - _recent[i].seen_ms;
    if (age >= oldest_age) {
      oldest_age = age;
      replace = i;
    }
  }

  _recent[replace].signature = signature;
  _recent[replace].seen_ms = now;
  _recent[replace].port = port;
  _recent[replace].valid = true;
}

DualSX1262Wrapper::Port DualSX1262Wrapper::findIngress(uint32_t signature) const {
  uint32_t now = millis();
  for (const auto& entry : _recent) {
    if (entry.valid && entry.signature == signature &&
        (uint32_t)(now - entry.seen_ms) < DUAL_SX1262_INGRESS_WINDOW_MS) {
      return entry.port;
    }
  }
  return PortNone;
}

DualSX1262Wrapper::Port DualSX1262Wrapper::chooseTxPort(const uint8_t* bytes, int len, Port* second_port) {
  *second_port = PortNone;

  Port ingress = findIngress(computeSignature(bytes, len));
#if DUAL_SX1262_FORWARD_TX_BOTH
  if (_slots[PortValley].enabled && _slots[PortBackhaul].enabled) {
    if (ingress == PortValley) {
      *second_port = PortValley;
      return PortBackhaul;
    }
    *second_port = PortBackhaul;
    return PortValley;
  }
  if (_slots[PortValley].enabled) return PortValley;
  if (_slots[PortBackhaul].enabled) return PortBackhaul;
  return PortNone;
#else
  if (ingress == PortValley) {
    return _slots[PortBackhaul].enabled ? PortBackhaul : PortNone;
  }
  if (ingress == PortBackhaul) {
    return _slots[PortValley].enabled ? PortValley : PortNone;
  }

#if DUAL_SX1262_LOCAL_TX_BOTH
  if (_slots[PortValley].enabled && _slots[PortBackhaul].enabled) {
    *second_port = PortBackhaul;
    return PortValley;
  }
  if (_slots[PortValley].enabled) return PortValley;
  if (_slots[PortBackhaul].enabled) return PortBackhaul;
  return PortNone;
#else
  if (_slots[PortBackhaul].enabled) return PortBackhaul;
  if (_slots[PortValley].enabled) return PortValley;
  return PortNone;
#endif
#endif
}

static float snr_threshold_for_sf(uint8_t sf) {
  switch (sf) {
    case 7: return -7.5f;
    case 8: return -10.0f;
    case 9: return -12.5f;
    case 10: return -15.0f;
    case 11: return -17.5f;
    case 12: return -20.0f;
    default: return 0.0f;
  }
}

float DualSX1262Wrapper::packetScoreInt(float snr, int sf, int packet_len) {
  if (sf < 7 || sf > 12) {
    return 0.0f;
  }

  float threshold = snr_threshold_for_sf(sf);
  if (snr < threshold) {
    return 0.0f;
  }

  float success_rate = (snr - threshold) / 10.0f;
  float collision_penalty = 1.0f - (packet_len / 256.0f);
  float score = success_rate * collision_penalty;
  if (score < 0.0f) return 0.0f;
  if (score > 1.0f) return 1.0f;
  return score;
}

float DualSX1262Wrapper::packetScore(float snr, int packet_len) {
  return packetScoreInt(snr, _slots[PortValley].radio->spreadingFactor, packet_len);
}
