#include "MyMesh.h"
#include <algorithm>
#if defined(P1_POWER_ALERTS)
#include <base64.hpp>
#endif

/* ------------------------------ Config -------------------------------- */

#ifndef LORA_FREQ
  #define LORA_FREQ 915.0
#endif
#ifndef LORA_BW
  #define LORA_BW 250
#endif
#ifndef LORA_SF
  #define LORA_SF 10
#endif
#ifndef LORA_CR
  #define LORA_CR 5
#endif
#ifndef LORA_TX_POWER
  #define LORA_TX_POWER 20
#endif

#ifndef ADVERT_NAME
  #define ADVERT_NAME "repeater"
#endif
#ifndef ADVERT_LAT
  #define ADVERT_LAT 0.0
#endif
#ifndef ADVERT_LON
  #define ADVERT_LON 0.0
#endif

#ifndef ADMIN_PASSWORD
  #define ADMIN_PASSWORD "password"
#endif

#ifndef SERVER_RESPONSE_DELAY
  #define SERVER_RESPONSE_DELAY 300
#endif

#ifndef TXT_ACK_DELAY
  #define TXT_ACK_DELAY 200
#endif

#define FIRMWARE_VER_LEVEL       2

#define REQ_TYPE_GET_STATUS         0x01 // same as _GET_STATS
#define REQ_TYPE_KEEP_ALIVE         0x02
#define REQ_TYPE_GET_TELEMETRY_DATA 0x03
#define REQ_TYPE_GET_ACCESS_LIST    0x05
#define REQ_TYPE_GET_NEIGHBOURS     0x06
#define REQ_TYPE_GET_OWNER_INFO     0x07     // FIRMWARE_VER_LEVEL >= 2

#define RESP_SERVER_LOGIN_OK        0 // response to ANON_REQ

#define ANON_REQ_TYPE_REGIONS      0x01
#define ANON_REQ_TYPE_OWNER        0x02
#define ANON_REQ_TYPE_BASIC        0x03   // just remote clock

#define CLI_REPLY_DELAY_MILLIS      600

#define LAZY_CONTACTS_WRITE_DELAY    5000

#if defined(P1_POWER_ALERTS)
// Standard MeshCore public channel.  It is deliberately selected only by an
// explicit CLI command so a new installation cannot accidentally spam it.
static constexpr char P1_PUBLIC_GROUP_PSK[] = "izOH6cXN6mrJ5e26oRXNcg==";
#endif

void MyMesh::putNeighbour(const mesh::Identity &id, uint32_t timestamp, float snr) {
#if MAX_NEIGHBOURS // check if neighbours enabled
  // find existing neighbour, else use least recently updated
  uint32_t oldest_timestamp = 0xFFFFFFFF;
  NeighbourInfo *neighbour = &neighbours[0];
  for (int i = 0; i < MAX_NEIGHBOURS; i++) {
    // if neighbour already known, we should update it
    if (id.matches(neighbours[i].id)) {
      neighbour = &neighbours[i];
      break;
    }

    // otherwise we should update the least recently updated neighbour
    if (neighbours[i].heard_timestamp < oldest_timestamp) {
      neighbour = &neighbours[i];
      oldest_timestamp = neighbour->heard_timestamp;
    }
  }

  // update neighbour info
  neighbour->id = id;
  neighbour->advert_timestamp = timestamp;
  neighbour->heard_timestamp = getRTCClock()->getCurrentTime();
  neighbour->snr = (int8_t)(snr * 4);
#endif
}

uint8_t MyMesh::handleLoginReq(const mesh::Identity& sender, const uint8_t* secret, uint32_t sender_timestamp, const uint8_t* data, bool is_flood) {
  ClientInfo* client = NULL;
  if (data[0] == 0) {   // blank password, just check if sender is in ACL
    client = acl.getClient(sender.pub_key, PUB_KEY_SIZE);
    if (client == NULL) {
    #if MESH_DEBUG
      MESH_DEBUG_PRINTLN("Login, sender not in ACL");
    #endif
    }
  }
  if (client == NULL) {
    uint8_t perms;
    if (strcmp((char *)data, _prefs.password) == 0) { // check for valid admin password
      perms = PERM_ACL_ADMIN;
    } else if (strcmp((char *)data, _prefs.guest_password) == 0) { // check guest password
      perms = PERM_ACL_GUEST;
    } else {
#if MESH_DEBUG
      MESH_DEBUG_PRINTLN("Invalid password: %s", data);
#endif
      return 0;
    }

    client = acl.putClient(sender, 0);  // add to contacts (if not already known)
    if (sender_timestamp <= client->last_timestamp) {
      MESH_DEBUG_PRINTLN("Possible login replay attack!");
      return 0;  // FATAL: client table is full -OR- replay attack
    }

    MESH_DEBUG_PRINTLN("Login success!");
    client->last_timestamp = sender_timestamp;
    client->last_activity = getRTCClock()->getCurrentTime();
    client->permissions &= ~0x03;
    client->permissions |= perms;
    memcpy(client->shared_secret, secret, PUB_KEY_SIZE);

    if (perms != PERM_ACL_GUEST) {   // keep number of FS writes to a minimum
      dirty_contacts_expiry = futureMillis(LAZY_CONTACTS_WRITE_DELAY);
    }
  }

  if (is_flood) {
    client->out_path_len = OUT_PATH_UNKNOWN;  // need to rediscover out_path
  }

  uint32_t now = getRTCClock()->getCurrentTimeUnique();
  memcpy(reply_data, &now, 4);   // response packets always prefixed with timestamp
  reply_data[4] = RESP_SERVER_LOGIN_OK;
  reply_data[5] = 0;  // Legacy: was recommended keep-alive interval (secs / 16)
  reply_data[6] = client->isAdmin() ? 1 : 0;
  reply_data[7] = client->permissions;
  getRNG()->random(&reply_data[8], 4);   // random blob to help packet-hash uniqueness
  reply_data[12] = FIRMWARE_VER_LEVEL;  // New field

  return 13;  // reply length
}

uint8_t MyMesh::handleAnonRegionsReq(const mesh::Identity& sender, uint32_t sender_timestamp, const uint8_t* data) {
  if (anon_limiter.allow(rtc_clock.getCurrentTime())) {
    // request data has: {reply-path-len}{reply-path}
    reply_path_len = *data++;
    if (!mesh::Packet::isValidPathLen(reply_path_len)) return 0;  // reject - bad encoding

    mesh::Packet::writePath(reply_path, data, reply_path_len);
    // data += (uint8_t)reply_path_len * reply_path_hash_size;

    memcpy(reply_data, &sender_timestamp, 4);   // prefix with sender_timestamp, like a tag
    uint32_t now = getRTCClock()->getCurrentTime();
    memcpy(&reply_data[4], &now, 4);     // include our clock (for easy clock sync, and packet hash uniqueness)

    return 8 + region_map.exportNamesTo((char *) &reply_data[8], sizeof(reply_data) - 12, REGION_DENY_FLOOD);   // reply length
  }
  return 0;
}

uint8_t MyMesh::handleAnonOwnerReq(const mesh::Identity& sender, uint32_t sender_timestamp, const uint8_t* data) {
  if (anon_limiter.allow(rtc_clock.getCurrentTime())) {
    // request data has: {reply-path-len}{reply-path}
    reply_path_len = *data++;
    if (!mesh::Packet::isValidPathLen(reply_path_len)) return 0;  // reject - bad encoding

    mesh::Packet::writePath(reply_path, data, reply_path_len);
    // data += (uint8_t)reply_path_len * reply_path_hash_size;

    memcpy(reply_data, &sender_timestamp, 4);   // prefix with sender_timestamp, like a tag
    uint32_t now = getRTCClock()->getCurrentTime();
    memcpy(&reply_data[4], &now, 4);     // include our clock (for easy clock sync, and packet hash uniqueness)
    sprintf((char *) &reply_data[8], "%s\n%s", _prefs.node_name, _prefs.owner_info);

    return 8 + strlen((char *) &reply_data[8]);   // reply length
  }
  return 0;
}

uint8_t MyMesh::handleAnonClockReq(const mesh::Identity& sender, uint32_t sender_timestamp, const uint8_t* data) {
  if (anon_limiter.allow(rtc_clock.getCurrentTime())) {
    // request data has: {reply-path-len}{reply-path}
    reply_path_len = *data++;
    if (!mesh::Packet::isValidPathLen(reply_path_len)) return 0;  // reject - bad encoding

    mesh::Packet::writePath(reply_path, data, reply_path_len);
    // data += (uint8_t)reply_path_len * reply_path_hash_size;

    memcpy(reply_data, &sender_timestamp, 4);   // prefix with sender_timestamp, like a tag
    uint32_t now = getRTCClock()->getCurrentTime();
    memcpy(&reply_data[4], &now, 4);     // include our clock (for easy clock sync, and packet hash uniqueness)
    reply_data[8] = 0;  // features
#ifdef WITH_RS232_BRIDGE
    reply_data[8] |= 0x01;  // is bridge, type UART
#elif WITH_ESPNOW_BRIDGE
    reply_data[8] |= 0x03;  // is bridge, type ESP-NOW
#endif
    if (_prefs.disable_fwd) {   // is this repeater currently disabled
      reply_data[8] |= 0x80;  // is disabled
    }
    // TODO:  add some kind of moving-window utilisation metric, so can query 'how busy' is this repeater
    return 9;   // reply length
  }
  return 0;
}

