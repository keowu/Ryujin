#include "Ryujin.hh"

Ryujin::Ryujin(const std::string& strInputFilePath, const std::string& strPdbFilePath, const std::string& strOutputFilePath) :
m_strInputFilePath(strInputFilePath), m_strOutputFilePath(strOutputFilePath), m_strPdbFilePath(strPdbFilePath) {

	auto mappedInfo = RyujinUtils::MapPortableExecutableFileIntoMemory(m_strInputFilePath, m_mappedPE);

	m_szFile = mappedInfo.second;
	m_isInitialized = mappedInfo.first;

	if (!m_isInitialized) {

		::OutputDebugStringA(
			
			_In_ "Ryujin::Ryujin: failed to initilize.\n"
		
		);

	}

	m_ryujinProcedures = RyujinPdbParsing::ExtractProceduresFromPdb(

		reinterpret_cast<uintptr_t>(m_mappedPE.get()),
		m_szFile,
		m_strInputFilePath,
		m_strPdbFilePath

	);

	if (m_ryujinProcedures.size() == 0) {

		m_isInitialized = FALSE;

		::OutputDebugStringA(

			_In_ "Ryujin::Ryujin: No Associate PDB file found for the input binary.."

		);

	}

}

bool Ryujin::run() {

	auto imgDos = reinterpret_cast<PIMAGE_DOS_HEADER>(m_mappedPE.get());

	if (imgDos->e_magic != IMAGE_DOS_SIGNATURE) {

		::OutputDebugStringA(
			
			_In_ "Ryujin::run: Invalid PE File.\n"
		
		);

		return FALSE;
	}

	auto imgNt = reinterpret_cast<PIMAGE_NT_HEADERS>(m_mappedPE.get() + imgDos->e_lfanew);

	if (imgNt->Signature != IMAGE_NT_SIGNATURE) {

		::OutputDebugStringA(
			
			_In_ "Ryujin::run: Invalid NT headers for the input PE File.\n"
		
		);

		return FALSE;
	}

	if (!m_isInitialized) {

		::OutputDebugStringA(
			
			_In_ "Ryujin::Ryujin: not initilized.\n"
		
		);

		return FALSE;
	}

}

void Ryujin::listRyujinProcedures() {

	if (!m_isInitialized) {

		::OutputDebugStringA(

			_In_ "Ryujin::Ryujin: not initilized.\n"

		);

		return;
	}

	for (auto& procedure : m_ryujinProcedures) {

		std::printf("%s - 0x%llx - 0x%llx\n", procedure.name.c_str(), procedure.address, procedure.size);

	}

}

Ryujin::~Ryujin() {

}
