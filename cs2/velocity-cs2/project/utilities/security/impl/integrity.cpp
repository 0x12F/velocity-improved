#include <pch/pch.hpp>
#include "../security.hpp"

namespace security::integrity {

	namespace detail {
		std::vector<cached_hash> cached_hashes {};
	} // namespace detail

	bool initialize () {
		// note: the module-hash cache is no longer populated. it called the
		// game's pe analyzer on every loaded module, which crashes on current
		// cs2 builds (stale pattern), and the cache was only used to spoof
		// hashes for the vac hook, which handles an empty cache gracefully.
		return true;
	}

	cached_hash* get (std::uintptr_t module_base) {
		for (auto& entry : detail::cached_hashes) {
			if (entry.module_base == module_base) {
				return &entry;
			}
		}

		return nullptr;
	}

} // namespace security::integrity