int MyMesh::handleRequest(ClientInfo *sender, uint32_t sender_timestamp, uint8_t *payload, size_t payload_len) {
  // uint32_t now = getRTCClock()->getCurrentTimeUnique();
  // memcpy(reply_data, &now, 4);   // response packets always prefixed with timestamp
  memcpy(reply_data, &sender_timestamp, 4); // reflect sender_timestamp back in response packet (kind of like a 'tag')

  if (payload[0] == REQ_TYPE_GET_STATUS) {  // guests can also access this now
    RepeaterStats stats;
    stats.batt_milli_volts = board.getBattMilliVolts();
    stats.curr_tx_queue_len = _mgr->getOutboundTotal();
    stats.noise_floor = (int16_t)_radio->getNoiseFloor();
    stats.last_rssi = (int16_t)radio_driver.getLastRSSI();
    stats.n_packets_recv = radio_driver.getPacketsRecv();
    stats.n_packets_sent = radio_driver.getPacketsSent();
    stats.total_air_time_secs = getTotalAirTime() / 1000;
    stats.total_up_time_secs = uptime_millis / 1000;
    stats.n_sent_flood = getNumSentFlood();
    stats.n_sent_direct = getNumSentDirect();
    stats.n_recv_flood = getNumRecvFlood();
    stats.n_recv_direct = getNumRecvDirect();
    stats.err_events = _err_flags;
    stats.last_snr = (int16_t)(radio_driver.getLastSNR() * 4);
    stats.n_direct_dups = ((SimpleMeshTables *)getTables())->getNumDirectDups();
    stats.n_flood_dups = ((SimpleMeshTables *)getTables())->getNumFloodDups();
    stats.total_rx_air_time_secs = getReceiveAirTime() / 1000;
    stats.n_recv_errors = radio_driver.getPacketsRecvErrors();
    memcpy(&reply_data[4], &stats, sizeof(stats));

    return 4 + sizeof(stats); //  reply_len
  }
  if (payload[0] == REQ_TYPE_GET_TELEMETRY_DATA) {
    uint8_t perm_mask = ~(payload[1]); // NEW: first reserved byte (of 4), is now inverse mask to apply to permissions

    telemetry.reset();
    telemetry.addVoltage(TELEM_CHANNEL_SELF, (float)board.getBattMilliVolts() / 1000.0f);

    // query other sensors -- target specific
    if ((sender->permissions & PERM_ACL_ROLE_MASK) == PERM_ACL_GUEST) {
      perm_mask = 0x00;  // just base telemetry allowed
    }
    sensors.querySensors(perm_mask, telemetry);

	// This default temperature will be overridden by external sensors (if any)
    float temperature = board.getMCUTemperature();
    if(!isnan(temperature)) { // Supported boards with built-in temperature sensor. ESP32-C3 may return NAN
      telemetry.addTemperature(TELEM_CHANNEL_SELF, temperature); // Built-in MCU Temperature
    }

    uint8_t tlen = telemetry.getSize();
    memcpy(&reply_data[4], telemetry.getBuffer(), tlen);
    return 4 + tlen; // reply_len
  }
  if (payload[0] == REQ_TYPE_GET_ACCESS_LIST && sender->isAdmin()) {
    uint8_t res1 = payload[1];   // reserved for future  (extra query params)
    uint8_t res2 = payload[2];
    if (res1 == 0 && res2 == 0) {
      uint8_t ofs = 4;
      for (int i = 0; i < acl.getNumClients() && ofs + 7 <= sizeof(reply_data) - 4; i++) {
        auto c = acl.getClientByIdx(i);
        if (c->permissions == 0) continue;  // skip deleted entries
        memcpy(&reply_data[ofs], c->id.pub_key, 6); ofs += 6;  // just 6-byte pub_key prefix
        reply_data[ofs++] = c->permissions;
      }
      return ofs;
    }
  }
  if (payload[0] == REQ_TYPE_GET_NEIGHBOURS) {
    uint8_t request_version = payload[1];
    if (request_version == 0) {

      // reply data offset (after response sender_timestamp/tag)
      int reply_offset = 4;

      // get request params
      uint8_t count = payload[2]; // how many neighbours to fetch (0-255)
      uint16_t offset;
      memcpy(&offset, &payload[3], 2); // offset from start of neighbours list (0-65535)
      uint8_t order_by = payload[5]; // how to order neighbours. 0=newest_to_oldest, 1=oldest_to_newest, 2=strongest_to_weakest, 3=weakest_to_strongest
      uint8_t pubkey_prefix_length = payload[6]; // how many bytes of neighbour pub key we want
      // we also send a 4 byte random blob in payload[7...10] to help packet uniqueness

      MESH_DEBUG_PRINTLN("REQ_TYPE_GET_NEIGHBOURS count=%d, offset=%d, order_by=%d, pubkey_prefix_length=%d", count, offset, order_by, pubkey_prefix_length);

      // clamp pub key prefix length to max pub key length
      if(pubkey_prefix_length > PUB_KEY_SIZE){
        pubkey_prefix_length = PUB_KEY_SIZE;
        MESH_DEBUG_PRINTLN("REQ_TYPE_GET_NEIGHBOURS invalid pubkey_prefix_length=%d clamping to %d", pubkey_prefix_length, PUB_KEY_SIZE);
      }

      // create copy of neighbours list, skipping empty entries so we can sort it separately from main list
      int16_t neighbours_count = 0;
#if MAX_NEIGHBOURS
      NeighbourInfo* sorted_neighbours[MAX_NEIGHBOURS];
      for (int i = 0; i < MAX_NEIGHBOURS; i++) {
        auto neighbour = &neighbours[i];
        if (neighbour->heard_timestamp > 0) {
          sorted_neighbours[neighbours_count] = neighbour;
          neighbours_count++;
        }
      }

      // sort neighbours based on order
      if (order_by == 0) {
        // sort by newest to oldest
        MESH_DEBUG_PRINTLN("REQ_TYPE_GET_NEIGHBOURS sorting newest to oldest");
        std::sort(sorted_neighbours, sorted_neighbours + neighbours_count, [](const NeighbourInfo* a, const NeighbourInfo* b) {
          return a->heard_timestamp > b->heard_timestamp; // desc
        });
      } else if (order_by == 1) {
        // sort by oldest to newest
        MESH_DEBUG_PRINTLN("REQ_TYPE_GET_NEIGHBOURS sorting oldest to newest");
        std::sort(sorted_neighbours, sorted_neighbours + neighbours_count, [](const NeighbourInfo* a, const NeighbourInfo* b) {
          return a->heard_timestamp < b->heard_timestamp; // asc
        });
      } else if (order_by == 2) {
        // sort by strongest to weakest
        MESH_DEBUG_PRINTLN("REQ_TYPE_GET_NEIGHBOURS sorting strongest to weakest");
        std::sort(sorted_neighbours, sorted_neighbours + neighbours_count, [](const NeighbourInfo* a, const NeighbourInfo* b) {
          return a->snr > b->snr; // desc
        });
      } else if (order_by == 3) {
        // sort by weakest to strongest
        MESH_DEBUG_PRINTLN("REQ_TYPE_GET_NEIGHBOURS sorting weakest to strongest");
        std::sort(sorted_neighbours, sorted_neighbours + neighbours_count, [](const NeighbourInfo* a, const NeighbourInfo* b) {
          return a->snr < b->snr; // asc
        });
      }
#endif

      // build results buffer
      int results_count = 0;
      int results_offset = 0;
      uint8_t results_buffer[130];
      for(int index = 0; index < count && index + offset < neighbours_count; index++){
        
        // stop if we can't fit another entry in results
        int entry_size = pubkey_prefix_length + 4 + 1;
        if(results_offset + entry_size > sizeof(results_buffer)){
          MESH_DEBUG_PRINTLN("REQ_TYPE_GET_NEIGHBOURS no more entries can fit in results buffer");
          break;
        }

#if MAX_NEIGHBOURS
        // add next neighbour to results
        auto neighbour = sorted_neighbours[index + offset];
        uint32_t heard_seconds_ago = getRTCClock()->getCurrentTime() - neighbour->heard_timestamp;
        memcpy(&results_buffer[results_offset], neighbour->id.pub_key, pubkey_prefix_length); results_offset += pubkey_prefix_length;
        memcpy(&results_buffer[results_offset], &heard_seconds_ago, 4); results_offset += 4;
        memcpy(&results_buffer[results_offset], &neighbour->snr, 1); results_offset += 1;
        results_count++;
#endif

      }

      // build reply
      MESH_DEBUG_PRINTLN("REQ_TYPE_GET_NEIGHBOURS neighbours_count=%d results_count=%d", neighbours_count, results_count);
      memcpy(&reply_data[reply_offset], &neighbours_count, 2); reply_offset += 2;
      memcpy(&reply_data[reply_offset], &results_count, 2); reply_offset += 2;
      memcpy(&reply_data[reply_offset], &results_buffer, results_offset); reply_offset += results_offset;

      return reply_offset;
    }
  } else if (payload[0] == REQ_TYPE_GET_OWNER_INFO) {
    sprintf((char *) &reply_data[4], "%s\n%s\n%s", FIRMWARE_VERSION, _prefs.node_name, _prefs.owner_info);
    return 4 + strlen((char *) &reply_data[4]);
  }
  return 0; // unknown command
}

mesh::Packet *MyMesh::createSelfAdvert() {
  uint8_t app_data[MAX_ADVERT_DATA_SIZE];
  uint8_t app_data_len = _cli.buildAdvertData(ADV_TYPE_REPEATER, app_data);

  return createAdvert(self_id, app_data, app_data_len);
}

File MyMesh::openAppend(const char *fname) {
#if defined(NRF52_PLATFORM) || defined(STM32_PLATFORM)
  return _fs->open(fname, FILE_O_WRITE);
#elif defined(RP2040_PLATFORM)
  return _fs->open(fname, "a");
#else
  return _fs->open(fname, "a", true);
#endif
}

#if defined(P1_EVENT_LOG)
bool MyMesh::gpsContinuousPersisted() const {
  return _fs && _fs->exists(P1_GPS_CONTINUOUS_FILE);
}

bool MyMesh::persistGpsContinuous(bool enabled) {
  if (!_fs) return false;
  if (!enabled) {
    if (_fs->exists(P1_GPS_CONTINUOUS_FILE) &&
        !_fs->remove(P1_GPS_CONTINUOUS_FILE)) {
      return false;
    }
    return !_fs->exists(P1_GPS_CONTINUOUS_FILE);
  }

  if (_fs->exists(P1_GPS_CONTINUOUS_FILE) &&
      !_fs->remove(P1_GPS_CONTINUOUS_FILE)) {
    return false;
  }
  File file = _fs->open(P1_GPS_CONTINUOUS_FILE, FILE_O_WRITE);
  const uint8_t marker = 1;
  const bool written = file && file.write(&marker, 1) == 1;
  if (file) {
    file.flush();
    file.close();
  }
  return written && _fs->exists(P1_GPS_CONTINUOUS_FILE);
}

bool MyMesh::loadGpsPowerGuard() {
  gps_power_guard_mode = mesh::GpsPowerGuardMode::ECONOMY;
  if (!_fs) return false;
  if (!_fs->exists(P1_GPS_POWER_GUARD_FILE)) return true;

  File file = _fs->open(P1_GPS_POWER_GUARD_FILE, FILE_O_READ);
  if (!file) return false;
  char value[16];
  size_t len = file.readBytesUntil('\n', value, sizeof(value) - 1);
  value[len] = '\0';
  file.close();
  if (len > 0 && value[len - 1] == '\r') value[--len] = '\0';

  mesh::GpsPowerGuardMode loaded;
  if (!mesh::GpsPowerGuard::parseMode(value, loaded)) return false;
  gps_power_guard_mode = loaded;
  return true;
}

bool MyMesh::saveGpsPowerGuard() {
  if (!_fs) return false;
  if (_fs->exists(P1_GPS_POWER_GUARD_TEMP_FILE)) {
    _fs->remove(P1_GPS_POWER_GUARD_TEMP_FILE);
  }
  File file = _fs->open(P1_GPS_POWER_GUARD_TEMP_FILE, FILE_O_WRITE);
  if (!file) return false;
  const char* value = mesh::GpsPowerGuard::modeName(gps_power_guard_mode);
  const bool written = file.print(value) == strlen(value) &&
                       file.print('\n') == 1;
  file.flush();
  file.close();
  if (!written || !_fs->rename(P1_GPS_POWER_GUARD_TEMP_FILE,
                               P1_GPS_POWER_GUARD_FILE)) {
    _fs->remove(P1_GPS_POWER_GUARD_TEMP_FILE);
    return false;
  }
  return true;
}

void MyMesh::handleGpsPowerGuardCommand(const char* command, char* reply) {
  while (*command == ' ') command++;
  if (*command == '\0' || strcmp(command, "status") == 0) {
    snprintf(reply, 160,
             "GPS powerguard=%s (economy|critical|off); final SYSTEMOFF always protected",
             mesh::GpsPowerGuard::modeName(gps_power_guard_mode));
    return;
  }

  mesh::GpsPowerGuardMode requested;
  if (!mesh::GpsPowerGuard::parseMode(command, requested)) {
    strcpy(reply, "ERR - gps powerguard economy|critical|off|status");
    return;
  }
  const auto previous = gps_power_guard_mode;
  gps_power_guard_mode = requested;
  if (!saveGpsPowerGuard()) {
    gps_power_guard_mode = previous;
    strcpy(reply, "ERR - GPS powerguard not saved");
    return;
  }
  snprintf(reply, 160,
           "OK - GPS powerguard=%s; final SYSTEMOFF remains enabled",
           mesh::GpsPowerGuard::modeName(gps_power_guard_mode));
}
#endif

static uint8_t max_loop_minimal[] =  { 0, /* 1-byte */  4, /* 2-byte */  2, /* 3-byte */  1 };
static uint8_t max_loop_moderate[] = { 0, /* 1-byte */  2, /* 2-byte */  1, /* 3-byte */  1 };
static uint8_t max_loop_strict[] =   { 0, /* 1-byte */  1, /* 2-byte */  1, /* 3-byte */  1 };

bool MyMesh::isLooped(const mesh::Packet* packet, const uint8_t max_counters[]) {
  uint8_t hash_size = packet->getPathHashSize();
  uint8_t hash_count = packet->getPathHashCount();
  uint8_t n = 0;
  const uint8_t* path = packet->path;
  while (hash_count > 0) {      // count how many times this node is already in the path
    if (self_id.isHashMatch(path, hash_size)) n++;
    hash_count--;
    path += hash_size;
  }
  return n >= max_counters[hash_size];
}

void MyMesh::sendFloodReply(mesh::Packet* packet, unsigned long delay_millis, uint8_t path_hash_size) {
  TransportKey req_scope;
  bool is_wildcard = recv_pkt_region != NULL && recv_pkt_region->isWildcard();
  bool req_scope_known = recv_pkt_region != NULL && !is_wildcard
                      && region_map.getTransportKeysFor(*recv_pkt_region, &req_scope, 1) > 0;

  switch (mesh::chooseReplyScope(req_scope_known, is_wildcard, !default_scope.isNull())) {
    case mesh::REPLY_SCOPE_REQUEST:
      sendFloodScoped(req_scope, packet, delay_millis, path_hash_size);   // reply with same scope as request
      break;
    case mesh::REPLY_SCOPE_DEFAULT:
      // requester's scope is unknown: DIRECT request (no transport codes), or code matched no Region.
      // un-scoped would be dropped at hop 0 by repeaters running flood.max.unscoped=0
      sendFloodScoped(default_scope, packet, delay_millis, path_hash_size);
      break;
    case mesh::REPLY_SCOPE_NONE:
      sendFlood(packet, delay_millis, path_hash_size);  // send un-scoped
      break;
  }
}

bool MyMesh::allowPacketForward(const mesh::Packet *packet) {
  if (_prefs.disable_fwd) return false;
  if (packet->isRouteFlood()
      && mesh::isFloodHopLimitExceeded(packet, _prefs.flood_max, _prefs.flood_max_unscoped, _prefs.flood_max_advert)) {
    return false;
  }
  if (packet->isRouteFlood() && recv_pkt_region == NULL) {
    MESH_DEBUG_PRINTLN("allowPacketForward: unknown transport code, or wildcard not allowed for FLOOD packet");
    return false;
  }
  if (packet->isRouteFlood() && _prefs.loop_detect != LOOP_DETECT_OFF) {
    const uint8_t* maximums;
    if (_prefs.loop_detect == LOOP_DETECT_MINIMAL) {
      maximums = max_loop_minimal;
    } else if (_prefs.loop_detect == LOOP_DETECT_MODERATE) {
      maximums = max_loop_moderate;
    } else {
      maximums = max_loop_strict;
    }
    if (isLooped(packet, maximums)) {
      MESH_DEBUG_PRINTLN("allowPacketForward: FLOOD packet loop detected!");
      return false;
    }
  }
  return true;
}

