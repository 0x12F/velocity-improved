module;

#include "platform/platform.h"


#include "protection/address_macros.h"

module velocity;

import <cstddef>;
import <string_view>;
import <type_traits>;

namespace random {

static std::mt19937& Rng() {
  static std::mt19937 gen{std::random_device{}()};
  return gen;
}

void Seed(std::uint32_t s) { Rng().seed(s); }

int Integer(int min, int max) {
  return std::uniform_int_distribution<int>{min, max}(Rng());
}

float Floating(float min, float max) {
  return std::uniform_real_distribution<float>{min, max}(Rng());
}

float Normal(float mean, float stddev) {
  return std::normal_distribution<float>{mean, stddev}(Rng());
}

float NormalClamped(float mean, float stddev, float min, float max) {
  auto val = Normal(mean, stddev);
  if (val < min) {
    val = min;
  }

  if (val > max) {
    val = max;
  }

  return val;
}

float HoldDuration(float mean, float stddev) {
  return NormalClamped(mean, stddev, 0.045f, 0.140f);
}

float SubtickFraction() { return Floating(0.0f, std::nextafter(1.0f, 0.0f)); }

namespace engine {

void Seed(std::uint32_t s) {
  memory::Call<void>(MODULE_EXPORT("tier0.dll:RandomSeed"), s);
}

int RandomInt(int min, int max) {
  return memory::Call<int>(MODULE_EXPORT("tier0.dll:RandomInt"), min, max);
}

float RandomFloat(float min, float max) {
  return memory::Call<float>(MODULE_EXPORT("tier0.dll:RandomFloat"), min, max);
}

std::int32_t Md5PseudoRandom(std::int32_t seed) {
  return memory::Call<std::int32_t>(MODULE_EXPORT("tier0.dll:MD5_PseudoRandom"),
                                    seed);
}

}  // namespace engine

}  // namespace random
