#include "RyujinCRC32Utils.hh"

auto RyujinCRC32Utils::checksum_crc32gentab() -> void {

	unsigned long poly = 0xEDB88320L;
	for (int i = 0; i < 256; i++) {

		unsigned long crc = i;

		for (int j = 8; j > 0; j--) {

			if (crc & 1) crc = (crc >> 1) ^ poly;
			else crc >>= 1;

		}

		m_crc_tab[i] = crc;
	}

}

auto RyujinCRC32Utils::checksum_crc32(unsigned char* block, unsigned int length) -> uint32_t {

	register unsigned long crc = 0xFFFFFFFF;

	for (unsigned long i = 0; i < length; i++) crc = ((crc >> 8) & 0x00FFFFFF) ^ m_crc_tab[(crc ^ *block++) & 0xFF];

	return (crc ^ 0xFFFFFFFF);
}

auto RyujinCRC32Utils::crc32(unsigned char* block, unsigned int length) -> uint32_t {

	if (!m_bInitialized) {

		checksum_crc32gentab();
		m_bInitialized = TRUE;

	}

	return checksum_crc32(block, length);
}