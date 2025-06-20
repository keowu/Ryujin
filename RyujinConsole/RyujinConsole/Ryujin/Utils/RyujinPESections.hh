#pragma once
#include <iostream>
#include <memory>
#include <vector>
#include <Windows.h>

#include "RyujinUtils.hh"

#define ALIGN_UP(value, alignment) ((value + alignment - 1) & ~(alignment - 1))
#define JACKPOTNUMBER 0x777

class RyujinPESections {

private:
	IMAGE_SECTION_HEADER m_newSection;
	PIMAGE_DOS_HEADER m_dosHeader;
	PIMAGE_NT_HEADERS m_ntHeader;
	unsigned char* m_ucModifiedPeMap;
	uintptr_t m_szFile;
	unsigned char* m_ucResizedPE;
	uintptr_t m_szNewSec;

public:

	uintptr_t getRyujinSectionVA() {

		return m_newSection.VirtualAddress;
	}

	unsigned char* getRyujinSection() {
	
		return m_ucResizedPE;
	}

	uintptr_t mappedPeDiskBaseAddress() {

		return reinterpret_cast<uintptr_t>(m_dosHeader);
	}

	uintptr_t getRyujinSectionSize() {

		return m_szNewSec;
	}

	uintptr_t getRyujinMappedPeSize() {

		return m_szFile;
	}

	BOOL AddNewSection(const std::string& strInputFilePath, char chSectionName[8]);

	BOOL ProcessOpcodesNewSection(std::vector<unsigned char>& opcodeData);

	BOOL FinishNewSection(const std::string& strOutputFilePath);

};