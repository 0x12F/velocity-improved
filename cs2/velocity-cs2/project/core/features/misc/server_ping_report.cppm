module;

#include "platform/platform.h"

export module velocity:core.features.misc.server_ping_report;

import <cstddef>;
import <cstdint>;
import <span>;
import <string_view>;
import <utility>;
import <vector>;

export import :core.features.misc.server_regions;

export {
namespace features::misc::server_ping_report {

inline constexpr std::uint32_t kMessageType{
    9103};  // MatchmakingClient2ServerPing
inline constexpr std::uint32_t kProtobufFlag{0x80000000u};
inline constexpr std::size_t kEnvelopeSize{8};
inline constexpr std::size_t kMaximumReportSize{1024 * 1024};

enum class Result { kUnchanged, kRewritten, kMalformed, kNoSelectedCenters };

namespace detail {
using Bytes = std::span<const std::uint8_t>;

inline std::uint32_t ReadFixed32(Bytes data) {
  return std::uint32_t(data[0]) | (std::uint32_t(data[1]) << 8) |
         (std::uint32_t(data[2]) << 16) | (std::uint32_t(data[3]) << 24);
}

inline bool ReadVarint(Bytes data, std::size_t& offset, std::uint64_t& value) {
  value = 0;
  for (unsigned shift = 0; shift < 64 && offset < data.size(); shift += 7) {
    const auto byte = data[offset++];
    if (shift == 63 && byte > 1) return false;
    value |= std::uint64_t(byte & 0x7f) << shift;
    if (!(byte & 0x80)) return true;
  }
  return false;
}

struct Field {
  std::uint32_t number{};
  unsigned wire_type{};
  Bytes encoded{};
  Bytes payload{};
};

inline bool NextField(Bytes data, std::size_t& offset, Field& out) {
  const auto start = offset;
  std::uint64_t tag{};
  if (!ReadVarint(data, offset, tag) || tag < 8 || tag > 0xffffffffu)
    return false;
  out.number = static_cast<std::uint32_t>(tag >> 3);
  out.wire_type = tag & 7;
  auto payload_start = offset;
  std::uint64_t length{};
  switch (out.wire_type) {
    case 0:
      if (!ReadVarint(data, offset, length)) return false;
      break;
    case 1:
      length = 8;
      break;
    case 5:
      length = 4;
      break;
    case 2:
      if (!ReadVarint(data, offset, length)) return false;
      payload_start = offset;
      break;
    default:
      return false;
  }
  if (out.wire_type != 0) {
    if (length > data.size() - offset) return false;
    offset += static_cast<std::size_t>(length);
  }
  out.encoded = data.subspan(start, offset - start);
  out.payload = data.subspan(payload_start, offset - payload_start);
  return true;
}

inline void Append(std::vector<std::uint8_t>& out, Bytes data) {
  out.insert(out.end(), data.begin(), data.end());
}

inline void AppendVarint(std::vector<std::uint8_t>& out, std::uint64_t value) {
  while (value >= 0x80) {
    out.push_back(static_cast<std::uint8_t>(value) | 0x80);
    value >>= 7;
  }
  out.push_back(static_cast<std::uint8_t>(value));
}

inline bool RewriteCenter(Bytes data, int region,
                          std::vector<std::uint8_t>& out, bool& selected) {
  std::size_t offset{};
  std::uint32_t pop{};
  bool has_pop{};
  while (offset < data.size()) {
    Field current;
    if (!NextField(data, offset, current)) return false;
    if (current.number == 1) {
      if (current.wire_type != 5) return false;
      pop = ReadFixed32(current.payload);
      has_pop = true;
    }
    if (current.number == 2) {
      if (current.wire_type != 0) return false;
    } else
      Append(out, current.encoded);
  }
  if (!has_pop) return false;
  selected = server_regions::Contains(region, pop);
  out.push_back(2 << 3);  // DataCenterPing.ping is sint32 (ZigZag).
  AppendVarint(out, std::uint32_t(selected ? server_regions::kSelectedPingMs
                                           : server_regions::kExcludedPingMs)
                        << 1);
  return true;
}
}  // namespace detail

// Preserve the GC envelope and every unrelated protobuf field byte-for-byte.
inline Result Rewrite(std::span<const std::uint8_t> packet, int region,
                      std::vector<std::uint8_t>& output) {
  output.clear();
  if (!server_regions::Valid(region)) return Result::kUnchanged;
  if (packet.size() < kEnvelopeSize || packet.size() > kMaximumReportSize)
    return Result::kMalformed;
  if (detail::ReadFixed32(packet) != (kMessageType | kProtobufFlag))
    return Result::kMalformed;
  const auto header_size = detail::ReadFixed32(packet.subspan(4));
  if (header_size > packet.size() - kEnvelopeSize) return Result::kMalformed;
  const auto body_offset = kEnvelopeSize + header_size;
  const auto body = packet.subspan(body_offset);
  std::vector<std::uint8_t> rewritten;
  detail::Append(rewritten, packet.first(body_offset));
  std::size_t offset{};
  bool has_selected_center{};
  while (offset < body.size()) {
    detail::Field current;
    if (!detail::NextField(body, offset, current)) return Result::kMalformed;
    if (current.number == 4) {
      std::vector<std::uint8_t> center;
      bool selected{};
      if (current.wire_type != 2 ||
          !detail::RewriteCenter(current.payload, region, center, selected))
        return Result::kMalformed;
      has_selected_center |= selected;
      rewritten.push_back((4 << 3) | 2);
      detail::AppendVarint(rewritten, center.size());
      detail::Append(rewritten, center);
    } else if (current.number == 5) {
      if (current.wire_type != 0) return Result::kMalformed;
    } else
      detail::Append(rewritten, current.encoded);
  }
  if (!has_selected_center) return Result::kNoSelectedCenters;
  constexpr std::uint32_t kMatchmakingPingLimit{25};
  rewritten.push_back(5 << 3);
  detail::AppendVarint(rewritten, kMatchmakingPingLimit);
  output = std::move(rewritten);
  return Result::kRewritten;
}

}  // namespace features::misc::server_ping_report
}  // export
