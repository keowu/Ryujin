#pragma once
#include <vector>
#include <Windows.h>
#include <string>

#define MAX_PROCEDURES 128
#define MAX_PROCEDURE_NAME_LEN 128

struct RyujinObfuscatorProcs {
    int procedureCount;
    char procedures[MAX_PROCEDURES][MAX_PROCEDURE_NAME_LEN];
};

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
    bool m_isAntiDump; // Enable Anti Dump technic for Ryujin protected binary
    RyujinObfuscatorProcs m_strProceduresToObfuscate; // Names of the procedures to obfuscate - FFI
    std::vector<std::string> m_strdProceduresToObfuscate; // Names of the procedures to obfuscate

    static bool RunRyujin(const std::string& strInputFilePath, const std::string& strPdbFilePath, const std::string& strOutputFilePath, RyujinObfuscatorConfig& config) {

        using tpdRunRyujinCore = BOOL(__stdcall*)(const char*, const char*, const char*, RyujinObfuscatorConfig&);

        auto hModule = LoadLibraryW(L"RyujinCore.dll");

        if (!hModule) return FALSE;

        auto RunRyujinCore = reinterpret_cast<tpdRunRyujinCore>(GetProcAddress(hModule, "RunRyujinCore"));

        if (!RunRyujinCore) return FALSE;

        return RunRyujinCore(strInputFilePath.c_str(), strPdbFilePath.c_str(), strOutputFilePath.c_str(), config);
    }

};
