#include <pch/pch.hpp>
#include <utilities/diag.hpp>
#include "../logging.hpp"

namespace logging::console {

	bool initialize () {
		// note: this used to resolve tier0's LoggingSystem_Log by hardcoded
		// ordinal, which is stale on current cs2 builds and crashed the game
		// whenever anything was printed. Logging now goes to the structured
		// diagnostics file next to the DLL and the debugger output instead.
		return true;
	}

	void print_raw (const char* text) {
		if ( !text ) {
			return;
		}

		const bool was_emitting = emitting;
		emitting = true;
		diag::write( diag::level::info, text );
		emitting = was_emitting;
	}

} // namespace logging::console
