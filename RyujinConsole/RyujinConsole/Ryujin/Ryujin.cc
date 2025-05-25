#include "Ryujin.hh"

Ryujin::Ryujin(const std::string& strInputFilePath, const std::string& strPdbFilePath, const std::string& strOutputFilePath) {

	m_strInputFilePath.assign(strInputFilePath.begin(), strInputFilePath.end());
	m_strOutputFilePath.assign(strOutputFilePath.begin(), strOutputFilePath.end());
	m_strPdbFilePath.assign(strPdbFilePath.begin(), strPdbFilePath.end());

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

bool Ryujin::run(const RyujinObfuscatorConfig& config) {

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

	if (config.m_strProceduresToObfuscate.size() == 0) {

		::OutputDebugStringA(

			_In_ "Ryujin::Ryujin: not provided functions to obfuscate.\n"

		);

		return FALSE;
	}

	for (auto& proc : m_ryujinProcedures) {

		auto it = std::find(config.m_strProceduresToObfuscate.begin(), config.m_strProceduresToObfuscate.end(), proc.name);

		if (it == config.m_strProceduresToObfuscate.end()) continue;

		std::printf(
			
			"[WORKING ON]: %s\n",
			proc.name.c_str()
		
		);

		// Is a valid procedure ?
		if (proc.size == 0) {

			::OutputDebugStringA(

				_In_ "Ryujin::Ryujin: The candidate is a ghost function cannot obfuscate this..\n"

			);

			continue;
		}

		//Get procedure opcodes from mapped pe file
		auto ucOpcodes = new unsigned char[proc.size] { 0 };
		std::memcpy(
			
			ucOpcodes,
			reinterpret_cast<void*>(proc.address),
			proc.size
		
		);

		//Create basic blocks
		RyujinBasicBlockerBuilder rybb(ZYDIS_MACHINE_MODE_LONG_64, ZydisStackWidth_::ZYDIS_STACK_WIDTH_64);
		proc.basic_blocks = rybb.createBasicBlocks(ucOpcodes, proc.size, proc.address);

		//Is time to obfuscate ?
		if (config.m_isVirtualized) todoAction();
		if (config.m_isIatObfuscation) todoAction();
		if (config.m_isJunkCode) todoAction();

		//TODO: Custom passes support

		//Clean up opcodes
		delete[] ucOpcodes;

	}

	//More obfuscation
	if (config.m_isIgnoreOriginalCodeRemove) todoAction();

	//Add section
	char chSectionName[8]{ '.', 'R', 'y', 'u', 'j', 'i', 'n', '\0' };
	if (config.m_isRandomSection) RyujinUtils::randomizeSectionName(chSectionName);

	RyujinPESections peSections;
	peSections.AddNewSection(m_strInputFilePath, chSectionName);

	//Process new opcodes
	std::vector<unsigned char> tempValued = { 0xDE, 0xAD, 0xBE, 0xEF };
	peSections.ProcessOpcodesNewSection(tempValued);

	//Fix relocations

	//Save output file
	peSections.FinishNewSection(m_strOutputFilePath);

}

void Ryujin::listRyujinProcedures() {

	if (!m_isInitialized) {

		::OutputDebugStringA(
			
			_In_ "Ryujin::listRyujinProcedures: not initialized.\n"
		
		);

		return;
	}

	std::printf("=== Ryujin Procedures ===\n");

	for (const auto& procedure : m_ryujinProcedures) {

		std::printf(
			"Name: %-30s | Address: 0x%016llx | Size: 0x%llx\n",
			procedure.name.c_str(),
			procedure.address,
			procedure.size
		);

	}

	std::printf("==========================\n");

}

Ryujin::~Ryujin() {

}
