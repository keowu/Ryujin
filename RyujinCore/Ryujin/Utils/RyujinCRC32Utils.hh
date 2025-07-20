#pragma once
#include <Windows.h>
#include <cstdint>

class RyujinCRC32Utils {

private:
	uint32_t m_crc_tab[256];
	BOOL m_bInitialized = FALSE;

	auto checksum_crc32gentab() -> void;

	auto checksum_crc32(unsigned char* block, unsigned int length) -> uint32_t;

public:

	auto crc32(unsigned char* block, unsigned int length) -> uint32_t;

};

