#pragma once
#include <vector>
#include <Windows.h>
#include <string>

class RyujinObfuscatorConfig {

public:
    bool m_isRandomSection; // Randomize the name of the new section with the processed code -> ".Ryujin" standard
    bool m_isVirtualized; // Virtualize the code [Try as much as possible]
    bool m_isIatObfuscation; //Process IAT Obfuscation
    bool m_isJunkCode; // Insert junk code to confuse
    bool m_isIgnoreOriginalCodeRemove; // Do not remove the original code after processing (replace the original instructions with NOPs)
    bool m_isEncryptObfuscatedCode; // The user wants to encrypt all obfuscated code to avoid detection
	bool m_isAntiDebug; // The user wants to avoid debuggers use while running a binary protected by Ryujin
	bool m_isTrollRerversers; // The user wants to trick and use a special feature to troll reversers when their debugs be detected making they loose all the progress
    std::vector<std::string> m_strProceduresToObfuscate; // Names of the procedures to obfuscate

    bool RunRyujin(const std::string& strInputFilePath, const std::string& strPdbFilePath, const std::string& strOutputFilePath, RyujinObfuscatorConfig& config) {

        using tpdRunRyujinCore = BOOL(__stdcall*)(const std::string& strInputFilePath, const std::string& strPdbFilePath, const std::string& strOutputFilePath, RyujinObfuscatorConfig& config);

        auto hModule = LoadLibraryW(L"RyujinCore.dll");

        if (!hModule) return FALSE;

        auto RunRyujinCore = reinterpret_cast<tpdRunRyujinCore>(GetProcAddress(hModule, "RunRyujinCore"));

        if (!RunRyujinCore) return FALSE;

        return RunRyujinCore(strInputFilePath, strPdbFilePath, strOutputFilePath, config);
    }

};
