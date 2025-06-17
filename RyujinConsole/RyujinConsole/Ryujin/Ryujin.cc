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

	std::vector<RyujinObfuscationCore> processed_procs;

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
		RyujinObfuscationCore obc(config, proc, reinterpret_cast<uintptr_t>(m_mappedPE.get()));
		obc.Run();

		//TODO: Custom passes support

		//Storing processed procs
		processed_procs.push_back(obc);

		//Clean up opcodes
		delete[] ucOpcodes;

	}

	//Add section
	char chSectionName[8]{ '.', 'R', 'y', 'u', 'j', 'i', 'n', '\0' };
	if (config.m_isRandomSection) RyujinUtils::randomizeSectionName(chSectionName);

	RyujinPESections peSections;
	peSections.AddNewSection(m_strInputFilePath, chSectionName);

	uintptr_t offsetVA = 0, miniVmEnterAddress = 0;
	std::vector<unsigned char> opcodesWithRelocsFixed;

	//Insert minivm enter routine
	if (config.m_isVirtualized) {

		std::vector<unsigned char> miniVmEnter{
		
			0x48, 0x89, 0x4C, 0x24, 0x08, 0x48, 0x83, 0xEC, 0x28, 0x48, 0x8B, 0x44,
			0x24, 0x30, 0x48, 0xC1, 0xE8, 0x10, 0x48, 0x25, 0xFF, 0x00, 0x00, 0x00,
			0x88, 0x44, 0x24, 0x01, 0x48, 0x8B, 0x44, 0x24, 0x30, 0x48, 0xC1, 0xE8,
			0x08, 0x48, 0x25, 0xFF, 0x00, 0x00, 0x00, 0x88, 0x04, 0x24, 0x48, 0x8B,
			0x44, 0x24, 0x30, 0x48, 0x25, 0xFF, 0x00, 0x00, 0x00, 0x48, 0x89, 0x44,
			0x24, 0x10, 0x48, 0xC7, 0x44, 0x24, 0x08, 0x00, 0x00, 0x00, 0x00, 0x0F,
			0xB6, 0x04, 0x24, 0x88, 0x44, 0x24, 0x04, 0x80, 0x7C, 0x24, 0x04, 0x01,
			0x74, 0x17, 0x80, 0x7C, 0x24, 0x04, 0x02, 0x74, 0x27, 0x80, 0x7C, 0x24,
			0x04, 0x03, 0x74, 0x37, 0x80, 0x7C, 0x24, 0x04, 0x04, 0x74, 0x42, 0xEB,
			0x53, 0x48, 0x8B, 0x44, 0x24, 0x10, 0x48, 0x8B, 0x4C, 0x24, 0x08, 0x48,
			0x03, 0xC8, 0x48, 0x8B, 0xC1, 0x48, 0x89, 0x44, 0x24, 0x08, 0xEB, 0x45,
			0x48, 0x8B, 0x44, 0x24, 0x10, 0x48, 0x8B, 0x4C, 0x24, 0x08, 0x48, 0x2B,
			0xC8, 0x48, 0x8B, 0xC1, 0x48, 0x89, 0x44, 0x24, 0x08, 0xEB, 0x2E, 0x48,
			0x8B, 0x44, 0x24, 0x08, 0x48, 0x0F, 0xAF, 0x44, 0x24, 0x10, 0x48, 0x89,
			0x44, 0x24, 0x08, 0xEB, 0x1C, 0x33, 0xD2, 0x48, 0x8B, 0x44, 0x24, 0x08,
			0x48, 0xF7, 0x74, 0x24, 0x10, 0x48, 0x89, 0x44, 0x24, 0x08, 0xEB, 0x09,
			0x48, 0xC7, 0x44, 0x24, 0x08, 0x00, 0x00, 0x00, 0x00, 0x48, 0x8B, 0x44,
			0x24, 0x08, 0x48, 0x83, 0xC4, 0x28, 0xC3
		
		};

		opcodesWithRelocsFixed.insert(opcodesWithRelocsFixed.end(), miniVmEnter.begin(), miniVmEnter.end());

		miniVmEnterAddress = peSections.getRyujinSectionVA();

		offsetVA += miniVmEnter.size();

	}

	for (auto& obc : processed_procs) {

		auto tempValued = obc.getProcessedProc().getUpdateOpcodes();

		// Fix relocations
		obc.applyRelocationFixupsToInstructions(reinterpret_cast<uintptr_t>(imgDos), peSections.getRyujinSectionVA() + offsetVA, tempValued);

		// Removing and adding a jump in the original procedure and removing original opcodes for a jump to the new obfuscated code
		obc.removeOldOpcodeRedirect(peSections.mappedPeDiskBaseAddress(), peSections.getRyujinMappedPeSize(), reinterpret_cast<uintptr_t>(imgDos) + peSections.getRyujinSectionVA() + offsetVA, config.m_isIgnoreOriginalCodeRemove);

		// Inserindo MiniVMEnter
		if (config.m_isVirtualized) obc.InsertMiniVmEnterProcedureAddress(reinterpret_cast<uintptr_t>(imgDos), miniVmEnterAddress, tempValued);

		// Destructing class
		obc.~RyujinObfuscationCore();

		// Inserting procedures into the list of corrected opcodes
		opcodesWithRelocsFixed.insert(opcodesWithRelocsFixed.end(), tempValued.begin(), tempValued.end());

		// Incrementing the offset with the size of the opcodes in question
		offsetVA += tempValued.size();

	}

	// Encrypt all obfuscated code
	if (config.m_isEncryptObfuscatedCode) todoAction();

	//Process new opcodes
	peSections.ProcessOpcodesNewSection(opcodesWithRelocsFixed);

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
