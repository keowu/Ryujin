#pragma once
#include <windows.h>
#include <dbghelp.h>
#pragma comment(lib, "DbgHelp.lib")
#include "RyujinProcedure.hh"

#define SymTagFunction 5

class RyujinPdbParsing {

private:

	static BOOL CALLBACK EnumSymbolsCallback(PSYMBOL_INFO pSymInfo, ULONG SymbolSize, PVOID UserContext) {

		auto* vecSymbols = reinterpret_cast<std::vector<RyujinProcedure>*>(UserContext);

		//Daddy, is this symbol a function entry ?!
		if (pSymInfo->Tag == SymTagFunction) {

			RyujinProcedure proc;
			proc.name = pSymInfo->Name;
			proc.address = pSymInfo->Address;
			proc.size = pSymInfo->Size;
			vecSymbols->emplace_back(proc);

		}

		return TRUE;
	}


public:

	static std::vector<RyujinProcedure> ExtractProceduresFromPdb(uintptr_t mappedPebase, uintptr_t szFile, const std::string& strInputFilePath, const std::string& strPdbFilePath) {

		std::vector<RyujinProcedure> procs;

		::SymSetOptions(

			_In_ SYMOPT_DEFERRED_LOADS | SYMOPT_UNDNAME

		);

		if (!::SymInitialize(

			_In_ ::GetCurrentProcess(),
			_In_opt_ nullptr,
			_In_ NULL

		)) {

			::OutputDebugStringA(

				_In_ "RyujinPdbParsing::ExtractProceduresFromPdb: SymInitialize failed..\n"

			);

			return procs;
		}

		auto strPdbDirectory = strPdbFilePath.substr(0, strPdbFilePath.find_last_of("\\/"));

		::SymSetSearchPath(

			::GetCurrentProcess(),
			strPdbDirectory.c_str()

		);

		auto loadedModuleBase = ::SymLoadModule64(

			_In_ ::GetCurrentProcess(),
			_In_opt_ nullptr,
			_In_opt_ strInputFilePath.c_str(),
			_In_opt_ nullptr,
			_In_ mappedPebase,
			_In_ szFile

		);

		if (loadedModuleBase == 0) {

			::OutputDebugStringA(

				_In_ "RyujinPdbParsing::ExtractProceduresFromPdb: Failed to load SymLoadModule64..\n"

			);

			::SymCleanup(

				_In_ ::GetCurrentProcess()

			);

			return procs;
		}

		::SymEnumSymbols(
			
			_In_ ::GetCurrentProcess(),
			_In_ loadedModuleBase,
			_In_opt_ nullptr,
			_In_ RyujinPdbParsing::EnumSymbolsCallback,
			_In_opt_ &procs
		
		);

		::SymCleanup(

			_In_::GetCurrentProcess()

		);

		return procs;
	}

};