const char *MyMesh::getLogDateTime() {
  static char tmp[32];
  uint32_t now = getRTCClock()->getCurrentTime();
  DateTime dt = DateTime(now);
  sprintf(tmp, "%02d:%02d:%02d - %d/%d/%d U", dt.hour(), dt.minute(), dt.second(), dt.day(), dt.month(),
          dt.year());
  return tmp;
}

void MyMesh::logRxRaw(float snr, float rssi, const uint8_t raw[], int len) {
#if MESH_PACKET_LOGGING
  Serial.print(getLogDateTime());
  Serial.print(" RAW: ");
  mesh::Utils::printHex(Serial, raw, len);
  Serial.println();
#endif
}

void MyMesh::logRx(mesh::Packet *pkt, int len, float score) {
#ifdef WITH_BRIDGE
  if (_prefs.bridge_pkt_src == 1) {
    bridge.sendPacket(pkt);
  }
#endif

  if (_logging) {
    File f = openAppend(PACKET_LOG_FILE);
    if (f) {
      f.print(getLogDateTime());
      f.printf(": RX, len=%d (type=%d, route=%s, payload_len=%d) SNR=%d RSSI=%d score=%d", len,
               pkt->getPayloadType(), pkt->isRouteDirect() ? "D" : "F", pkt->payload_len,
               (int)_radio->getLastSNR(), (int)_radio->getLastRSSI(), (int)(score * 1000));

      if (pkt->getPayloadType() == PAYLOAD_TYPE_PATH || pkt->getPayloadType() == PAYLOAD_TYPE_REQ ||
          pkt->getPayloadType() == PAYLOAD_TYPE_RESPONSE || pkt->getPayloadType() == PAYLOAD_TYPE_TXT_MSG) {
        f.printf(" [%02X -> %02X]\n", (uint32_t)pkt->payload[1], (uint32_t)pkt->payload[0]);
      } else {
        f.printf("\n");
      }
      f.close();
    }
  }
}

void MyMesh::logTx(mesh::Packet *pkt, int len) {
#ifdef WITH_BRIDGE
  if (_prefs.bridge_pkt_src == 0) {
    bridge.sendPacket(pkt);
  }
#endif

  if (_logging) {
    File f = openAppend(PACKET_LOG_FILE);
    if (f) {
      f.print(getLogDateTime());
      f.printf(": TX, len=%d (type=%d, route=%s, payload_len=%d)", len, pkt->getPayloadType(),
               pkt->isRouteDirect() ? "D" : "F", pkt->payload_len);

      if (pkt->getPayloadType() == PAYLOAD_TYPE_PATH || pkt->getPayloadType() == PAYLOAD_TYPE_REQ ||
          pkt->getPayloadType() == PAYLOAD_TYPE_RESPONSE || pkt->getPayloadType() == PAYLOAD_TYPE_TXT_MSG) {
        f.printf(" [%02X -> %02X]\n", (uint32_t)pkt->payload[1], (uint32_t)pkt->payload[0]);
      } else {
        f.printf("\n");
      }
      f.close();
    }
  }
}

void MyMesh::logTxFail(mesh::Packet *pkt, int len) {
  if (_logging) {
    File f = openAppend(PACKET_LOG_FILE);
    if (f) {
      f.print(getLogDateTime());
      f.printf(": TX FAIL!, len=%d (type=%d, route=%s, payload_len=%d)\n", len, pkt->getPayloadType(),
               pkt->isRouteDirect() ? "D" : "F", pkt->payload_len);
      f.close();
    }
  }
}

int MyMesh::calcRxDelay(float score, uint32_t air_time) const {
  if (_prefs.rx_delay_base <= 0.0f) return 0;
  return (int)((pow(_prefs.rx_delay_base, 0.85f - score) - 1.0) * air_time);
}

uint32_t MyMesh::getRetransmitDelay(const mesh::Packet *packet) {
  uint32_t t = (_radio->getEstAirtimeFor(packet->getPathByteLen() + packet->payload_len + 2) * _prefs.tx_delay_factor);
  return getRNG()->nextInt(0, 5*t + 1);
}
uint32_t MyMesh::getDirectRetransmitDelay(const mesh::Packet *packet) {
  uint32_t t = (_radio->getEstAirtimeFor(packet->getPathByteLen() + packet->payload_len + 2) * _prefs.direct_tx_delay_factor);
  return getRNG()->nextInt(0, 5*t + 1);
}

mesh::DispatcherAction MyMesh::onRecvPacket(mesh::Packet* pkt) {
  if (pkt->getRouteType() == ROUTE_TYPE_TRANSPORT_FLOOD) {
    recv_pkt_region = region_map.findMatch(pkt, REGION_DENY_FLOOD);
  } else if (pkt->getRouteType() == ROUTE_TYPE_FLOOD) {
    if (region_map.getWildcard().flags & REGION_DENY_FLOOD) {
      recv_pkt_region = NULL;
    } else {
      recv_pkt_region =  &region_map.getWildcard();
    }
  } else {
    recv_pkt_region = NULL;
  }
  return Mesh::onRecvPacket(pkt);
}

void MyMesh::onAnonDataRecv(mesh::Packet *packet, const uint8_t *secret, const mesh::Identity &sender,
                            uint8_t *data, size_t len) {
  if (packet->getPayloadType() == PAYLOAD_TYPE_ANON_REQ) { // received an initial request by a possible admin
                                                           // client (unknown at this stage)
    uint32_t timestamp;
    memcpy(&timestamp, data, 4);

    data[len] = 0;  // ensure null terminator
    uint8_t reply_len;

    reply_path_len = 0xFF;
    if (data[4] == 0 || data[4] >= ' ') {   // is password, ie. a login request
      reply_len = handleLoginReq(sender, secret, timestamp, &data[4], packet->isRouteFlood());
    } else if (data[4] == ANON_REQ_TYPE_REGIONS && packet->isRouteDirect()) {
      reply_len = handleAnonRegionsReq(sender, timestamp, &data[5]);
    } else if (data[4] == ANON_REQ_TYPE_OWNER && packet->isRouteDirect()) {
      reply_len = handleAnonOwnerReq(sender, timestamp, &data[5]);
    } else if (data[4] == ANON_REQ_TYPE_BASIC && packet->isRouteDirect()) {
      reply_len = handleAnonClockReq(sender, timestamp, &data[5]);
    } else {
      reply_len = 0;  // unknown/invalid request type
    }

    if (reply_len == 0) return;   // invalid request

    // a DIRECT login can reply via the stored out_path, as onPeerDataRecv() does for REQ
    ClientInfo* client = acl.getClient(sender.pub_key, PUB_KEY_SIZE);
    bool have_out_path = client != NULL && client->out_path_len != OUT_PATH_UNKNOWN;

    auto route = mesh::chooseReplyRoute(packet->isRouteFlood(), reply_path_len != 0xFF, have_out_path);

    if (route == mesh::REPLY_ROUTE_PATH_RETURN) {
      // let this sender know path TO here, so they can use sendDirect(), and ALSO encode the response
      mesh::Packet* path = createPathReturn(sender, secret, packet->path, packet->path_len,
                                            PAYLOAD_TYPE_RESPONSE, reply_data, reply_len);
      if (path) sendFloodReply(path, SERVER_RESPONSE_DELAY, packet->getPathHashSize());
      return;
    }

    mesh::Packet* reply = createDatagram(PAYLOAD_TYPE_RESPONSE, sender, secret, reply_data, reply_len);
    if (reply == NULL) return;

    if (route == mesh::REPLY_ROUTE_DIRECT_SUPPLIED) {
      sendDirect(reply, reply_path, reply_path_len, SERVER_RESPONSE_DELAY);
    } else if (route == mesh::REPLY_ROUTE_DIRECT_OUT_PATH) {
      sendDirect(reply, client->out_path, client->out_path_len, SERVER_RESPONSE_DELAY);
    } else {
      sendFloodReply(reply, SERVER_RESPONSE_DELAY, packet->getPathHashSize());
    }
  }
}

int MyMesh::searchPeersByHash(const uint8_t *hash) {
  int n = 0;
  for (int i = 0; i < acl.getNumClients(); i++) {
    if (acl.getClientByIdx(i)->id.isHashMatch(hash)) {
      matching_peer_indexes[n++] = i; // store the INDEXES of matching contacts (for subsequent 'peer' methods)
    }
  }
  return n;
}

void MyMesh::getPeerSharedSecret(uint8_t *dest_secret, int peer_idx) {
  int i = matching_peer_indexes[peer_idx];
  if (i >= 0 && i < acl.getNumClients()) {
    // lookup pre-calculated shared_secret
    memcpy(dest_secret, acl.getClientByIdx(i)->shared_secret, PUB_KEY_SIZE);
  } else {
    MESH_DEBUG_PRINTLN("getPeerSharedSecret: Invalid peer idx: %d", i);
  }
}

static bool isShare(const mesh::Packet *packet) {
  if (packet->hasTransportCodes()) {
    return packet->transport_codes[0] == 0 && packet->transport_codes[1] == 0;  // codes { 0, 0 } means 'send to nowhere'
  }
  return false;
}

void MyMesh::onAdvertRecv(mesh::Packet *packet, const mesh::Identity &id, uint32_t timestamp,
                          const uint8_t *app_data, size_t app_data_len) {
  mesh::Mesh::onAdvertRecv(packet, id, timestamp, app_data, app_data_len); // chain to super impl

  // if this a zero hop advert (and not via 'Share'), add it to neighbours
  if (packet->getPathHashCount() == 0 && !isShare(packet)) {
    AdvertDataParser parser(app_data, app_data_len);
    if (parser.isValid() && parser.getType() == ADV_TYPE_REPEATER) { // just keep neigbouring Repeaters
      putNeighbour(id, timestamp, packet->getSNR());
    }
  }
}

void MyMesh::onPeerDataRecv(mesh::Packet *packet, uint8_t type, int sender_idx, const uint8_t *secret,
                            uint8_t *data, size_t len) {
  int i = matching_peer_indexes[sender_idx];
  if (i < 0 || i >= acl.getNumClients()) { // get from our known_clients table (sender SHOULD already be known in this context)
    MESH_DEBUG_PRINTLN("onPeerDataRecv: invalid peer idx: %d", i);
    return;
  }
  ClientInfo* client = acl.getClientByIdx(i);

  if (type == PAYLOAD_TYPE_REQ) { // request (from a Known admin client!)
    uint32_t timestamp;
    memcpy(&timestamp, data, 4);

    if (timestamp > client->last_timestamp) { // prevent replay attacks
      int reply_len = handleRequest(client, timestamp, &data[4], len - 4);
      if (reply_len == 0) return; // invalid command

      client->last_timestamp = timestamp;
      client->last_activity = getRTCClock()->getCurrentTime();

      if (packet->isRouteFlood()) {
        // let this sender know path TO here, so they can use sendDirect(), and ALSO encode the response
        mesh::Packet *path = createPathReturn(client->id, secret, packet->path, packet->path_len,
                                              PAYLOAD_TYPE_RESPONSE, reply_data, reply_len);
        if (path) sendFloodReply(path, SERVER_RESPONSE_DELAY, packet->getPathHashSize());
      } else {
        mesh::Packet *reply =
            createDatagram(PAYLOAD_TYPE_RESPONSE, client->id, secret, reply_data, reply_len);
        if (reply) {
          if (client->out_path_len != OUT_PATH_UNKNOWN) { // we have an out_path, so send DIRECT
            sendDirect(reply, client->out_path, client->out_path_len, SERVER_RESPONSE_DELAY);
          } else {
            sendFloodReply(reply, SERVER_RESPONSE_DELAY, packet->getPathHashSize());
          }
        }
      }
    } else {
      MESH_DEBUG_PRINTLN("onPeerDataRecv: possible replay attack detected");
    }
  } else if (type == PAYLOAD_TYPE_TXT_MSG && len > 5 && client->isAdmin()) { // a CLI command
    uint32_t sender_timestamp;
    memcpy(&sender_timestamp, data, 4); // timestamp (by sender's RTC clock - which could be wrong)
    uint8_t flags = (data[4] >> 2);        // message attempt number, and other flags

    if (!(flags == TXT_TYPE_PLAIN || flags == TXT_TYPE_CLI_DATA)) {
      MESH_DEBUG_PRINTLN("onPeerDataRecv: unsupported text type received: flags=%02x", (uint32_t)flags);
    } else if (sender_timestamp >= client->last_timestamp) { // prevent replay attacks
      bool is_retry = (sender_timestamp == client->last_timestamp);
      client->last_timestamp = sender_timestamp;
      client->last_activity = getRTCClock()->getCurrentTime();

      // len can be > original length, but 'text' will be padded with zeroes
      data[len] = 0; // need to make a C string again, with null terminator

      if (flags == TXT_TYPE_PLAIN) { // for legacy CLI, send Acks
        uint32_t ack_hash; // calc truncated hash of the message timestamp + text + sender pub_key, to prove
                           // to sender that we got it
        mesh::Utils::sha256((uint8_t *)&ack_hash, 4, data, 5 + strlen((char *)&data[5]), client->id.pub_key,
                            PUB_KEY_SIZE);

        mesh::Packet *ack = createAck(ack_hash);
        if (ack) {
          if (client->out_path_len == OUT_PATH_UNKNOWN) {
            sendFloodReply(ack, TXT_ACK_DELAY, packet->getPathHashSize());
          } else {
            sendDirect(ack, client->out_path, client->out_path_len, TXT_ACK_DELAY);
          }
        }
      }

      uint8_t temp[166];
      char *command = (char *)&data[5];
      char *reply = (char *)&temp[5];
      if (is_retry) {
        *reply = 0;
      } else {
        handleCommand(sender_timestamp, command, reply);
      }
      int text_len = strlen(reply);
      if (text_len > 0) {
        uint32_t timestamp = getRTCClock()->getCurrentTimeUnique();
        if (timestamp == sender_timestamp) {
          // WORKAROUND: the two timestamps need to be different, in the CLI view
          timestamp++;
        }
        memcpy(temp, &timestamp, 4);        // mostly an extra blob to help make packet_hash unique
        temp[4] = (TXT_TYPE_CLI_DATA << 2); // NOTE: legacy was: TXT_TYPE_PLAIN

        auto reply = createDatagram(PAYLOAD_TYPE_TXT_MSG, client->id, secret, temp, 5 + text_len);
        if (reply) {
          if (client->out_path_len == OUT_PATH_UNKNOWN) {
            sendFloodReply(reply, CLI_REPLY_DELAY_MILLIS, packet->getPathHashSize());
          } else {
            sendDirect(reply, client->out_path, client->out_path_len, CLI_REPLY_DELAY_MILLIS);
          }
        }
      }
    } else {
      MESH_DEBUG_PRINTLN("onPeerDataRecv: possible replay attack detected");
    }
  }
}

