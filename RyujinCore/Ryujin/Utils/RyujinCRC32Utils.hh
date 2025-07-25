#pragma once
#include <Windows.h>
#include <cstdint>

namespace RyujinCRC32Utils {

	auto compute_crc(const uint8_t* data, size_t len, uint32_t poly = 0xB0B0C400) -> uint32_t;

};

