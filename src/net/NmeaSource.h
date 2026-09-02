#pragma once

#include <WiFiClient.h>
#include <WiFiUdp.h>

#include "nmea/NmeaLineReader.h"
#include "nmea/NmeaParser.h"
#include "nmea/NmeaTypes.h"
#include "settings/AppSettings.h"

// What the source is actually doing, so the dashboard can say something more
// useful than CONNECTED/WAITING. The distinction that matters in practice is
// "the socket is fine but nothing is arriving" (NoData) vs. "the socket never
// came up" (Failed) - those have completely different causes and the old
// two-state display couldn't tell them apart.
enum class SourceState : uint8_t {
  Idle,        // begin() not called yet
  Listening,   // UDP socket open, no packet has ever arrived
  Connecting,  // TCP dial-out in progress / between retries
  Connected,   // socket up and bytes arriving
  NoData,      // socket up but nothing received for STALL_AFTER_MS
  Failed,      // UDP bind failed, or the TCP connect attempt was refused
};

// Owns the socket (TCP client or UDP listener, per AppSettings::Protocol)
// and feeds whatever bytes arrive into NmeaLineReader -> NmeaParser,
// updating the shared NmeaData/SentenceTable in place.
//
// UDP and TCP behave differently on purpose, matching how real NMEA-over-IP
// multiplexers work: UDP listens locally on `port` for broadcast traffic
// (`host` is unused and ignored), TCP dials out to `host:port` as a client.
// Getting this backwards is the single most common reason no data shows up.
class NmeaSource {
 public:
  bool begin(const NmeaProfile& profile);

  // Closes whatever socket is open and resets state, so a different profile can
  // be applied without rebooting. Leaving the old socket open would hold an
  // lwIP descriptor and, for TCP, keep the previous device connected.
  void end();

  // Call every loop() iteration. Reads whatever is available without
  // blocking, retries a dropped/never-established connection periodically.
  void poll(NmeaData& data, SentenceTable& table);

  SourceState state() const { return state_; }
  const char* stateText() const;
  bool isConnected() const { return state_ == SourceState::Connected; }

  // millis() of the last byte received from the source, 0 if never.
  unsigned long lastRxMs() const { return lastRxMs_; }
  uint32_t bytesReceived() const { return bytesReceived_; }

 private:
  void pollTcp(NmeaData& data, SentenceTable& table);
  void pollUdp(NmeaData& data, SentenceTable& table);
  void handleByte(uint8_t c, NmeaData& data, SentenceTable& table);
  void noteStall();

  NmeaProfile settings_;
  WiFiClient tcp_;
  WiFiUDP udp_;
  NmeaLineReader lineReader_;
  NmeaParser parser_;
  SourceState state_ = SourceState::Idle;
  unsigned long lastReconnectAttemptMs_ = 0;
  unsigned long lastRxMs_ = 0;
  uint32_t bytesReceived_ = 0;
  int connectAttempts_ = 0;
};
