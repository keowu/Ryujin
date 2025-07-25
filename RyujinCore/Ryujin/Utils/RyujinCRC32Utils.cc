#include "RyujinCRC32Utils.hh"

auto RyujinCRC32Utils::compute_crc(const uint8_t* data, size_t len, uint32_t poly) -> uint32_t {

	uint32_t crc = 0xFFFFFFFF;
	for (size_t i = 0; i < len; ++i) {
	
		crc ^= data[i];
		
		for (int j = 0; j < 8; ++j) {
			if (crc & 1)
				crc = (crc >> 1) ^ poly;
			else
				crc >>= 1;
		}
	
	}
	
	return crc ^ 0xFFFFFFFF;
}