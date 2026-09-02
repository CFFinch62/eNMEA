#include "NmeaSource.h"

#include <Arduino.h>
#include <WiFi.h>

#include "Product.h"

namespace {
constexpr unsigned long RECONNECT_INTERVAL_MS = 3000;
// How long a socket can be up with nothing arriving before the dashboard stops
// claiming CONNECTED. Long enough not to flicker on a slow feed (a multiplexer
// sending one sentence every few seconds is normal), short enough to notice a
// dead link inside a couple of dashboard redraws.
constexpr unsigned long STALL_AFTER_MS = 10000;
// Bounded so a wrong host doesn't stall loop() for the stack's 30s default -
// the dashboard would stop redrawing and the button gestures stop responding.
constexpr int32_t TCP_CONNECT_TIMEOUT_MS = 4000;
char lineBuf[NMEA_MAX_SENTENCE_LEN + 1];  // reused every call, not per-byte
}  // namespace

const char* NmeaSource::stateText() const {
  switch (state_) {
    case SourceState::Listening:
      return "LISTENING";
    case SourceState::Connecting:
      return "CONNECTING";
    case SourceState::Connected:
      return "CONNECTED";
    case SourceState::NoData:
      return "NO DATA";
    case SourceState::Failed:
      return "FAILED";
    case SourceState::Idle:
    default:
      return "IDLE";
  }
}

void NmeaSource::end() {
  tcp_.stop();
  udp_.stop();
  state_ = SourceState::Idle;
  lastRxMs_ = 0;
  bytesReceived_ = 0;
  connectAttempts_ = 0;
  lastReconnectAttemptMs_ = 0;
  lineReader_ = NmeaLineReader{};  // drop any half-received sentence
}

bool NmeaSource::begin(const NmeaProfile& profile) {
  settings_ = profile;
  lastReconnectAttemptMs_ = 0;
  lastRxMs_ = 0;
  bytesReceived_ = 0;
  connectAttempts_ = 0;

  if (settings_.protocol == NmeaProfile::Protocol::UDP) {
    if (udp_.begin(settings_.port)) {
      Serial.printf(LOG_TAG "UDP: listening on port %u for broadcast traffic.\n", settings_.port);
      Serial.println(LOG_TAG "UDP: the Host setting is ignored in this mode - the device does not dial out.");
      Serial.println(LOG_TAG "      If your source is a TCP server, switch to TCP at the setup page.");
      state_ = SourceState::Listening;
      return true;
    }
    Serial.printf(LOG_TAG "UDP: begin(%u) FAILED - port busy or out of sockets.\n", settings_.port);
    state_ = SourceState::Failed;
    return false;
  }

  if (settings_.host[0] == '\0') {
    Serial.println(LOG_TAG "TCP: no Host configured. Set it at the setup page, or switch to UDP.");
    state_ = SourceState::Failed;
    return false;
  }

  Serial.printf(LOG_TAG "TCP: will dial out to %s:%u as a client.\n", settings_.host, settings_.port);
  state_ = SourceState::Connecting;
  return true;
}

void NmeaSource::handleByte(uint8_t c, NmeaData& data, SentenceTable& table) {
  ++bytesReceived_;
  if (!lineReader_.feed(c, lineBuf)) return;

  const NmeaParser::Result result = parser_.parseLine(lineBuf, data);
  if (!result.hasAddress) return;

  SentenceStatus* status = table.findOrAdd(result.sentenceId);
  if (status == nullptr) return;  // table full; still-tracked IDs keep updating

  if (result.checksumValid) {
    status->validCount++;
    status->lastValidMs = millis();
  } else {
    status->checksumFailCount++;
  }
}

// Demote CONNECTED to NO DATA once the feed has been quiet too long. Only
// meaningful after something has actually arrived; before that the state is
// LISTENING/CONNECTING, which already says "nothing yet".
void NmeaSource::noteStall() {
  if (lastRxMs_ == 0) return;
  if (millis() - lastRxMs_ > STALL_AFTER_MS) state_ = SourceState::NoData;
}

