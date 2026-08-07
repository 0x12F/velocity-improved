#include <pch/pch.hpp>
#include <utilities/memory/memory.hpp>
#include <protection/game_addresses.hpp>
#include "../logging.hpp"

namespace logging::popup {

	namespace detail {

		inline std::uintptr_t game_ui {0};

	} // namespace detail

	bool initialize () {
		detail::game_ui = memory::find_instance_by_rtti (MODULE_BASE ("client.dll"), xs ("CLegacyGameUI"));
		return detail::game_ui != 0;
	}

	void show (const char* title, const char* message) {
		if (!detail::game_ui) {
			return;
		}

		memory::call_vfunc<void> (detail::game_ui, 28, title, message, true, false, nullptr, nullptr, nullptr, 0, 0ll);
	}

} // namespace logging::popup