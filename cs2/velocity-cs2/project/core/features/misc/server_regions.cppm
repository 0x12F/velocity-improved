module;

#include "platform/platform.h"

export module velocity:core.features.misc.server_regions;

import <array>;
import <cstdint>;
import <string_view>;

export {
namespace features::misc::server_regions {

inline constexpr std::array kNames{"North America", "Europe",  "South America",
                                   "Asia",          "Oceania", "Middle East",
                                   "Africa"};

// Valve's CS2 SDR POP codes. Keep four-character data centers distinct.
inline constexpr std::array<std::string_view, kNames.size()> kDataCenters{
    "atl,dfw,eat,iad,lax,ord,sea",
    "ams,fra,fsn,hel,lhr,mad,par,sto,sto2,vie,waw,dfra,dvie,dsto,dlhr",
    "eze,gru,lim,scl",
    "bom,bom2,maa,maa2,ctu,ctum,ctut,ctuu,pek,pekm,pekt,peku,pvg,pvgm,pvgt,"
    "pvgu,tgd,tgdm,tgdt,tgdu,hkg,seo,sgp,tyo",
    "syd,gum",
    "dxb",
    "jnb"};

inline constexpr int kDisabled{-1};
inline constexpr int kSelectedPingMs{1};
inline constexpr int kExcludedPingMs{9999};

[[nodiscard]] constexpr bool Valid(int region) {
  return region >= 0 && region < static_cast<int>(kNames.size());
}

[[nodiscard]] constexpr std::uint32_t PopId(std::string_view code) {
  if (code.size() != 3 && code.size() != 4) return 0;

  return (static_cast<std::uint32_t>(code[0]) << 16) |
         (static_cast<std::uint32_t>(code[1]) << 8) |
         static_cast<std::uint32_t>(code[2]) |
         (code.size() == 4 ? static_cast<std::uint32_t>(code[3]) << 24 : 0);
}

[[nodiscard]] constexpr bool Contains(int region, std::uint32_t pop) {
  if (!Valid(region)) return false;

  auto codes = kDataCenters[region];
  while (!codes.empty()) {
    const auto separator = codes.find(',');
    if (PopId(codes.substr(0, separator)) == pop) return true;
    if (separator == std::string_view::npos) break;
    codes.remove_prefix(separator + 1);
  }
  return false;
}

[[nodiscard]] constexpr int FilterPing(int region, std::uint32_t pop,
                                       int measured_ping) {
  if (!Valid(region)) return measured_ping;
  if (!Contains(region, pop)) return kExcludedPingMs;
  // A negative result can mean "not measured", particularly for distant POPs.
  // Keep selected centers eligible so they reach the matchmaking report.
  return kSelectedPingMs;
}

}  // namespace features::misc::server_regions
}  // export