void NmeaSource::pollUdp(NmeaData& data, SentenceTable& table) {
  const int packetSize = udp_.parsePacket();
  if (packetSize <= 0) {
    // No packet this tick. Previously `connected_` latched true on the first
    // packet and never cleared, so a dead feed still read CONNECTED forever.
    if (lastRxMs_ == 0) {
      if (state_ != SourceState::Failed) state_ = SourceState::Listening;
    } else {
      noteStall();
    }
    return;
  }

  if (state_ != SourceState::Connected) {
    Serial.printf(LOG_TAG "UDP: first packet from %s (%d bytes)\n", udp_.remoteIP().toString().c_str(), packetSize);
  }
  lastRxMs_ = millis();
  state_ = SourceState::Connected;
  while (udp_.available() > 0) {
    handleByte(static_cast<uint8_t>(udp_.read()), data, table);
  }
}

void NmeaSource::pollTcp(NmeaData& data, SentenceTable& table) {
  if (settings_.host[0] == '\0') return;  // nothing to dial; begin() already said so

  if (tcp_.connected()) {
    bool gotBytes = false;
    while (tcp_.available() > 0) {
      handleByte(static_cast<uint8_t>(tcp_.read()), data, table);
      gotBytes = true;
    }
    if (gotBytes) {
      lastRxMs_ = millis();
      state_ = SourceState::Connected;
    } else if (lastRxMs_ == 0) {
      state_ = SourceState::Connected;  // socket up, server hasn't sent yet
    } else {
      noteStall();
    }
    return;
  }

  if (state_ == SourceState::Connected || state_ == SourceState::NoData) {
    Serial.println(LOG_TAG "TCP: link dropped by the far end - will retry.");
  }

  const unsigned long now = millis();
  if (connectAttempts_ > 0 && now - lastReconnectAttemptMs_ < RECONNECT_INTERVAL_MS) {
    return;
  }
  lastReconnectAttemptMs_ = now;
  ++connectAttempts_;

  // Release the previous socket before dialing again. A WiFiClient that failed
  // to connect (or was dropped) still owns its lwIP file descriptor, and the
  // stack only has a handful - CONFIG_LWIP_MAX_SOCKETS defaults to 10 - so
  // leaking one per 3-second retry wedges every attempt after the first few
  // with no error that distinguishes it from "server not there".
  tcp_.stop();

  if (state_ != SourceState::Failed) state_ = SourceState::Connecting;

  Serial.printf(LOG_TAG "TCP: attempt %d -> %s:%u (device %s, gw %s)\n", connectAttempts_, settings_.host,
                settings_.port, WiFi.localIP().toString().c_str(), WiFi.gatewayIP().toString().c_str());

  if (tcp_.connect(settings_.host, settings_.port, TCP_CONNECT_TIMEOUT_MS)) {
    Serial.println(LOG_TAG "TCP: connected.");
    state_ = SourceState::Connected;
    return;
  }

  state_ = SourceState::Failed;
  Serial.printf(LOG_TAG "TCP: connect to %s:%u FAILED.\n", settings_.host, settings_.port);
  // The three causes that account for essentially every failure here, in the
  // order they're worth checking. Printed on the first attempt and then every
  // 10th, so a long retry loop doesn't bury the rest of the log.
  if (connectAttempts_ == 1 || connectAttempts_ % 10 == 0) {
    Serial.println(LOG_TAG "  1. Is the server actually running and listening on that port?");
    Serial.println(LOG_TAG "     On the host: ss -ltn | grep <port>");
    Serial.printf(LOG_TAG "  2. Is %s still the right address? A laptop's IP changes between networks -\n",
                  settings_.host);
    Serial.println(LOG_TAG "     a host saved on one network will never answer on another.");
    Serial.println(LOG_TAG "  3. Same subnet? Compare the device IP/gateway above with the server's.");
    Serial.println(LOG_TAG "     AP client isolation and a 2.4GHz/5GHz split both break this silently.");
  }
}

void NmeaSource::poll(NmeaData& data, SentenceTable& table) {
  if (settings_.protocol == NmeaProfile::Protocol::UDP) {
    pollUdp(data, table);
  } else {
    pollTcp(data, table);
  }
}