bool MyMesh::onPeerPathRecv(mesh::Packet *packet, int sender_idx, const uint8_t *secret, uint8_t *path,
                            uint8_t path_len, uint8_t extra_type, uint8_t *extra, uint8_t extra_len) {
  // TODO: prevent replay attacks
  int i = matching_peer_indexes[sender_idx];

  if (i >= 0 && i < acl.getNumClients()) { // get from our known_clients table (sender SHOULD already be known in this context)
    MESH_DEBUG_PRINTLN("PATH to client, path_len=%d", (uint32_t)path_len);
    auto client = acl.getClientByIdx(i);

    // store a copy of path, for sendDirect()
    client->out_path_len = mesh::Packet::copyPath(client->out_path, path, path_len);
    client->last_activity = getRTCClock()->getCurrentTime();
  } else {
    MESH_DEBUG_PRINTLN("onPeerPathRecv: invalid peer idx: %d", i);
  }

  // NOTE: no reciprocal path send!!
  return false;
}

#define CTL_TYPE_NODE_DISCOVER_REQ   0x80
#define CTL_TYPE_NODE_DISCOVER_RESP  0x90

void MyMesh::onControlDataRecv(mesh::Packet* packet) {
  uint8_t type = packet->payload[0] & 0xF0;    // just test upper 4 bits
  if (type == CTL_TYPE_NODE_DISCOVER_REQ && packet->payload_len >= 6
      && !_prefs.disable_fwd && discover_limiter.allow(rtc_clock.getCurrentTime())
  ) {
    int i = 1;
    uint8_t  filter = packet->payload[i++];
    uint32_t tag;
    memcpy(&tag, &packet->payload[i], 4); i += 4;
    uint32_t since;
    if (packet->payload_len >= i+4) {   // optional since field
      memcpy(&since, &packet->payload[i], 4); i += 4;
    } else {
      since = 0;
    }

    if ((filter & (1 << ADV_TYPE_REPEATER)) != 0 && _prefs.discovery_mod_timestamp >= since) {
      bool prefix_only = packet->payload[0] & 1;
      uint8_t data[6 + PUB_KEY_SIZE];
      data[0] = CTL_TYPE_NODE_DISCOVER_RESP | ADV_TYPE_REPEATER;   // low 4-bits for node type
      data[1] = packet->_snr;   // let sender know the inbound SNR ( x 4)
      memcpy(&data[2], &tag, 4);     // include tag from request, for client to match to
      memcpy(&data[6], self_id.pub_key, PUB_KEY_SIZE);
      auto resp = createControlData(data, prefix_only ? 6 + 8 : 6 + PUB_KEY_SIZE);
      if (resp) {
        sendZeroHop(resp, getRetransmitDelay(resp)*4);  // apply random delay (widened x4), as multiple nodes can respond to this
      }
    }
  } else if (type == CTL_TYPE_NODE_DISCOVER_RESP && packet->payload_len >= 6) {
    uint8_t node_type = packet->payload[0] & 0x0F;
    if (node_type != ADV_TYPE_REPEATER) {
      return;
    }
    if (packet->payload_len < 6 + PUB_KEY_SIZE) {
      MESH_DEBUG_PRINTLN("onControlDataRecv: DISCOVER_RESP pubkey too short: %d", (uint32_t)packet->payload_len);
      return;
    }

    if (pending_discover_tag == 0 || millisHasNowPassed(pending_discover_until)) {
      pending_discover_tag = 0;
      return;
    }
    uint32_t tag;
    memcpy(&tag, &packet->payload[2], 4);
    if (tag != pending_discover_tag) {
      return;
    }

    mesh::Identity id(&packet->payload[6]);
    if (id.matches(self_id)) {
      return;
    }
    putNeighbour(id, rtc_clock.getCurrentTime(), packet->getSNR());
  }
}

void MyMesh::sendNodeDiscoverReq() {
  uint8_t data[10];
  data[0] = CTL_TYPE_NODE_DISCOVER_REQ; // prefix_only=0
  data[1] = (1 << ADV_TYPE_REPEATER);
  getRNG()->random(&data[2], 4); // tag
  memcpy(&pending_discover_tag, &data[2], 4);
  pending_discover_until = futureMillis(60000);
  uint32_t since = 0;
  memcpy(&data[6], &since, 4);

  auto pkt = createControlData(data, sizeof(data));
  if (pkt) {
    sendZeroHop(pkt);
  }
}

#if defined(P1_POWER_ALERTS)
bool MyMesh::loadAlertRecipients() {
  alert_recipients.clear();
  if (!_fs) return false;

  if (!_fs->exists(P1_ALERT_RECIPIENTS_FILE)) {
#ifdef P1_DEFAULT_ALERT_RECIPIENT
    if (!alert_recipients.addHex(P1_DEFAULT_ALERT_RECIPIENT)) return false;
    return saveAlertRecipients();
#else
    return true;
#endif
  }

  File file = _fs->open(P1_ALERT_RECIPIENTS_FILE, FILE_O_READ);
  if (!file) return false;

  bool valid = true;
  while (file.available()) {
    char line[mesh::AlertRecipientList<MAX_ALERT_RECIPIENTS>::KEY_HEX_CHARS + 3];
    size_t len = file.readBytesUntil('\n', line, sizeof(line) - 1);
    line[len] = '\0';
    if (len > 0 && line[len - 1] == '\r') line[--len] = '\0';
    if (len == 0) continue;
    if (!alert_recipients.addHex(line)) {
      valid = false;
      break;
    }
  }
  file.close();

  if (!valid) alert_recipients.clear();
  return valid;
}

bool MyMesh::saveAlertRecipients() {
  if (!_fs) return false;
  if (_fs->exists(P1_ALERT_RECIPIENTS_TEMP_FILE)) {
    _fs->remove(P1_ALERT_RECIPIENTS_TEMP_FILE);
  }

  File file = _fs->open(P1_ALERT_RECIPIENTS_TEMP_FILE, FILE_O_WRITE);
  if (!file) return false;

  bool written = true;
  char hex[mesh::AlertRecipientList<MAX_ALERT_RECIPIENTS>::KEY_HEX_CHARS + 1];
  for (size_t i = 0; i < alert_recipients.count(); i++) {
    mesh::AlertRecipientList<MAX_ALERT_RECIPIENTS>::encodeHexKey(
        alert_recipients.keyAt(i), hex);
    written = written && file.print(hex) == strlen(hex);
    written = written && file.print('\n') == 1;
  }
  file.flush();
  file.close();

  if (!written) {
    _fs->remove(P1_ALERT_RECIPIENTS_TEMP_FILE);
    return false;
  }
  if (!_fs->rename(P1_ALERT_RECIPIENTS_TEMP_FILE,
                   P1_ALERT_RECIPIENTS_FILE)) {
    _fs->remove(P1_ALERT_RECIPIENTS_TEMP_FILE);
    return false;
  }
  return true;
}

bool MyMesh::loadAlertConfig() {
  early_alert_mv = P1_EARLY_ALERT_MV;
  early_alert_clear_mv = P1_EARLY_ALERT_CLEAR_MV;
  if (!_fs) return false;
  if (!_fs->exists(P1_ALERT_CONFIG_FILE)) return saveAlertConfig();

  File file = _fs->open(P1_ALERT_CONFIG_FILE, FILE_O_READ);
  if (!file) return false;
  char line[32];
  const size_t len = file.readBytesUntil('\n', line, sizeof(line) - 1);
  line[len] = '\0';
  file.close();

  unsigned warning = 0;
  unsigned clear = 0;
  char trailing = 0;
  if (sscanf(line, "%u %u %c", &warning, &clear, &trailing) != 2 ||
      warning < 3000 || warning > 4100 ||
      clear < warning + 25 || clear > 4300) {
    early_alert_mv = P1_EARLY_ALERT_MV;
    early_alert_clear_mv = P1_EARLY_ALERT_CLEAR_MV;
    return false;
  }
  early_alert_mv = (uint16_t)warning;
  early_alert_clear_mv = (uint16_t)clear;
  return true;
}

bool MyMesh::saveAlertConfig() {
  if (!_fs) return false;
  if (_fs->exists(P1_ALERT_CONFIG_TEMP_FILE)) {
    _fs->remove(P1_ALERT_CONFIG_TEMP_FILE);
  }
  File file = _fs->open(P1_ALERT_CONFIG_TEMP_FILE, FILE_O_WRITE);
  if (!file) return false;
  const size_t written = file.printf("%u %u\n", early_alert_mv,
                                     early_alert_clear_mv);
  file.flush();
  file.close();
  if (written == 0) {
    _fs->remove(P1_ALERT_CONFIG_TEMP_FILE);
    return false;
  }
  if (!_fs->rename(P1_ALERT_CONFIG_TEMP_FILE, P1_ALERT_CONFIG_FILE)) {
    _fs->remove(P1_ALERT_CONFIG_TEMP_FILE);
    return false;
  }
  return true;
}

bool MyMesh::configureAlertChannel(mesh::GroupChannel& channel,
                                   const char* encoded_key,
                                   char* normalized_key,
                                   size_t normalized_key_size) {
  if (!encoded_key) return false;
  const size_t encoded_len = strlen(encoded_key);
  uint8_t secret[PUB_KEY_SIZE] = {};
  int secret_len = 0;

  bool all_hex = encoded_len == 32 || encoded_len == 64;
  for (size_t i = 0; all_hex && i < encoded_len; i++) {
    all_hex = mesh::Utils::isHexChar(encoded_key[i]);
  }
  if (all_hex) {
    secret_len = (int)(encoded_len / 2);
    if (!mesh::Utils::fromHex(secret, secret_len, encoded_key)) return false;
  } else {
    // Exact encoded lengths bound the third-party decoder to the 32-byte
    // destination and reject truncated/oversized credentials early.
    if (!(encoded_len == 24 || encoded_len == 44)) return false;
    secret_len = decode_base64((unsigned char*)encoded_key, encoded_len,
                               secret);
    if (!(secret_len == 16 || secret_len == 32)) return false;
  }

  memset(&channel, 0, sizeof(channel));
  memcpy(channel.secret, secret, secret_len);
  mesh::Utils::sha256(channel.hash, sizeof(channel.hash), channel.secret,
                      secret_len);
  if (normalized_key && normalized_key_size > encoded_len) {
    StrHelper::strncpy(normalized_key, encoded_key, normalized_key_size);
  }
  return true;
}

