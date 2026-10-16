#pragma once

#include <RE/Skyrim.h>
#include <SKSE/SKSE.h>

#include <atomic>
#include <chrono>
#include <deque>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <mutex>
#include <sstream>
#include <string>
#include <thread>
#include <vector>

#include <nlohmann/json.hpp>

namespace logs = SKSE::log;
using json = nlohmann::json;
