module;

#include "platform/platform.h"

export module velocity:utilities.random.random;

import <cstdint>;

export {
namespace random {

void Seed(std::uint32_t s);
int Integer(int min, int max);
float Floating(float min, float max);

float Normal(float mean, float stddev);
float NormalClamped(float mean, float stddev, float min, float max);

float HoldDuration(float mean = 0.085f, float stddev = 0.012f);
float SubtickFraction();

namespace engine {

void Seed(std::uint32_t s);
int RandomInt(int min, int max);
float RandomFloat(float min, float max);
std::int32_t Md5PseudoRandom(std::int32_t seed);

}  // namespace engine

}  // namespace random
}  // export