bool MyMesh::loadAlertChannelConfig() {
  alert_destination = AlertDestination::PRIVATE;
  custom_alert_channel_configured = false;
  custom_alert_channel_name[0] = '\0';
  custom_alert_channel_key[0] = '\0';
  memset(&custom_alert_channel, 0, sizeof(custom_alert_channel));

  if (!configureAlertChannel(public_alert_channel, P1_PUBLIC_GROUP_PSK)) {
    return false;
  }
  if (!_fs) return false;
  if (!_fs->exists(P1_ALERT_CHANNEL_FILE)) return true;

  File file = _fs->open(P1_ALERT_CHANNEL_FILE, FILE_O_READ);
  if (!file) return false;
  char mode[16];
  char name[MAX_ALERT_CHANNEL_NAME_LEN + 2];
  char key[MAX_ALERT_CHANNEL_KEY_LEN + 2];
  auto readLine = [&file](char* dest, size_t capacity) -> bool {
    if (!capacity) return false;
    const size_t len = file.readBytesUntil('\n', dest, capacity - 1);
    dest[len] = '\0';
    if (len > 0 && dest[len - 1] == '\r') dest[len - 1] = '\0';
    return len > 0;
  };
  const bool have_mode = readLine(mode, sizeof(mode));
  const bool have_name = readLine(name, sizeof(name));
  const bool have_key = readLine(key, sizeof(key));
  file.close();
  if (!have_mode) return false;

  if (have_name && have_key && strcmp(name, "-") != 0 &&
      strcmp(key, "-") != 0) {
    const size_t name_len = strlen(name);
    bool valid_name = name_len > 0 && name_len <= MAX_ALERT_CHANNEL_NAME_LEN;
    for (size_t i = 0; valid_name && i < name_len; i++) {
      valid_name = (uint8_t)name[i] >= 32 && (uint8_t)name[i] <= 126;
    }
    if (!valid_name || !configureAlertChannel(
            custom_alert_channel, key, custom_alert_channel_key,
            sizeof(custom_alert_channel_key))) {
      return false;
    }
    StrHelper::strncpy(custom_alert_channel_name, name,
                       sizeof(custom_alert_channel_name));
    custom_alert_channel_configured = true;
  }

  if (strcmp(mode, "private") == 0) {
    alert_destination = AlertDestination::PRIVATE;
  } else if (strcmp(mode, "public") == 0) {
    alert_destination = AlertDestination::PUBLIC;
  } else if (strcmp(mode, "channel") == 0 &&
             custom_alert_channel_configured) {
    alert_destination = AlertDestination::CUSTOM_CHANNEL;
  } else {
    return false;
  }
  return true;
}

bool MyMesh::saveAlertChannelConfig() {
  if (!_fs) return false;
  if (_fs->exists(P1_ALERT_CHANNEL_TEMP_FILE)) {
    _fs->remove(P1_ALERT_CHANNEL_TEMP_FILE);
  }
  File file = _fs->open(P1_ALERT_CHANNEL_TEMP_FILE, FILE_O_WRITE);
  if (!file) return false;

  const char* mode = "private";
  if (alert_destination == AlertDestination::PUBLIC) mode = "public";
  if (alert_destination == AlertDestination::CUSTOM_CHANNEL) mode = "channel";
  const char* name = custom_alert_channel_configured
                         ? custom_alert_channel_name : "-";
  const char* key = custom_alert_channel_configured
                        ? custom_alert_channel_key : "-";
  const bool written = file.print(mode) == strlen(mode) &&
                       file.print('\n') == 1 &&
                       file.print(name) == strlen(name) &&
                       file.print('\n') == 1 &&
                       file.print(key) == strlen(key) &&
                       file.print('\n') == 1;
  file.flush();
  file.close();
  if (!written || !_fs->rename(P1_ALERT_CHANNEL_TEMP_FILE,
                               P1_ALERT_CHANNEL_FILE)) {
    _fs->remove(P1_ALERT_CHANNEL_TEMP_FILE);
    return false;
  }
  return true;
}

uint8_t MyMesh::alertDestinationCount() const {
  if (alert_destination == AlertDestination::PRIVATE) {
    return (uint8_t)alert_recipients.count();
  }
  if (alert_destination == AlertDestination::PUBLIC) return 1;
  return custom_alert_channel_configured ? 1 : 0;
}

const char* MyMesh::alertDestinationName() const {
  if (alert_destination == AlertDestination::PRIVATE) return "private";
  if (alert_destination == AlertDestination::PUBLIC) return "Public";
  return custom_alert_channel_configured ? custom_alert_channel_name
                                         : "channel-unconfigured";
}

bool MyMesh::sendGroupText(const mesh::GroupChannel& channel, const char* text,
                           uint32_t timestamp, uint32_t delay_millis,
                           const mesh::Packet* reply_to) {
  if (!text || !*text) return false;
  // Group text wire format used by companion radios: timestamp, plain-text
  // type, then "sender: message".  Keeping the same timestamp on the retry
  // produces the same packet hash, so clients that received the first flood
  // suppress a duplicate while a client that missed it can still accept it.
  uint8_t payload[5 + MAX_GROUP_DATA_LENGTH];
  memcpy(payload, &timestamp, sizeof(uint32_t));
  payload[4] = TXT_TYPE_PLAIN;
  int prefix_len = snprintf((char*)&payload[5], MAX_GROUP_DATA_LENGTH,
                            "%s: ", _prefs.node_name);
  if (prefix_len < 0) return false;
  if (prefix_len > MAX_GROUP_DATA_LENGTH - 5) {
    prefix_len = MAX_GROUP_DATA_LENGTH - 5;
  }
  size_t text_len = strlen(text);
  const size_t max_text_len = MAX_GROUP_DATA_LENGTH - 5 - (size_t)prefix_len;
  if (text_len > max_text_len) text_len = max_text_len;
  memcpy(&payload[5 + prefix_len], text, text_len);

  mesh::Packet* packet = createGroupDatagram(
      PAYLOAD_TYPE_GRP_TXT, channel, payload, 5 + prefix_len + text_len);
  if (!packet) return false;
  if (reply_to) {
    // A channel query may arrive unscoped or through a region other than this
    // node's default.  Reply through the incoming scope so the requesting
    // companion can actually receive it.
    sendFloodReply(packet, delay_millis, reply_to->getPathHashSize());
  } else {
    sendFloodScoped(default_scope, packet, delay_millis,
                    _prefs.path_hash_mode + 1);
  }
  return true;
}

bool MyMesh::sendAlertToChannel(const char* text) {
  const mesh::GroupChannel* channel = nullptr;
  if (alert_destination == AlertDestination::PUBLIC) {
    channel = &public_alert_channel;
  } else if (alert_destination == AlertDestination::CUSTOM_CHANNEL &&
             custom_alert_channel_configured) {
    channel = &custom_alert_channel;
  }
  return channel && sendGroupText(*channel, text,
                                  pending_alert_channel_timestamp);
}

bool MyMesh::sendChannelCliReply(const char* text,
                                 const mesh::Packet* incoming_packet,
                                 uint32_t incoming_timestamp) {
  if (alert_destination != AlertDestination::CUSTOM_CHANNEL ||
      !custom_alert_channel_configured) {
    return false;
  }
  // Stable per-node slots keep several repeaters on the same maintenance
  // channel from answering at the same instant.  The packet is queued, so
  // this adds no blocking delay to the repeater loop.
  const uint16_t identity_slot =
      (((uint16_t)self_id.pub_key[0] << 8) | self_id.pub_key[1]) % 16;
  const uint32_t delay_millis = 350U + (uint32_t)identity_slot * 450U;
  // The P1 may boot with an untrusted placeholder clock until its first GPS
  // fix.  Reuse the companion's recent channel-message timestamp so its UI
  // does not file a valid reply years back in the conversation history.
  uint32_t reply_timestamp = incoming_timestamp + 1U;
  if (incoming_timestamp == 0 || reply_timestamp == 0) {
    reply_timestamp = getRTCClock()->getCurrentTimeUnique();
  }
  return sendGroupText(custom_alert_channel, text,
                       reply_timestamp, delay_millis, incoming_packet);
}

void MyMesh::handleChannelCli(const char* command, char* reply,
                              size_t reply_size) {
  if (!reply || reply_size == 0) return;
  reply[0] = '\0';
  while (command && *command == ' ') command++;

  if (!command || *command == '\0' || strcmp(command, "help") == 0) {
    snprintf(reply, reply_size,
             "CLI OK: %s status|battery|gps|version|alerts|help",
             POWER_ALERT_CHANNEL_COMMAND);
    return;
  }
  if (strcmp(command, "version") == 0) {
    snprintf(reply, reply_size, "CLI OK version: %s (%s)", FIRMWARE_VERSION,
             FIRMWARE_BUILD_DATE);
    return;
  }
  if (strcmp(command, "battery") == 0 || strcmp(command, "power") == 0) {
    mesh::MainBoard::PowerStatus status;
    if (!board.getPowerStatus(status)) {
      snprintf(reply, reply_size, "CLI ERR battery: status indisponible");
      return;
    }
    snprintf(reply, reply_size,
             "CLI OK battery: %s; %u mV; alerte %u; veille %u; bas %lus/%lus",
             status.state ? status.state : "?", status.battery_mv,
             early_alert_mv, status.shutdown_mv,
             (unsigned long)status.low_seconds,
             (unsigned long)status.shutdown_delay_seconds);
    return;
  }
  if (strcmp(command, "gps") == 0) {
    char gps_status[120];
    if (!sensors.getGpsScheduleStatus(gps_status, sizeof(gps_status))) {
      snprintf(reply, reply_size, "CLI ERR gps: status indisponible");
      return;
    }
    snprintf(reply, reply_size, "CLI OK gps: %s", gps_status);
    return;
  }
  if (strcmp(command, "alerts") == 0) {
    snprintf(reply, reply_size,
             "CLI OK alerts: canal=%s; alerte=%u mV; veille=%u mV; radio-cli=lecture seule",
             alertDestinationName(), early_alert_mv, PWR_SHUTDOWN_MV);
    return;
  }
  if (strcmp(command, "status") == 0) {
    mesh::MainBoard::PowerStatus status;
    char gps_status[72] = "indisponible";
    sensors.getGpsScheduleStatus(gps_status, sizeof(gps_status));
    if (!board.getPowerStatus(status)) {
      snprintf(reply, reply_size, "CLI ERR status: alimentation indisponible");
      return;
    }
    snprintf(reply, reply_size, "CLI OK status: %s %u mV; GPS %s",
             status.state ? status.state : "?", status.battery_mv, gps_status);
    return;
  }

  snprintf(reply, reply_size, "CLI ERR: commande inconnue; envoyer %s help",
           POWER_ALERT_CHANNEL_COMMAND);
}

bool MyMesh::sendAlertTo(size_t index, const char* text, uint8_t attempt) {
  const uint8_t* public_key = alert_recipients.keyAt(index);
  if (!public_key || !text) return false;

  const size_t text_len = strlen(text);
  if (text_len == 0 || text_len > MAX_ALERT_TEXT_LEN) return false;

  uint8_t payload[5 + MAX_ALERT_TEXT_LEN];
  memcpy(payload, &pending_alert_timestamp[index], sizeof(uint32_t));
  payload[4] = (TXT_TYPE_PLAIN << 2) | (attempt & 3);
  memcpy(&payload[5], text, text_len);

  mesh::Utils::sha256((uint8_t*)&pending_alert_ack[index], 4,
                      payload, 5 + text_len, self_id.pub_key, PUB_KEY_SIZE);

  const mesh::Identity recipient(public_key);
  uint8_t shared_secret[PUB_KEY_SIZE];
  self_id.calcSharedSecret(shared_secret, public_key);
  mesh::Packet* packet = createDatagram(PAYLOAD_TYPE_TXT_MSG, recipient,
                                        shared_secret, payload, 5 + text_len);
  if (!packet) return false;

  ClientInfo* known_client = acl.getClient(public_key, PUB_KEY_SIZE);
  if (known_client && known_client->out_path_len != OUT_PATH_UNKNOWN) {
    sendDirect(packet, known_client->out_path, known_client->out_path_len);
  } else {
    sendFloodScoped(default_scope, packet, 0, _prefs.path_hash_mode + 1);
  }
  pending_alert_waiting[index] = true;
  return true;
}

uint8_t MyMesh::startOperationalAlert(const char* text, bool replace_active) {
  const uint8_t destination_count = alertDestinationCount();
  if (!text || !*text || destination_count == 0) return 0;

  if (pending_alert_active && !replace_active) {
    StrHelper::strncpy(deferred_alert_text, text, sizeof(deferred_alert_text));
    deferred_alert_pending = true;
    return destination_count;
  }
  if (replace_active) {
    pending_alert_active = false;
    deferred_alert_pending = false;
    memset(pending_alert_waiting, 0, sizeof(pending_alert_waiting));
    pending_alert_channel_waiting = false;
  }

  StrHelper::strncpy(pending_alert_text, text, sizeof(pending_alert_text));
  memset(pending_alert_timestamp, 0, sizeof(pending_alert_timestamp));
  memset(pending_alert_waiting, 0, sizeof(pending_alert_waiting));
  pending_alert_channel_waiting = false;
  pending_alert_channel_timestamp = 0;
  uint8_t sent = 0;
  if (alert_destination == AlertDestination::PRIVATE) {
    for (size_t i = 0; i < alert_recipients.count(); i++) {
      // A per-recipient timestamp makes the truncated ACK hash unambiguous even
      // though every destination receives the same alert text.
      pending_alert_timestamp[i] = getRTCClock()->getCurrentTimeUnique();
      if (sendAlertTo(i, pending_alert_text, 0)) sent++;
    }
  } else {
    pending_alert_channel_timestamp = getRTCClock()->getCurrentTimeUnique();
    pending_alert_channel_waiting = sendAlertToChannel(pending_alert_text);
    if (pending_alert_channel_waiting) sent++;
  }
  pending_alert_active = sent > 0;
  pending_alert_retried = false;
  pending_alert_retry_at = futureMillis(4000);
  pending_alert_finish_at = futureMillis(9000);
  return sent;
}

