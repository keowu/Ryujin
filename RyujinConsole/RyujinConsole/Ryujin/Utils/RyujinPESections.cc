#include "RyujinPESections.hh"

BOOL RyujinPESections::AddNewSection(const std::string& strInputFilePath, char chSectionName[8]) {

	auto mappedDiskPE = RyujinUtils::MapDiskPE(strInputFilePath);

	m_szFile = mappedDiskPE.second;

	m_ucModifiedPeMap = new unsigned char[m_szFile]{ 0 };

	std::memcpy(

		m_ucModifiedPeMap,
		mappedDiskPE.first,
		m_szFile

	);

	::UnmapViewOfFile(
		
		_In_ mappedDiskPE.first
	
	);

	m_dosHeader = reinterpret_cast<PIMAGE_DOS_HEADER>(m_ucModifiedPeMap);
	if (m_dosHeader->e_magic != IMAGE_DOS_SIGNATURE) {

		delete[] m_ucModifiedPeMap;

		return FALSE;
	}

	m_ntHeader = reinterpret_cast<PIMAGE_NT_HEADERS>(m_ucModifiedPeMap + m_dosHeader->e_lfanew);
	if (m_ntHeader->Signature != IMAGE_NT_SIGNATURE) {

		delete[] m_ucModifiedPeMap;

		return FALSE;
	}

	auto sectionTableSize = m_ntHeader->FileHeader.NumberOfSections * sizeof(IMAGE_SECTION_HEADER);
	if (m_dosHeader->e_lfanew + sizeof(IMAGE_NT_HEADERS) + sectionTableSize > m_ntHeader->OptionalHeader.SizeOfHeaders) {

		//No space to insert a new section on this PE FILE :(

		delete[] m_ucModifiedPeMap;

		return FALSE;
	}

	//Adding the new section
	std::memcpy(

		m_newSection.Name,
		chSectionName,
		sizeof(chSectionName)

	);

	auto imgLastSection = IMAGE_FIRST_SECTION(m_ntHeader) + (m_ntHeader->FileHeader.NumberOfSections - 1);

	m_newSection.VirtualAddress = ALIGN_UP(
		imgLastSection->VirtualAddress + imgLastSection->Misc.VirtualSize,
		m_ntHeader->OptionalHeader.SectionAlignment
	);

	m_newSection.PointerToRawData = ALIGN_UP(
		imgLastSection->PointerToRawData + imgLastSection->SizeOfRawData,
		m_ntHeader->OptionalHeader.FileAlignment
	);

	m_newSection.PointerToRelocations = JACKPOTNUMBER;

	m_newSection.NumberOfRelocations = JACKPOTNUMBER;

	m_newSection.PointerToLinenumbers = JACKPOTNUMBER;

	m_newSection.NumberOfLinenumbers = JACKPOTNUMBER;

	m_newSection.Characteristics = IMAGE_SCN_CNT_CODE | IMAGE_SCN_MEM_EXECUTE | IMAGE_SCN_MEM_READ | IMAGE_SCN_MEM_WRITE;

	return TRUE;
}

BOOL RyujinPESections::ProcessOpcodesNewSection(std::vector<unsigned char>& opcodeData) {

	m_newSection.Misc.VirtualSize = opcodeData.size();
	m_newSection.SizeOfRawData = ALIGN_UP(opcodeData.size(), m_ntHeader->OptionalHeader.FileAlignment);

	//Let's add a new section entry on the section table
	auto newSectionEntry = IMAGE_FIRST_SECTION(m_ntHeader) + m_ntHeader->FileHeader.NumberOfSections;
	std::memcpy(

		newSectionEntry,
		&m_newSection,
		sizeof(IMAGE_SECTION_HEADER)

	);

	//Let's incement section number
	m_ntHeader->FileHeader.NumberOfSections++;

	//Updating size of img
	m_ntHeader->OptionalHeader.SizeOfImage = ALIGN_UP(m_newSection.VirtualAddress + m_newSection.Misc.VirtualSize, m_ntHeader->OptionalHeader.SectionAlignment);

	//New PE Sz
	m_szNewSec = m_newSection.PointerToRawData + m_newSection.SizeOfRawData;

	m_ucResizedPE = reinterpret_cast<unsigned char*>(std::realloc(

		m_ucModifiedPeMap,
		m_szNewSec

	));

	if (!m_ucResizedPE) {

		//Failed to realocate PE
		delete[] m_ucModifiedPeMap;
		delete[] m_ucResizedPE;

		return FALSE;
	}

	//Zeroing the extended PE region
	if (m_szNewSec > m_szFile) std::memset(

		m_ucResizedPE + m_szFile,
		0,
		m_szNewSec - m_szFile

	);

	//Copying the new opcodes to the new section
	std::memcpy(

		m_ucResizedPE + m_newSection.PointerToRawData,
		opcodeData.data(),
		opcodeData.size()

	);

	//In order to allow fix relocs latter we want:
	//allow get va out of this class
	//give acess to the new allocated area
	//give access to the new size

	return TRUE;
}

BOOL RyujinPESections::FinishNewSection(const std::string& strOutputFilePath) {

	auto bSucess = RyujinUtils::SaveBuffer(strOutputFilePath, m_ucResizedPE, m_szNewSec);

	if (!bSucess) return FALSE;
	
	return TRUE;
}