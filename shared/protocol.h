// UFO Device protocol - ESP-NOW packet framing (single source of truth).
//
// Wire format (little endian, explicit serialization; structs are never sent raw):
//
//   off size field
//   0   2    magic          (0x5546)
//   2   1    version        (1)
//   3   1    source_id
//   4   1    destination_id
//   5   1    message_type   (MsgType)
//   6   2    sequence
//   8   4    session_id
//   12  4    command_id     (COMMAND: command id / EVENT: event id / ACK: acked id)
//   16  1    payload_length (0..47)
//   17  n    payload
//
// NOTE: the original spec says "header 16 bytes, max 64 bytes", but the listed
// fields add up to 17 bytes. The field list wins: header = 17 B, payload <= 47 B,
// total <= 64 B. See docs/protocol.md.
//
// Pure C++ (no Arduino dependency) so it is unit-tested on the host.
#pragma once

#include <stddef.h>
#include <stdint.h>

namespace ufo {

constexpr uint16_t kMagic = 0x5546;
constexpr uint8_t kProtocolVersion = 1;
constexpr size_t kHeaderSize = 17;
constexpr size_t kMaxPacketSize = 64;
constexpr size_t kMaxPayload = kMaxPacketSize - kHeaderSize;  // 47

enum class MsgType : uint8_t {
  Command = 0x01,
  Event = 0x02,
  Ack = 0x03,
  Status = 0x04,
  Heartbeat = 0x05,
  Error = 0x06,
};

bool isValidMsgType(uint8_t t);
const char* msgTypeName(MsgType t);

struct Header {
  uint16_t magic = kMagic;
  uint8_t version = kProtocolVersion;
  uint8_t source = 0;
  uint8_t destination = 0;
  MsgType type = MsgType::Command;
  uint16_t sequence = 0;
  uint32_t session = 0;
  uint32_t txn = 0;  // command_id / event_id / acked id
  uint8_t payload_len = 0;
};

struct Packet {
  Header h;
  uint8_t payload[kMaxPayload] = {0};
};

enum class DecodeError : uint8_t {
  Ok = 0,
  TooShort,
  TooLong,
  BadMagic,
  BadVersion,
  BadType,
  BadLength,  // payload_length does not match the frame length or exceeds kMaxPayload
};

const char* decodeErrorName(DecodeError e);

// Returns the number of bytes written, or 0 if the packet is invalid / cap too small.
size_t encodePacket(const Packet& p, uint8_t* out, size_t cap);
// Strict: the frame must be exactly kHeaderSize + payload_length bytes.
DecodeError decodePacket(const uint8_t* in, size_t len, Packet& out);

// ---- little-endian byte helpers -------------------------------------------
class Writer {
 public:
  Writer(uint8_t* buf, size_t cap) : buf_(buf), cap_(cap) {}
  Writer& u8(uint8_t v);
  Writer& i8(int8_t v) { return u8(static_cast<uint8_t>(v)); }
  Writer& u16(uint16_t v);
  Writer& u32(uint32_t v);
  Writer& bytes(const uint8_t* p, size_t n);
  bool ok() const { return ok_; }
  size_t size() const { return pos_; }

 private:
  uint8_t* buf_;
  size_t cap_;
  size_t pos_ = 0;
  bool ok_ = true;
};

class Reader {
 public:
  Reader(const uint8_t* buf, size_t len) : buf_(buf), len_(len) {}
  uint8_t u8();
  int8_t i8() { return static_cast<int8_t>(u8()); }
  uint16_t u16();
  uint32_t u32();
  bool ok() const { return ok_; }
  bool atEnd() const { return pos_ == len_; }
  size_t remaining() const { return ok_ ? len_ - pos_ : 0; }

 private:
  const uint8_t* buf_;
  size_t len_;
  size_t pos_ = 0;
  bool ok_ = true;
};

inline bool macEqual(const uint8_t* a, const uint8_t* b) {
  for (int i = 0; i < 6; ++i)
    if (a[i] != b[i]) return false;
  return true;
}
inline bool macIsZero(const uint8_t* a) {
  for (int i = 0; i < 6; ++i)
    if (a[i] != 0) return false;
  return true;
}
inline bool macIsBroadcast(const uint8_t* a) {
  for (int i = 0; i < 6; ++i)
    if (a[i] != 0xFF) return false;
  return true;
}

}  // namespace ufo