void MyMesh::serviceOperationalAlerts() {
  if (!pending_alert_active) return;

  if (!pending_alert_retried && millisHasNowPassed(pending_alert_retry_at)) {
    pending_alert_retried = true;
    for (size_t i = 0; i < alert_recipients.count(); i++) {
      if (pending_alert_waiting[i]) sendAlertTo(i, pending_alert_text, 1);
    }
    if (pending_alert_channel_waiting) {
      sendAlertToChannel(pending_alert_text);
    }
  }
  if (millisHasNowPassed(pending_alert_finish_at)) {
    pending_alert_active = false;
    memset(pending_alert_waiting, 0, sizeof(pending_alert_waiting));
    pending_alert_channel_waiting = false;
    if (deferred_alert_pending) {
      char next[MAX_ALERT_TEXT_LEN + 1];
      StrHelper::strncpy(next, deferred_alert_text, sizeof(next));
      deferred_alert_pending = false;
      deferred_alert_text[0] = '\0';
      startOperationalAlert(next);
    }
  }
}

int MyMesh::searchChannelsByHash(const uint8_t* hash,
                                 mesh::GroupChannel channels[],
                                 int max_matches) {
  // The radio CLI is intentionally unavailable on Public and in legacy
  // private-alert mode.  A valid custom-channel PSK is required to decrypt a
  // command; the one-byte hash merely selects the candidate key.
  if (!hash || !channels || max_matches <= 0 ||
      alert_destination != AlertDestination::CUSTOM_CHANNEL ||
      !custom_alert_channel_configured ||
      memcmp(hash, custom_alert_channel.hash, PATH_HASH_SIZE) != 0) {
    return 0;
  }
  channels[0] = custom_alert_channel;
  return 1;
}

void MyMesh::onGroupDataRecv(mesh::Packet* packet, uint8_t type,
                             const mesh::GroupChannel& channel, uint8_t* data,
                             size_t len) {
  (void)packet;
  (void)channel;
  if (type != PAYLOAD_TYPE_GRP_TXT || !data || len < 5) return;
  if ((data[4] >> 2) != TXT_TYPE_PLAIN) return;

  uint32_t incoming_timestamp = 0;
  memcpy(&incoming_timestamp, data, sizeof(incoming_timestamp));

  // Mesh reserves enough scratch space for this terminator; this mirrors the
  // standard companion-radio group text decoder.
  data[len] = '\0';
  const char* message = (const char*)&data[5];
  const char* sender_separator = strstr(message, ": ");
  if (sender_separator) message = sender_separator + 2;
  while (*message == ' ') message++;

  const size_t command_prefix_len = strlen(POWER_ALERT_CHANNEL_COMMAND);
  if (strncmp(message, POWER_ALERT_CHANNEL_COMMAND, command_prefix_len) != 0 ||
      !(message[command_prefix_len] == '\0' ||
        message[command_prefix_len] == ' ')) {
    return;
  }

  // Replayed packets are normally removed by the Mesh seen-packet table.  A
  // second guard bounds radio replies even if a companion creates fresh
  // timestamps for the same command.
  const uint32_t now = millis();
  if (last_channel_cli_at != 0 &&
      (uint32_t)(now - last_channel_cli_at) < 3000U) {
    return;
  }
  last_channel_cli_at = now;

  const char* command = message + command_prefix_len;
  while (*command == ' ') command++;
  char reply[MAX_ALERT_TEXT_LEN + 1];
  handleChannelCli(command, reply, sizeof(reply));
  sendChannelCliReply(reply, packet, incoming_timestamp);
}

void MyMesh::onAckRecv(mesh::Packet* packet, uint32_t ack_crc) {
  if (!pending_alert_active) return;
  for (size_t i = 0; i < alert_recipients.count(); i++) {
    if (pending_alert_waiting[i] && pending_alert_ack[i] == ack_crc) {
      pending_alert_waiting[i] = false;
      packet->markDoNotRetransmit();
      break;
    }
  }
}

void MyMesh::notifyPowerStatus(const mesh::MainBoard::PowerStatus& status) {
  if (!status.state) return;
  if (status.battery_mv >= 1000 && status.battery_mv <= 5000) {
    if (!early_alert_latched && status.battery_mv <= early_alert_mv) {
      early_alert_latched = true;
      char text[MAX_ALERT_TEXT_LEN + 1];
      snprintf(text, sizeof(text),
               "%s alerte arret: batterie %u mV; seuil veille %u mV approche; noeud encore actif.",
               POWER_ALERT_NODE_LABEL, status.battery_mv, status.shutdown_mv);
      startOperationalAlert(text);
    } else if (early_alert_latched &&
               status.battery_mv >= early_alert_clear_mv) {
      early_alert_latched = false;
    }
  }

  if (!power_state_initialized) {
    StrHelper::strncpy(previous_power_state, status.state,
                       sizeof(previous_power_state));
    power_state_initialized = true;
    return;
  }
  if (strcmp(previous_power_state, status.state) == 0) return;

  StrHelper::strncpy(previous_power_state, status.state,
                     sizeof(previous_power_state));
  if (strcmp(status.state, "systemoff") == 0) {
    char text[MAX_ALERT_TEXT_LEN + 1];
    snprintf(text, sizeof(text),
             "%s: batterie faible %u mV. Arret LoRa et mise en veille imminents.",
             POWER_ALERT_NODE_LABEL, status.battery_mv);
    // The shutdown alert has priority over a diagnostic/test message because
    // the radio grace period is intentionally short.
    startOperationalAlert(text, true);
  }
}

void MyMesh::sendBootAlert(uint16_t battery_mv, const char* reset_reason,
                           const char* previous_shutdown_reason) {
  char text[MAX_ALERT_TEXT_LEN + 1];
  snprintf(text, sizeof(text),
           "%s demarre: batterie %u mV; reset=%s; arret precedent=%s.",
           POWER_ALERT_NODE_LABEL, battery_mv, reset_reason ? reset_reason : "?",
           previous_shutdown_reason ? previous_shutdown_reason : "?");
  startOperationalAlert(text);
}

void MyMesh::sendGpsFixAlert(uint32_t acquisition_seconds,
                             int32_t satellites) {
  char text[MAX_ALERT_TEXT_LEN + 1];
  snprintf(text, sizeof(text),
           "%s GPS recupere: fix en %lu s; %ld satellite(s); position actualisee.",
           POWER_ALERT_NODE_LABEL, (unsigned long)acquisition_seconds,
           (long)satellites);
  startOperationalAlert(text);
}

void MyMesh::handleAlertCommand(const char* command, char* reply) {
  while (*command == ' ') command++;

  if (*command == '\0' || strcmp(command, "list") == 0 ||
      strcmp(command, "status") == 0) {
    int used = snprintf(reply, 160,
                        "dest=%s; private=%u/%u; early=%u/%u mV",
                        alertDestinationName(),
                        (unsigned)alert_recipients.count(),
                        (unsigned)alert_recipients.capacity(), early_alert_mv,
                        early_alert_clear_mv);
    char hex[mesh::AlertRecipientList<MAX_ALERT_RECIPIENTS>::KEY_HEX_CHARS + 1];
    for (size_t i = 0; i < alert_recipients.count() && used < 159; i++) {
      mesh::AlertRecipientList<MAX_ALERT_RECIPIENTS>::encodeHexKey(
          alert_recipients.keyAt(i), hex);
      used += snprintf(reply + used, 160 - used, "; %u:%.12s...",
                       (unsigned)(i + 1), hex);
    }
    return;
  }

  if (strcmp(command, "public") == 0 || strcmp(command, "private") == 0) {
    const AlertDestination previous = alert_destination;
    alert_destination = strcmp(command, "public") == 0
                            ? AlertDestination::PUBLIC
                            : AlertDestination::PRIVATE;
    if (!saveAlertChannelConfig()) {
      alert_destination = previous;
      strcpy(reply, "ERR - alert destination not saved");
      return;
    }
    snprintf(reply, 160, "OK - battery alerts -> %s",
             alertDestinationName());
    return;
  }

  if (strcmp(command, "channel") == 0) {
    if (!custom_alert_channel_configured) {
      strcpy(reply, "ERR - configure: bat low channel NAME PSK");
      return;
    }
    const AlertDestination previous = alert_destination;
    alert_destination = AlertDestination::CUSTOM_CHANNEL;
    if (!saveAlertChannelConfig()) {
      alert_destination = previous;
      strcpy(reply, "ERR - alert destination not saved");
      return;
    }
    snprintf(reply, 160, "OK - battery alerts -> %s",
             custom_alert_channel_name);
    return;
  }

  if (strncmp(command, "channel ", 8) == 0) {
    const char* definition = command + 8;
    const char* final_space = strrchr(definition, ' ');
    if (!final_space || final_space == definition || !final_space[1]) {
      strcpy(reply, "ERR - bat low channel NAME PSK(base64|hex)");
      return;
    }
    size_t name_len = (size_t)(final_space - definition);
    while (name_len > 0 && definition[name_len - 1] == ' ') name_len--;
    if (name_len == 0 || name_len > MAX_ALERT_CHANNEL_NAME_LEN) {
      strcpy(reply, "ERR - channel name must be 1..31 characters");
      return;
    }
    bool valid_name = true;
    for (size_t i = 0; i < name_len; i++) {
      valid_name = valid_name && (uint8_t)definition[i] >= 32 &&
                   (uint8_t)definition[i] <= 126;
    }
    if (!valid_name) {
      strcpy(reply, "ERR - invalid channel name");
      return;
    }

    const AlertDestination previous_destination = alert_destination;
    const mesh::GroupChannel previous_channel = custom_alert_channel;
    const bool previous_configured = custom_alert_channel_configured;
    char previous_name[sizeof(custom_alert_channel_name)];
    char previous_key[sizeof(custom_alert_channel_key)];
    StrHelper::strncpy(previous_name, custom_alert_channel_name,
                       sizeof(previous_name));
    StrHelper::strncpy(previous_key, custom_alert_channel_key,
                       sizeof(previous_key));

    char name[MAX_ALERT_CHANNEL_NAME_LEN + 1];
    memcpy(name, definition, name_len);
    name[name_len] = '\0';
    if (!configureAlertChannel(custom_alert_channel, final_space + 1,
                               custom_alert_channel_key,
                               sizeof(custom_alert_channel_key))) {
      strcpy(reply, "ERR - PSK must decode to 16/32 bytes (base64 or hex)");
      return;
    }
    StrHelper::strncpy(custom_alert_channel_name, name,
                       sizeof(custom_alert_channel_name));
    custom_alert_channel_configured = true;
    alert_destination = AlertDestination::CUSTOM_CHANNEL;
    if (!saveAlertChannelConfig()) {
      alert_destination = previous_destination;
      custom_alert_channel = previous_channel;
      custom_alert_channel_configured = previous_configured;
      StrHelper::strncpy(custom_alert_channel_name, previous_name,
                         sizeof(custom_alert_channel_name));
      StrHelper::strncpy(custom_alert_channel_key, previous_key,
                         sizeof(custom_alert_channel_key));
      strcpy(reply, "ERR - channel configuration not saved");
      return;
    }
    snprintf(reply, 160, "OK - battery alerts -> %s; PSK hidden",
             custom_alert_channel_name);
    return;
  }

  if (strcmp(command, "threshold") == 0) {
    snprintf(reply, 160, "early=%u mV; clear=%u mV", early_alert_mv,
             early_alert_clear_mv);
    return;
  }

  if (strncmp(command, "threshold ", 10) == 0) {
    const char* argument = command + 10;
    char* end = nullptr;
    const unsigned long warning = strtoul(argument, &end, 10);
    while (end && *end == ' ') end++;
    unsigned long clear = warning + 50;
    if (end && *end) {
      char* clear_end = nullptr;
      clear = strtoul(end, &clear_end, 10);
      if (clear_end == end || *clear_end) {
        strcpy(reply, "ERR - alert threshold MV [CLEAR_MV]");
        return;
      }
    }
    if (warning < 3000 || warning > 4100 ||
        clear < warning + 25 || clear > 4300) {
      strcpy(reply, "ERR - threshold 3000..4100; clear +25..4300");
      return;
    }
    const uint16_t previous_warning = early_alert_mv;
    const uint16_t previous_clear = early_alert_clear_mv;
    early_alert_mv = (uint16_t)warning;
    early_alert_clear_mv = (uint16_t)clear;
    if (!saveAlertConfig()) {
      early_alert_mv = previous_warning;
      early_alert_clear_mv = previous_clear;
      strcpy(reply, "ERR - alert threshold not saved");
      return;
    }
    early_alert_latched = false;
    snprintf(reply, 160, "OK - early=%u mV; clear=%u mV", early_alert_mv,
             early_alert_clear_mv);
    return;
  }

  if (strncmp(command, "get ", 4) == 0) {
    char* end = nullptr;
    const unsigned long number = strtoul(command + 4, &end, 10);
    if (!end || *end || number == 0 || number > alert_recipients.count()) {
      strcpy(reply, "ERR - recipient index");
      return;
    }
    mesh::AlertRecipientList<MAX_ALERT_RECIPIENTS>::encodeHexKey(
        alert_recipients.keyAt(number - 1), reply);
    return;
  }

  if (strncmp(command, "add ", 4) == 0) {
    const char* hex = command + 4;
    uint8_t key[PUB_KEY_SIZE];
    if (!mesh::AlertRecipientList<MAX_ALERT_RECIPIENTS>::decodeHexKey(hex, key)) {
      strcpy(reply, "ERR - key must be 64 hex chars");
      return;
    }
    if (alert_recipients.contains(key)) {
      strcpy(reply, "ERR - recipient already present");
      return;
    }
    if (alert_recipients.count() >= alert_recipients.capacity()) {
      strcpy(reply, "ERR - recipient list full");
      return;
    }
    auto previous = alert_recipients;
    alert_recipients.add(key);
    if (!saveAlertRecipients()) {
      alert_recipients = previous;
      strcpy(reply, "ERR - recipient list not saved");
      return;
    }
    snprintf(reply, 160, "OK - recipient %u added",
             (unsigned)alert_recipients.count());
    return;
  }

  if (strncmp(command, "remove ", 7) == 0) {
    const char* argument = command + 7;
    auto previous = alert_recipients;
    bool removed = false;
    char* end = nullptr;
    const unsigned long number = strtoul(argument, &end, 10);
    if (end != argument && *end == '\0' && number > 0) {
      removed = alert_recipients.removeAt(number - 1);
    } else {
      removed = alert_recipients.removeHex(argument);
    }
    if (!removed) {
      strcpy(reply, "ERR - recipient not found");
      return;
    }
    if (!saveAlertRecipients()) {
      alert_recipients = previous;
      strcpy(reply, "ERR - recipient list not saved");
      return;
    }
    strcpy(reply, "OK - recipient removed");
    return;
  }

  if (strcmp(command, "clear") == 0) {
    auto previous = alert_recipients;
    alert_recipients.clear();
    if (!saveAlertRecipients()) {
      alert_recipients = previous;
      strcpy(reply, "ERR - recipient list not saved");
      return;
    }
    strcpy(reply, "OK - recipient list cleared");
    return;
  }

  if (strcmp(command, "test") == 0) {
    char text[MAX_ALERT_TEXT_LEN + 1];
    snprintf(text, sizeof(text),
             "%s test alerte: radio operationnelle, batterie %u mV.",
             POWER_ALERT_NODE_LABEL, board.getBattMilliVolts());
    const uint8_t sent = startOperationalAlert(text);
    snprintf(reply, 160,
             sent ? "OK - test queued -> %s" : "ERR - no alert queued",
             alertDestinationName());
    return;
  }

  // Once a custom channel has been provisioned, its human-readable name is a
  // direct selector: for example `bat low FR SV`.
  if (custom_alert_channel_configured &&
      strcmp(command, custom_alert_channel_name) == 0) {
    const AlertDestination previous = alert_destination;
    alert_destination = AlertDestination::CUSTOM_CHANNEL;
    if (!saveAlertChannelConfig()) {
      alert_destination = previous;
      strcpy(reply, "ERR - alert destination not saved");
      return;
    }
    snprintf(reply, 160, "OK - battery alerts -> %s",
             custom_alert_channel_name);
    return;
  }

  strcpy(reply, "ERR - bat low status|public|private|channel NAME PSK|NAME|test");
}
#endif

