module;

#include "platform/platform.h"

#include <external/config.hpp>
#include <external/nlohmann/json.hpp>

export module velocity:core.systems.systems;

import <array>;
import <atomic>;
import <cstddef>;
import <cstdint>;
import <filesystem>;
import <functional>;
import <memory>;
import <mutex>;
import <shared_mutex>;
import <span>;
import <string>;
import <string_view>;
import <unordered_map>;
import <vector>;

export import :core.settings;
export import :utilities.cstypes;
export import :utilities.math.math;
export import :utilities.proto.proto;
export import :utilities.fnv1a;

export {
namespace systems {

class Schemas {
 public:
  [[nodiscard]] static std::uint32_t Lookup(const char* class_name,
                                            std::uint32_t field_hash);
};

class Materials {
 public:
  enum class CloneType : int { kTranslucent, kIgnorez };

  [[nodiscard]] static bool Initialize();
  [[nodiscard]] static std::uintptr_t Find(settings::Esp::ChamIds id);
  [[nodiscard]] static const char* GetTexturePath(std::uintptr_t entry);
  [[nodiscard]] static std::string EmitTranslucentKv(std::uintptr_t src_mat);
  [[nodiscard]] static std::string EmitIgnorezKv(std::uintptr_t src_mat);
  [[nodiscard]] static std::uintptr_t Load(const char* vmat_data,
                                           const char* name);

  [[nodiscard]] static std::uintptr_t GetOrCreateClone(
      std::uintptr_t src_mat, CloneType type = CloneType::kTranslucent);
  static void ClearClones();
  static void SetMaterialVec3(std::uintptr_t mat, const char* param_name,
                              float x, float y, float z);

 private:
  static inline std::array<std::uintptr_t, static_cast<std::size_t>(
                                               settings::Esp::ChamIds::kCount)>
      m_loaded_{};
  static inline std::unordered_map<std::uint64_t, std::uintptr_t> m_map_{};
  static inline std::vector<cstypes::StrongHandle> m_handles_{};
  static inline std::mutex m_mtx_{};
};

class Events {
 public:
  using HandlerFn = void(__fastcall*)(void* event);

  static bool Initialize();
  static void Shutdown();

  static bool RegisterListener(const char* event_name, HandlerFn handler);
  static void UnregisterListener(const char* event_name);

 private:
  struct Listener {
    void** vtable;
    int debug_id;
  };

  struct ListenerEntry {
    Listener listener;
    void* vtable_data[3];
    HandlerFn handler;
    const char* name;
    bool registered;
  };

  static void* __fastcall FireEvent(void* self, void* event);
  static int __fastcall GetDebugId(void* self);
  inline static std::vector<std::unique_ptr<ListenerEntry>> m_listeners_{};
};

class Input {
 public:
  struct InButtonState {
    void* vtable;
    std::uintptr_t value;
    std::uintptr_t value_changed;
    std::uintptr_t value_scroll;
  };

  struct Usercmd {
    void* vtable;
    std::intptr_t command_number;
    void* proto_vtable;
    void* proto_arena;
    proto::CsgoUsercmdPb csgo_user_cmd;
    InButtonState buttons;
    char pad1[0x8];
    double last_server_time;
    bool has_been_predicted;
    int flag;
    int command_type;
  };

  struct InputHistoryParams {
    math::Vector3 view_angles{};
    math::Vector3 shoot_position{};
    int render_tick{};
    float render_frac{};
    int player_tick{};
    float player_frac{};
    int frame_number{};
    int target_ent_index{-1};

    float cl_interp_frac{};
    int sv_interp0_src{};
    int sv_interp0_dst{};
    float sv_interp0_frac{};
    int sv_interp1_src{};
    int sv_interp1_dst{};
    float sv_interp1_frac{};
    int player_interp_src{};
    int player_interp_dst{};
    float player_interp_frac{};

    math::Vector3 target_head_pos{};
    math::Vector3 target_abs_pos{};
    math::Vector3 target_abs_ang{};
    bool fill_cheat_check_data{};
  };

  void Update();
  void Apply();

  [[nodiscard]] Usercmd* Get() const { return this->m_current_cmd_; }
  [[nodiscard]] Usercmd* GetCurrentCmd(std::uintptr_t local_controller) const;
  [[nodiscard]] proto::SubtickMoveStep* AcquireSubtickStep(
      proto::RepeatedPtrField<proto::SubtickMoveStep>* subtick_moves) const;
  [[nodiscard]] math::Vector3 GetViewAngles() const;
  [[nodiscard]] proto::InputHistoryEntry* PushInputHistory(
      Usercmd* cmd, const InputHistoryParams& params) const;

  void SetViewAngles(const math::Vector3& angles) const;
  void Desubtick(Usercmd* cmd) const;
  void SetWeaponSelect(Usercmd* cmd, std::uintptr_t csgo_input) const;

  [[nodiscard]] Usercmd* GetCommandBySequence(std::uintptr_t local_controller,
                                              int sequence) const;
  [[nodiscard]] bool IsSubtickOverwrite(Usercmd* cmd) const;

 private:
  Usercmd* m_current_cmd_{};
  proto::BaseUsercmdPb m_backup_{};

  bool CalculateCrc(proto::BaseUsercmdPb* base) const;
};

class LegitInput {
 public:
  // void press( std::uintptr_t button_bit, float when = -1.0f );
  // void release( std::uintptr_t button_bit, float when = -1.0f );
  void AddMouseDelta(float pitch_degrees, float yaw_degrees);

  // void on_create_move( input::usercmd* cmd );
  void OnProcessInputEvent(std::uintptr_t csgo_input, int slot);

 private:
  //[[nodiscard]] float resolve_when( ) const;

  bool m_pending_press_{};
  bool m_pending_release_{};
  std::uintptr_t m_press_button_{};
  std::uintptr_t m_release_button_{};
  float m_press_when_{-1.0f};
  float m_release_when_{-1.0f};

  float m_pending_pitch_{};
  float m_pending_yaw_{};
};

class Entities {
 public:
  enum class Type : std::uint8_t { kUnknown, kPlayer, kItem, kProjectile };

  struct Cached {
    std::uintptr_t ptr{};
    std::uint32_t schema_hash{};
    std::int16_t index{};
    Type type{};
  };

  void OnAddEntity(std::uintptr_t entity, std::uint32_t handle);
  void OnRemoveEntity(std::uintptr_t entity, std::uint32_t handle);
  void ForceUpdate();

  [[nodiscard]] bool Exists(std::uintptr_t entity) const;
  [[nodiscard]] const char* GetSchemaName(std::uintptr_t entity) const;
  [[nodiscard]] std::uintptr_t GetByIndex(int index);
  [[nodiscard]] std::uintptr_t Lookup(std::uint32_t handle) const;
  [[nodiscard]] std::vector<Cached> GetByType(Type type) const;

  [[nodiscard]] bool IsEmpty() const;

 private:
  [[nodiscard]] Type ClassifyEntity(std::uint32_t schema_hash) const;

  static constexpr std::int32_t kEntityIndexMask = 0x7fff;
  static constexpr std::size_t kEntitiesPerChunk = 512;
  static constexpr std::size_t kEntityChunkCount =
      (kEntityIndexMask + 1) / kEntitiesPerChunk;

  std::vector<Cached> m_cached_{};
  mutable std::shared_mutex m_cache_mtx_{};
  std::array<std::uintptr_t, kEntityChunkCount> m_cached_list_entries_{};
  std::uintptr_t m_cached_entity_list_{};
};

class Local {
 public:
  struct Snapshot {
    std::uintptr_t controller{};
    std::uintptr_t pawn{};
    std::uintptr_t observer_pawn{};
    std::uintptr_t observer_controller{};
    int team{};
    int view_team{};
    bool is_alive{};
    bool is_team_mode{};

    [[nodiscard]] std::uintptr_t ViewController() const {
      return this->is_alive ? this->controller : this->observer_controller;
    }
    [[nodiscard]] std::uintptr_t ViewPawn() const {
      return this->is_alive ? this->pawn : this->observer_pawn;
    }
    [[nodiscard]] bool IsValid() const {
      return this->pawn != 0 || this->observer_pawn != 0;
    }
    [[nodiscard]] bool IsThisOtherTeam(int other_team) const {
      return !this->is_team_mode || this->view_team != other_team;
    }
  };

  void Update();

  [[nodiscard]] Snapshot Get() const {
    std::shared_lock lock(this->m_mtx_);
    return this->m_snapshot_;
  }

  [[nodiscard]] bool IsInCinematic() const {
    return this->m_is_in_cinematic_.load();
  }
  [[nodiscard]] bool IsInTimeFreeze() const {
    return this->m_is_in_time_freeze_.load();
  }
  [[nodiscard]] bool IsInDeathmatch() const {
    return this->m_is_deathmatch_.load();
  }

  // made this public, to be called by level_shutdown
  void Reset();

 private:
  Snapshot m_snapshot_{};
  mutable std::shared_mutex m_mtx_{};

  std::atomic<bool> m_is_deathmatch_{};
  std::atomic<bool> m_is_in_cinematic_{};
  std::atomic<bool> m_is_in_time_freeze_{};
};

class Prediction {
 public:
  struct State {
    std::uint32_t flags{};
    math::Vector3 networked_velocity{};
    math::Vector3 velocity{};
    math::Vector3 origin{};
    math::Vector3 networked_origin{};
    math::Vector3 last_movement_impulses{};
    float surface_friction{};
    float stamina{};
  };

  void CapturePrestate(std::uintptr_t local_pawn,
                       std::uintptr_t movement_services);

  [[nodiscard]] const State& Pre() const { return this->m_prestate_; }

 private:
  State m_prestate_{};
};

class View {
 public:
  struct Projection {
    math::Vector2 screen{};
    float w{};
    bool on_screen{};
  };

  void Update(std::uintptr_t view);
  void UpdateMatrix();

  [[nodiscard]] math::Vector2 Project(const math::Vector3& world_pos);
  [[nodiscard]] Projection ProjectFull(const math::Vector3& world_pos) const;
  [[nodiscard]] bool ProjectionValid(const math::Vector2& screen_pos) {
    return screen_pos.x != this->kInvalid && screen_pos.y != this->kInvalid;
  }

  [[nodiscard]] bool HasCamera() const {
    return this->m_matrix_valid_.load(std::memory_order_acquire);
  }
  [[nodiscard]] math::Vector3 Origin() const { return this->m_origin_; }
  [[nodiscard]] math::Vector3 Angles() const { return this->m_angles_; }
  [[nodiscard]] float Fov() const { return this->m_fov_; }
  [[nodiscard]] math::Matrix4x4 Matrix() const;

 private:
  static constexpr auto kInvalid{0xdead};

  math::Matrix4x4 m_matrix_{};
  mutable std::mutex m_matrix_mtx_{};
  std::atomic<bool> m_matrix_valid_{};
  math::Vector3 m_origin_{};
  math::Vector3 m_angles_{};
  float m_fov_{kInvalid};
};

class Bones {
 public:
  struct Data {
    math::Vector3 position{};
    float scale{};
    math::Quaternion rotation{};
  };

  [[nodiscard]] static int GetCount(std::uintptr_t game_scene_node);
  [[nodiscard]] Data Get(std::uintptr_t entity, std::uint32_t bone_id);
  [[nodiscard]] std::array<Data, 27> GetSkeleton(std::uintptr_t entity);

 private:
  [[nodiscard]] std::uintptr_t GetBoneCache(std::uintptr_t entity,
                                            int* out_count = nullptr);
};

class Bounds {
 public:
  struct Data {
    math::Vector2 min{};
    math::Vector2 max{};
    bool valid{};

    [[nodiscard]] float Width() const { return this->max.x - this->min.x; }
    [[nodiscard]] float Height() const { return this->max.y - this->min.y; }
    [[nodiscard]] math::Vector2 Center() const {
      return {this->min.x + this->Width() * 0.5f,
              this->min.y + this->Height() * 0.5f};
    }
  };

  [[nodiscard]] Data Get(std::uintptr_t entity);
};

class Hitboxes {
 public:
  struct Entry {
    int index{-1};
    int bone{-1};
    int hitgroup{};
    math::Vector3 mins{};
    math::Vector3 maxs{};
    float radius{};
    std::uint8_t shape_type{};
    bool translation_only{};
  };

  struct Set {
    std::array<Entry, 20> entries{};
    int count{};

    [[nodiscard]] const Entry* begin() const { return this->entries.data(); }
    [[nodiscard]] const Entry* end() const {
      return this->entries.data() + this->count;
    }
  };

  [[nodiscard]] Set Query(std::uintptr_t game_scene_node, bool seh = false);
  [[nodiscard]] int HitgroupFromHitbox(int hitbox);
  [[nodiscard]] const char* HitgroupToName(int hitgroup);
};

class Tracing {
 public:
  struct Filter {
    std::uintptr_t vtable;
    std::uintptr_t mask;
    std::array<std::int64_t, 2> v1;
    std::array<int, 4> skip_handles;
    std::array<std::int16_t, 2> collisions;
    std::int16_t v2;
    std::uint8_t layer;
    std::uint8_t flags;
    std::uint8_t v5;
    std::uint8_t v6;
    std::byte pad0[0x6];
    char v7;
  };

  struct Ray {
    math::Vector3 mins;
    math::Vector3 maxs;
    std::byte pad0[0x10];
    std::uint8_t type;
    std::byte pad1[0x7];
  };

  struct Result {
    void* surface;
    std::uintptr_t hit_entity;
    void* hitbox_data;
    std::byte pad0[0x38];
    std::uint32_t contents;
    std::byte pad1[0x24];
    math::Vector3 start_pos;
    math::Vector3 end_pos;
    math::Vector3 normal;
    math::Vector3 position;
    std::byte pad2[0x4];
    float fraction;
    std::byte pad3[0x6];
    bool all_solid;
    std::byte pad4[0x4d];
  };

  struct TraceArrayElement {
    std::byte pad0[0x38];
  };

  struct TraceData {
    int unknown1;
    float unknown2{52.0f};
    void* array_pointer;
    int unknown3{128};
    int unknown4{static_cast<int>(0x80000000)};
    std::array<TraceArrayElement, 0x80> elements;
    std::byte pad0[0x8];
    int num_hits;
    int unknown5;
    void* hit_array_pointer;
    int hit_capacity{8};
    int hit_flags{static_cast<int>(0x80000000)};
    std::byte hit_elements[0xc0];
    math::Vector3 start;
    math::Vector3 direction;
    float fraction{1.0f};
    bool unknown6;
    std::byte pad1[0x4b];
  };

  static_assert(offsetof(TraceData, elements) == 0x18);
  static_assert(offsetof(TraceData, num_hits) == 0x1c20);
  static_assert(offsetof(TraceData, hit_array_pointer) == 0x1c28);
  static_assert(offsetof(TraceData, start) == 0x1cf8);
  static_assert(sizeof(TraceData) == 0x1d60);

  struct PlayerMovementFilter {
    std::byte data[0x48];
  };

  struct BboxCollision {
    math::Vector3 mins;
    math::Vector3 maxs;
  };

  [[nodiscard]] bool IsVisible(const math::Vector3& start,
                               const math::Vector3& end,
                               std::uintptr_t target_entity,
                               std::uintptr_t skip_entity,
                               std::uintptr_t mask = 0x1c3003) const;

  [[nodiscard]] Result Trace(const math::Vector3& start,
                             const math::Vector3& end,
                             std::uintptr_t skip_entity,
                             std::uintptr_t mask = 0x1c3003,
                             std::uint8_t layer = 4) const;
  [[nodiscard]] Result Trace(const math::Vector3& start,
                             const math::Vector3& end,
                             const Filter& filter) const;
  [[nodiscard]] Result TraceHull(const math::Vector3& start,
                                 const math::Vector3& end,
                                 const math::Vector3& mins,
                                 const math::Vector3& maxs,
                                 std::uintptr_t skip_entity,
                                 std::uintptr_t mask = 0x1c3003,
                                 std::uint8_t layer = 4) const;
  [[nodiscard]] Result TraceHull(const math::Vector3& start,
                                 const math::Vector3& end,
                                 const math::Vector3& mins,
                                 const math::Vector3& maxs,
                                 const Filter& filter) const;
  [[nodiscard]] Result TraceSphere(const math::Vector3& start,
                                   const math::Vector3& end, float radius,
                                   const Filter& filter) const;
  [[nodiscard]] Result TraceToEntity(const math::Vector3& start,
                                     const math::Vector3& end,
                                     std::uintptr_t target_entity,
                                     std::uintptr_t skip_entity,
                                     std::uintptr_t mask = 0x1c3003,
                                     std::uint8_t layer = 4) const;
  [[nodiscard]] Result TraceToEntity(const math::Vector3& start,
                                     const math::Vector3& end,
                                     std::uintptr_t target_entity,
                                     const Filter& filter) const;
  [[nodiscard]] Filter MakeFilter(std::uintptr_t skip_entity,
                                  std::uintptr_t mask, std::uint8_t layer,
                                  int type) const;
  [[nodiscard]] Filter MakeFilter(std::uintptr_t skip_entity,
                                  std::uintptr_t mask,
                                  std::uint8_t layer) const;
  [[nodiscard]] PlayerMovementFilter MakePlayerMovementFilter(
      std::uintptr_t entity, std::uint8_t collision_group = 11) const;
  [[nodiscard]] Tracing::Result TracePlayerBbox(
      const math::Vector3& start, const math::Vector3& end,
      const BboxCollision& bbox, const PlayerMovementFilter& filter,
      std::uintptr_t movement_services) const;

  void SetupTrace(TraceData* trace_data, const math::Vector3& start,
                  const math::Vector3& delta, const Filter& filter,
                  int penetration_count, bool trace_world = false) const;
  void InitResult(Result* trace_result) const;
  void FinalizeTrace(TraceData* trace_data, Result* trace_result,
                     float unknown_float, void* unknown) const;
};

class FrameData {
 public:
  void Update();

  [[nodiscard]] math::Vector3 Origin() const { return this->m_origin_; }
  [[nodiscard]] bool Valid() const { return this->m_valid_; }

 private:
  void Reset();

  math::Vector3 m_origin_{};
  bool m_valid_{};
};

class Icons {
 public:
  struct Icon {
    Microsoft::WRL::ComPtr<ID3D11ShaderResourceView> texture{};
    int width{};
    int height{};
  };

  bool Initialize();
  void Shutdown();

  [[nodiscard]] const Icon* Get(const std::string& name, float scale = 1.0f);
  [[nodiscard]] const Icon* Get(std::uint32_t schema_hash, float scale = 1.0f);

 private:
  bool LoadVpkDirectory(const std::filesystem::path& path);
  bool CacheSvgBytes(const std::filesystem::path& vpk_path,
                     const std::string& icon_name, std::uint32_t entry_offset,
                     std::uint32_t entry_length);
  std::vector<std::byte> DecompileVsvg(std::span<const std::byte> data) const;

  struct IconKey {
    std::string name;
    std::uint32_t scale_bits;

    bool operator==(const IconKey& o) const noexcept {
      return this->scale_bits == o.scale_bits && this->name == o.name;
    }
  };

  struct IconKeyHash {
    std::size_t operator()(const IconKey& k) const noexcept {
      return std::hash<std::string>{}(k.name) ^
             (std::hash<std::uint32_t>{}(k.scale_bits) << 1);
    }
  };

  std::unordered_map<IconKey, Icon, IconKeyHash> m_icons_{};
  std::unordered_map<std::uint32_t, std::string> m_hash_to_name_{};
  std::unordered_map<std::string, std::vector<std::byte>> m_pending_svgs_{};
};

Input g_input{};
LegitInput g_legit_input{};
Entities g_entities{};
Local g_local{};
Prediction g_prediction{};
View g_view{};
Bones g_bones{};
Bounds g_bounds{};
Hitboxes g_hitboxes{};
Tracing g_tracing{};
FrameData g_frame_data{};
Icons g_icons{};

}  // namespace systems
}  // export
