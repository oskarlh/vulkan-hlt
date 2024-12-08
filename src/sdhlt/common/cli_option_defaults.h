#pragma once

#include <cstddef>
#include "developer_level.h"
#include "threads.h"

namespace cli_option_defaults {
	constexpr bool chart = true;
	constexpr developer_level_t developer = DEVELOPER_LEVEL_ALWAYS;
	constexpr bool estimate = true;
	constexpr bool info = true;
	constexpr bool log = true;
	constexpr bool nulltex = true;
	constexpr bool verbose = false;
	constexpr q_threadpriority threadPriority = q_threadpriority::eThreadPriorityNormal;

#ifdef SYSTEM_WIN32
	constexpr int numberOfThreads = -1;
#else
	constexpr int numberOfThreads = 1;
#endif


	// This value is arbitrary
	constexpr std::size_t max_map_miptex = 0x2000000;
}