MyMesh::MyMesh(mesh::MainBoard &board, mesh::Radio &radio, mesh::MillisecondClock &ms, mesh::RNG &rng,
               mesh::RTCClock &rtc, mesh::MeshTables &tables)
    : mesh::Mesh(radio, ms, rng, rtc, *new StaticPoolPacketManager(32), tables),
      region_map(key_store), temp_map(key_store),
      _cli(board, rtc, sensors, region_map, acl, &_prefs, this),
      telemetry(MAX_PACKET_PAYLOAD - 4),
      discover_limiter(4, 120),  // max 4 every 2 minutes
      anon_limiter(4, 180)   // max 4 every 3 minutes
#if defined(WITH_RS232_BRIDGE)
      , bridge(&_prefs, WITH_RS232_BRIDGE, _mgr, &rtc)
#endif
#if defined(WITH_ESPNOW_BRIDGE)
      , bridge(&_prefs, _mgr, &rtc)
#endif
{
  last_millis = 0;
  uptime_millis = 0;
  next_local_advert = next_flood_advert = 0;
  dirty_contacts_expiry = 0;
  set_radio_at = revert_radio_at = 0;
  _logging = false;
  region_load_active = false;
  recv_pkt_region = NULL;

#if MAX_NEIGHBOURS
  memset(neighbours, 0, sizeof(neighbours));
#endif

  // defaults
  _prefs.airtime_factor = 1.0;
  _prefs.rx_delay_base = 0.0f;   // turn off by default, was 10.0;
  _prefs.tx_delay_factor = 0.5f; // was 0.25f
  _prefs.direct_tx_delay_factor = 0.3f; // was 0.2
  StrHelper::strncpy(_prefs.node_name, ADVERT_NAME, sizeof(_prefs.node_name));
  _prefs.node_lat = ADVERT_LAT;
  _prefs.node_lon = ADVERT_LON;
  StrHelper::strncpy(_prefs.password, ADMIN_PASSWORD, sizeof(_prefs.password));
  _prefs.freq = LORA_FREQ;
  _prefs.sf = LORA_SF;
  _prefs.bw = LORA_BW;
  _prefs.cr = LORA_CR;
  _prefs.tx_power_dbm = LORA_TX_POWER;
  _prefs.advert_interval = 1;        // default to 2 minutes for NEW installs
  _prefs.flood_advert_interval = 47; // 47 hours
  _prefs.flood_max = 64;
  _prefs.flood_max_unscoped = 64;
  _prefs.flood_max_advert = 8;
  _prefs.interference_threshold = 0; // disabled
  _prefs.cad_enabled = 0;            // hardware CAD before TX (off by default; 'set cad on')

  // bridge defaults
  _prefs.bridge_enabled = 1;    // enabled
  _prefs.bridge_delay   = 500;  // milliseconds
  _prefs.bridge_pkt_src = 0;    // logTx
  _prefs.bridge_baud = 115200;  // baud rate
  _prefs.bridge_channel = 1;    // channel 1

  StrHelper::strncpy(_prefs.bridge_secret, "LVSITANOS", sizeof(_prefs.bridge_secret));

  // GPS defaults
  _prefs.gps_enabled = 0;
  _prefs.gps_interval = 0;
  _prefs.advert_loc_policy = ADVERT_LOC_PREFS;

  _prefs.adc_multiplier = 0.0f; // 0.0f means use default board multiplier

#if defined(USE_SX1262) || defined(USE_SX1268)
#ifdef SX126X_RX_BOOSTED_GAIN
  _prefs.rx_boosted_gain = SX126X_RX_BOOSTED_GAIN;
#else
  _prefs.rx_boosted_gain = 1; // enabled by default;
#endif
#endif
  _prefs.radio_fem_rxgain = 1;
  _prefs.radio_fem_txgain = 0;

  pending_discover_tag = 0;
  pending_discover_until = 0;

  memset(default_scope.key, 0, sizeof(default_scope.key));
}

void MyMesh::begin(FILESYSTEM *fs) {
  mesh::Mesh::begin();
  _fs = fs;
  // load persisted prefs
  _cli.loadPrefs(_fs);
  acl.load(_fs, self_id);
#if defined(P1_POWER_ALERTS)
  if (!loadAlertRecipients()) {
    Serial.println("ERROR: alert recipient list unavailable or corrupt");
  }
  if (!loadAlertConfig()) {
    Serial.println("ERROR: alert threshold config unavailable or corrupt; defaults active");
  }
  if (!loadAlertChannelConfig()) {
    Serial.println("ERROR: alert channel config corrupt; private destination active");
  }
#endif
#if defined(P1_EVENT_LOG)
  if (!loadGpsPowerGuard()) {
    Serial.println("ERROR: GPS powerguard config corrupt; economy guard active");
  }
#endif
  // TODO: key_store.begin();
  region_map.load(_fs);

  // establish default-scope
  {
    RegionEntry* r = region_map.getDefaultRegion();
    if (r) {
      region_map.getTransportKeysFor(*r, &default_scope, 1);
    } else {
#ifdef DEFAULT_FLOOD_SCOPE_NAME
      r = region_map.findByName(DEFAULT_FLOOD_SCOPE_NAME);
      if (r == NULL) {
        r = region_map.putRegion(DEFAULT_FLOOD_SCOPE_NAME, 0);  // auto-create the default scope region
        if (r) { r->flags = 0; }   // Allow-flood
      }
      if (r) {
        region_map.setDefaultRegion(r);
        region_map.getTransportKeysFor(*r, &default_scope, 1);
      }
#endif
    }
  }

#if defined(WITH_BRIDGE)
  if (_prefs.bridge_enabled) {
    bridge.begin();
  }
#endif

  radio_driver.setParams(_prefs.freq, _prefs.bw, _prefs.sf, _prefs.cr);
  radio_driver.setTxPower(_prefs.tx_power_dbm);
#ifdef DUAL_SX1262_REPEATER
  radio_driver.loadConfig();
#endif

  radio_driver.setRxBoostedGainMode(_prefs.rx_boosted_gain);
  MESH_DEBUG_PRINTLN("RX Boosted Gain Mode: %s",
                     radio_driver.getRxBoostedGainMode() ? "Enabled" : "Disabled");
  board.setLoRaFemLnaEnabled(_prefs.radio_fem_rxgain);
  board.setLoRaFemPaGainEnabled(_prefs.radio_fem_txgain);

  updateAdvertTimer();
  updateFloodAdvertTimer();

  board.setAdcMultiplier(_prefs.adc_multiplier);

#if ENV_INCLUDE_GPS == 1
  applyGpsPrefs();
#if defined(P1_EVENT_LOG)
  if (gpsContinuousPersisted()) {
    char restored[96];
    sensors.handleGpsOverrideCommand("on", restored, sizeof(restored));
  }
#endif
#endif
}

void MyMesh::sendFloodScoped(const TransportKey& scope, mesh::Packet* pkt, uint32_t delay_millis, uint8_t path_hash_size) {
  if (scope.isNull()) {
    sendFlood(pkt, delay_millis, path_hash_size);
  } else {
    uint16_t codes[2];
    codes[0] = scope.calcTransportCode(pkt);
    codes[1] = 0;  // REVISIT: set to 'home' Region, for sender/return region?
    sendFlood(pkt, codes, delay_millis, path_hash_size);
  }
}

void MyMesh::applyTempRadioParams(float freq, float bw, uint8_t sf, uint8_t cr, int timeout_mins) {
  set_radio_at = futureMillis(2000); // give CLI reply some time to be sent back, before applying temp radio params
  pending_freq = freq;
  pending_bw = bw;
  pending_sf = sf;
  pending_cr = cr;

  revert_radio_at = futureMillis(2000 + timeout_mins * 60 * 1000); // schedule when to revert radio params
}

bool MyMesh::formatFileSystem() {
#if defined(NRF52_PLATFORM) || defined(STM32_PLATFORM)
  return InternalFS.format();
#elif defined(RP2040_PLATFORM)
  return LittleFS.format();
#elif defined(ESP32)
  return SPIFFS.format();
#else
#error "need to implement file system erase"
  return false;
#endif
}

void MyMesh::sendSelfAdvertisement(int delay_millis, bool flood) {
  mesh::Packet *pkt = createSelfAdvert();
  if (pkt) {
    if (flood) {
      sendFloodScoped(default_scope, pkt, delay_millis, _prefs.path_hash_mode + 1);
    } else {
      sendZeroHop(pkt, delay_millis);
    }
  } else {
    MESH_DEBUG_PRINTLN("ERROR: unable to create advertisement packet!");
  }
}

void MyMesh::updateAdvertTimer() {
  if (_prefs.advert_interval > 0) { // schedule local advert timer
    next_local_advert = futureMillis(((uint32_t)_prefs.advert_interval) * 2 * 60 * 1000);
  } else {
    next_local_advert = 0; // stop the timer
  }
}

void MyMesh::updateFloodAdvertTimer() {
  if (_prefs.flood_advert_interval > 0) { // schedule flood advert timer
    next_flood_advert = futureMillis(((uint32_t)_prefs.flood_advert_interval) * 60 * 60 * 1000);
  } else {
    next_flood_advert = 0; // stop the timer
  }
}

void MyMesh::dumpLogFile() {
#if defined(RP2040_PLATFORM)
  File f = _fs->open(PACKET_LOG_FILE, "r");
#else
  File f = _fs->open(PACKET_LOG_FILE);
#endif
  if (f) {
    while (f.available()) {
      int c = f.read();
      if (c < 0) break;
      Serial.print((char)c);
    }
    f.close();
  }
}

void MyMesh::setTxPower(int8_t power_dbm) {
#ifdef DUAL_SX1262_REPEATER
  radio_driver.setCommonTxPower(power_dbm);
#else
  radio_driver.setTxPower(power_dbm);
#endif
}

