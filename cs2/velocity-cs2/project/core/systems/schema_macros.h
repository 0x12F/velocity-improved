#ifndef VELOCITY_CORE_SYSTEMS_SCHEMA_MACROS_H_
#define VELOCITY_CORE_SYSTEMS_SCHEMA_MACROS_H_

#define SCHEMA(class_name, field_hash)                                        \
  []() -> int {                                                               \
    static const auto val = systems::Schemas::Lookup(class_name, field_hash); \
    return val;                                                               \
  }()

#endif  // VELOCITY_CORE_SYSTEMS_SCHEMA_MACROS_H_
