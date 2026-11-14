#include "protocol.h"

#include <string.h>

namespace ufo {

bool isValidMsgType(uint8_t t) { return t >= 0x01 && t <= 0x06; }

const char* msgTypeName(MsgType t) {
  switch (t) {
    case MsgType::Command: return "COMMAND";
    case MsgType::Event: return "EVENT";
    case MsgType::Ack: return "ACK";
    case MsgType::Status: return "STATUS";
    case MsgType::Heartbeat: return "HEARTBEAT";
    case MsgType::Error: return "ERROR";
  }
  return "?";
}

const char* decodeErrorName(DecodeError e) {
  switch (e) {
    case DecodeError::Ok: return "ok";
    case DecodeError::TooShort: return "too_short";
    case DecodeError::TooLong: return "too_long";
    case DecodeError::BadMagic: return "bad_magic";
    case DecodeError::BadVersion: return "bad_version";
    case DecodeError::BadType: return "bad_type";
    case DecodeError::BadLength: return "bad_length";
  }
  return "?";
}

Writer& Writer::u8(uint8_t v) {
  if (!ok_ || pos_ + 1 > cap_) {
    ok_ = false;
    return *this;
  }
  buf_[pos_++] = v;
  return *this;
}
Writer& Writer::u16(uint16_t v) {
  if (!ok_ || pos_ + 2 > cap_) {
    ok_ = false;
    return *this;
  }
  buf_[pos_++] = static_cast<uint8_t>(v & 0xFF);
  buf_[pos_++] = static_cast<uint8_t>(v >> 8);
  return *this;
}
Writer& Writer::u32(uint32_t v) {
  if (!ok_ || pos_ + 4 > cap_) {
    ok_ = false;
    return *this;
  }
  for (int i = 0; i < 4; ++i) buf_[pos_++] = static_cast<uint8_t>((v >> (8 * i)) & 0xFF);
  return *this;
}
Writer& Writer::bytes(const uint8_t* p, size_t n) {
  if (!ok_ || pos_ + n > cap_) {
    ok_ = false;
    return *this;
  }
  memcpy(buf_ + pos_, p, n);
  pos_ += n;
  return *this;
}

uint8_t Reader::u8() {
  if (!ok_ || pos_ + 1 > len_) {
    ok_ = false;
    return 0;
  }
  return buf_[pos_++];
}
uint16_t Reader::u16() {
  if (!ok_ || pos_ + 2 > len_) {
    ok_ = false;
    return 0;
  }
  uint16_t v = static_cast<uint16_t>(buf_[pos_] | (buf_[pos_ + 1] << 8));
  pos_ += 2;
  return v;
}
uint32_t Reader::u32() {
  if (!ok_ || pos_ + 4 > len_) {
    ok_ = false;
    return 0;
  }
  uint32_t v = 0;
  for (int i = 0; i < 4; ++i) v |= static_cast<uint32_t>(buf_[pos_ + i]) << (8 * i);
  pos_ += 4;
  return v;
}

size_t encodePacket(const Packet& p, uint8_t* out, size_t cap) {
  if (p.h.payload_len > kMaxPayload) return 0;
  if (!isValidMsgType(static_cast<uint8_t>(p.h.type))) return 0;
  Writer w(out, cap);
  w.u16(p.h.magic)
      .u8(p.h.version)
      .u8(p.h.source)
      .u8(p.h.destination)
      .u8(static_cast<uint8_t>(p.h.type))
      .u16(p.h.sequence)
      .u32(p.h.session)
      .u32(p.h.txn)
      .u8(p.h.payload_len)
      .bytes(p.payload, p.h.payload_len);
  return w.ok() ? w.size() : 0;
}

DecodeError decodePacket(const uint8_t* in, size_t len, Packet& out, uint16_t magic) {
  if (in == nullptr || len < kHeaderSize) return DecodeError::TooShort;
  if (len > kMaxPacketSize) return DecodeError::TooLong;
  Reader r(in, len);
  Header h;
  h.magic = r.u16();
  if (h.magic != magic) return DecodeError::BadMagic;
  h.version = r.u8();
  if (h.version != kProtocolVersion) return DecodeError::BadVersion;
  h.source = r.u8();
  h.destination = r.u8();
  uint8_t t = r.u8();
  if (!isValidMsgType(t)) return DecodeError::BadType;
  h.type = static_cast<MsgType>(t);
  h.sequence = r.u16();
  h.session = r.u32();
  h.txn = r.u32();
  h.payload_len = r.u8();
  if (!r.ok()) return DecodeError::TooShort;
  if (h.payload_len > kMaxPayload || kHeaderSize + h.payload_len != len) return DecodeError::BadLength;
  out.h = h;
  memset(out.payload, 0, sizeof(out.payload));
  memcpy(out.payload, in + kHeaderSize, h.payload_len);
  return DecodeError::Ok;
}

}  // namespace ufo