bool MyMesh::setRxBoostedGain(bool enable) {
  return radio_driver.setRxBoostedGainMode(enable);
}

#if defined(USE_LR2021)
bool MyMesh::configSideDetectors(const uint8_t sideDetSFs[], uint8_t num, float bw) {
  return radio_driver.configSideDetectors(sideDetSFs, num, bw);
}
#endif

void MyMesh::formatNeighborsReply(char *reply) {
  char *dp = reply;

#if MAX_NEIGHBOURS
  // create copy of neighbours list, skipping empty entries so we can sort it separately from main list
  int16_t neighbours_count = 0;
  NeighbourInfo* sorted_neighbours[MAX_NEIGHBOURS];
  for (int i = 0; i < MAX_NEIGHBOURS; i++) {
    auto neighbour = &neighbours[i];
    if (neighbour->heard_timestamp > 0) {
      sorted_neighbours[neighbours_count] = neighbour;
      neighbours_count++;
    }
  }

  // sort neighbours newest to oldest
  std::sort(sorted_neighbours, sorted_neighbours + neighbours_count, [](const NeighbourInfo* a, const NeighbourInfo* b) {
    return a->heard_timestamp > b->heard_timestamp; // desc
  });

  for (int i = 0; i < neighbours_count && dp - reply < 134; i++) {
    NeighbourInfo *neighbour = sorted_neighbours[i];

    // add new line if not first item
    if (i > 0) *dp++ = '\n';

    char hex[10];
    // get 4 bytes of neighbour id as hex
    mesh::Utils::toHex(hex, neighbour->id.pub_key, 4);

    // add next neighbour
    uint32_t secs_ago = getRTCClock()->getCurrentTime() - neighbour->heard_timestamp;
    sprintf(dp, "%s:%d:%d", hex, secs_ago, neighbour->snr);
    while (*dp)
      dp++; // find end of string
  }
#endif
  if (dp == reply) { // no neighbours, need empty response
    strcpy(dp, "-none-");
    dp += 6;
  }
  *dp = 0; // null terminator
}

void MyMesh::removeNeighbor(const uint8_t *pubkey, int key_len) {
#if MAX_NEIGHBOURS
  for (int i = 0; i < MAX_NEIGHBOURS; i++) {
    NeighbourInfo *neighbour = &neighbours[i];
    if (memcmp(neighbour->id.pub_key, pubkey, key_len) == 0) {
      neighbours[i] = NeighbourInfo(); // clear neighbour entry
    }
  }
#endif
}

void MyMesh::startRegionsLoad() {
  temp_map.resetFrom(region_map);   // rebuild regions in a temp instance
  memset(load_stack, 0, sizeof(load_stack));
  load_stack[0] = &temp_map.getWildcard();
  region_load_active = true;
}

bool MyMesh::saveRegions() {
  return region_map.save(_fs);
}

void MyMesh::onDefaultRegionChanged(const RegionEntry* r) {
  if (r) {
    region_map.getTransportKeysFor(*r, &default_scope, 1);
  } else {
    memset(default_scope.key, 0, sizeof(default_scope.key));
  }
}

void MyMesh::formatStatsReply(char *reply) {
  StatsFormatHelper::formatCoreStats(reply, board, *_ms, _err_flags, _mgr);
}

void MyMesh::formatRadioStatsReply(char *reply) {
  StatsFormatHelper::formatRadioStats(reply, _radio, radio_driver, getTotalAirTime(), getReceiveAirTime());
}

void MyMesh::formatPacketStatsReply(char *reply) {
  StatsFormatHelper::formatPacketStats(reply, radio_driver, getNumSentFlood(), getNumSentDirect(), 
                                       getNumRecvFlood(), getNumRecvDirect());
}

void MyMesh::saveIdentity(const mesh::LocalIdentity &new_id) {
#if defined(NRF52_PLATFORM) || defined(STM32_PLATFORM)
  IdentityStore store(*_fs, "");
#elif defined(ESP32)
  IdentityStore store(*_fs, "/identity");
#elif defined(RP2040_PLATFORM)
  IdentityStore store(*_fs, "/identity");
#else
#error "need to define saveIdentity()"
#endif
  store.save("_main", new_id);
}

void MyMesh::clearStats() {
  radio_driver.resetStats();
  resetStats();
  ((SimpleMeshTables *)getTables())->resetStats();
}

void MyMesh::handleCommand(uint32_t sender_timestamp, char *command, char *reply) {
  size_t reply_capacity = 160;
  if (region_load_active) {
    if (StrHelper::isBlank(command)) {  // empty/blank line, signal to terminate 'load' operation
      region_map = temp_map;  // copy over the temp instance as new current map
      region_load_active = false;

      sprintf(reply, "OK - loaded %d regions", region_map.getCount());
    } else {
      char *np = command;
      while (*np == ' ') np++;   // skip indent
      int indent = np - command;

      char *ep = np;
      while (RegionMap::is_name_char(*ep)) ep++;
      if (*ep) { *ep++ = 0; }  // set null terminator for end of name

      while (*ep && *ep != 'F') ep++;  // look for (optional) flags

      if (indent > 0 && indent < 8 && strlen(np) > 0) {
        auto parent = load_stack[indent - 1];
        if (parent) {
          auto old = region_map.findByName(np);
          auto nw = temp_map.putRegion(np, parent->id, old ? old->id : 0);  // carry-over the current ID (if name already exists)
          if (nw) {
            nw->flags = old ? old->flags : (*ep == 'F' ? 0 : REGION_DENY_FLOOD);   // carry-over flags from curr

            load_stack[indent] = nw;  // keep pointers to parent regions, to resolve parent_id's
          }
        }
      }
      reply[0] = 0;
    }
    return;
  }

  while (*command == ' ') command++; // skip leading spaces

  if (strlen(command) > 4 && command[2] == '|') { // optional prefix (for companion radio CLI)
    memcpy(reply, command, 3);                    // reflect the prefix back
    reply += 3;
    reply_capacity -= 3;
    command += 3;
  }

#ifdef DUAL_SX1262_REPEATER
  if (radio_driver.handleCommand(command, reply, reply_capacity)) {
    return;
  }
#endif

#if defined(P1_POWER_ALERTS)
  if (strncmp(command, "bat low", 7) == 0 &&
      (command[7] == '\0' || command[7] == ' ')) {
    handleAlertCommand(command + 7, reply);
    return;
  }
  if (strncmp(command, "alert", 5) == 0 &&
      (command[5] == '\0' || command[5] == ' ')) {
    handleAlertCommand(command + 5, reply);
    return;
  }
#endif

#if defined(P1_EVENT_LOG)
  if (strncmp(command, "gps powerguard", 14) == 0 &&
      (command[14] == 0 || command[14] == ' ')) {
    handleGpsPowerGuardCommand(command + 14, reply);
    return;
  }
  if (sender_timestamp == 0 && strncmp(command, "gps override", 12) == 0 &&
      (command[12] == 0 || command[12] == ' ')) {
    const char* argument = command + 12;
    while (*argument == ' ') argument++;
    const bool set_continuous = strcmp(argument, "on") == 0;
    const bool set_daily = strcmp(argument, "off") == 0;
    char* duration_end = nullptr;
    const unsigned long duration_hours = strtoul(argument, &duration_end, 10);
    const bool set_timed = duration_end != argument &&
                           strcmp(duration_end, "h") == 0 &&
                           duration_hours >= 1 && duration_hours <= 168;

    if ((set_continuous || set_daily || set_timed) &&
        !persistGpsContinuous(set_continuous)) {
      strcpy(reply, "ERR - seasonal GPS setting not saved");
      return;
    }

    if (!sensors.handleGpsOverrideCommand(argument, reply, 160)) {
      strcpy(reply, "ERR - GPS override unsupported");
    } else if (set_continuous) {
      strncat(reply, "; saved summer mode", 159 - strlen(reply));
    } else if (set_daily) {
      strncat(reply, "; saved winter mode", 159 - strlen(reply));
    } else if (strcmp(argument, "status") == 0 || *argument == 0) {
      strncat(reply, gpsContinuousPersisted() ? "; summer mode saved"
                                              : "; winter mode saved",
              159 - strlen(reply));
    }
    return;
  } else if (sender_timestamp == 0 && strcmp(command, "eventlog clear") == 0) {
    strcpy(reply, p1_event_journal.clear() ? "OK - event log cleared"
                                           : "ERR - event log clear failed");
    return;
  } else if (sender_timestamp == 0 && strcmp(command, "eventlog status") == 0) {
    p1_event_journal.formatStatus(reply, 160);
    return;
  } else if (sender_timestamp == 0 && strcmp(command, "eventlog") == 0) {
    p1_event_journal.dump(Serial);
    strcpy(reply, "EOF");
    return;
  }
#endif

  // handle ACL related commands
  if (memcmp(command, "setperm ", 8) == 0) {   // format:  setperm {pubkey-hex} {permissions-int8}
    char* hex = &command[8];
    char* sp = strchr(hex, ' ');   // look for separator char
    if (sp == NULL) {
      strcpy(reply, "Err - bad params");
    } else {
      *sp++ = 0;   // replace space with null terminator

      uint8_t pubkey[PUB_KEY_SIZE];
      int hex_len = min(sp - hex, PUB_KEY_SIZE*2);
      if (mesh::Utils::fromHex(pubkey, hex_len / 2, hex)) {
        uint8_t perms = atoi(sp);
        if (acl.applyPermissions(self_id, pubkey, hex_len / 2, perms)) {
          dirty_contacts_expiry = futureMillis(LAZY_CONTACTS_WRITE_DELAY);   // trigger acl.save()
          strcpy(reply, "OK");
        } else {
          strcpy(reply, "Err - invalid params");
        }
      } else {
        strcpy(reply, "Err - bad pubkey");
      }
    }
  } else if (sender_timestamp == 0 && strcmp(command, "get acl") == 0) {
    Serial.println("ACL:");
    for (int i = 0; i < acl.getNumClients(); i++) {
      auto c = acl.getClientByIdx(i);
      if (c->permissions == 0) continue;  // skip deleted (or guest) entries

      Serial.printf("%02X ", c->permissions);
      mesh::Utils::printHex(Serial, c->id.pub_key, PUB_KEY_SIZE);
      Serial.printf("\n");
    }
    reply[0] = 0;
  } else if (memcmp(command, "discover.neighbors", 18) == 0) {
    const char* sub = command + 18;
    while (*sub == ' ') sub++;
    if (*sub != 0) {
      strcpy(reply, "Err - discover.neighbors has no options");
    } else {
      sendNodeDiscoverReq();
      strcpy(reply, "OK - Discover sent");
    }
  } else{
    _cli.handleCommand(sender_timestamp, command, reply);  // common CLI commands
  }
}

void MyMesh::loop() {
#ifdef WITH_BRIDGE
  bridge.loop();
#endif

  mesh::Mesh::loop();
#if defined(P1_POWER_ALERTS)
  serviceOperationalAlerts();
#endif

  if (next_flood_advert && millisHasNowPassed(next_flood_advert)) {
    mesh::Packet *pkt = createSelfAdvert();
    uint32_t delay_millis = 0;
    if (pkt) sendFloodScoped(default_scope, pkt, delay_millis, _prefs.path_hash_mode + 1);

    updateFloodAdvertTimer(); // schedule next flood advert
    updateAdvertTimer();      // also schedule local advert (so they don't overlap)
  } else if (next_local_advert && millisHasNowPassed(next_local_advert)) {
    mesh::Packet *pkt = createSelfAdvert();
    if (pkt) sendZeroHop(pkt);

    updateAdvertTimer(); // schedule next local advert
  }

  if (set_radio_at && millisHasNowPassed(set_radio_at)) { // apply pending (temporary) radio params
    set_radio_at = 0;                                     // clear timer
    radio_driver.setParams(pending_freq, pending_bw, pending_sf, pending_cr);
    MESH_DEBUG_PRINTLN("Temp radio params");
  }

  if (revert_radio_at && millisHasNowPassed(revert_radio_at)) { // revert radio params to orig
    revert_radio_at = 0;                                        // clear timer
    radio_driver.setParams(_prefs.freq, _prefs.bw, _prefs.sf, _prefs.cr);
    MESH_DEBUG_PRINTLN("Radio params restored");
  }

  // is pending dirty contacts write needed?
  if (dirty_contacts_expiry && millisHasNowPassed(dirty_contacts_expiry)) {
    // On nRF52 an InternalFS erase may block the SoftDevice.  Never begin it
    // while a radio packet is queued; defer briefly so TX current and flash
    // erase do not overlap and so the mesh reply can leave first.
    if (_mgr->getOutboundTotal() == 0) {
      acl.save(_fs);
      dirty_contacts_expiry = 0;
    } else {
      dirty_contacts_expiry = futureMillis(1000);
    }
  }

  // update uptime
  uint32_t now = millis();
  uptime_millis += now - last_millis;
  last_millis = now;
}

// To check if there is pending work
bool MyMesh::hasPendingWork() const {
#if defined(WITH_BRIDGE)
  if (bridge.isRunning()) return true;  // bridge needs WiFi radio, can't sleep
#endif
  return _mgr->getOutboundTotal() > 0;
}
