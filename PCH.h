#pragma once

#include "RE/Skyrim.h"
#include "SKSE/SKSE.h"

// SKSEMenuFramework.h uses std::filesystem and RE::InputEvent without including either itself,
// so both have to be in scope before it is included anywhere.
#include <filesystem>

#include <algorithm>
#include <array>
#include <atomic>
#include <cctype>
#include <chrono>
#include <cmath>
#include <condition_variable>
#include <format>
#include <functional>
#include <mutex>
#include <initializer_list>
#include <optional>
#include <ranges>
#include <span>
#include <string>
#include <string_view>
#include <unordered_map>
#include <utility>
#include <vector>

namespace logger = SKSE::log;

using namespace std::literals;

// Skyrim 1.7 branch only: old CommonLibSSE-NG names the maintained library renamed.
#include "Compat.h"
