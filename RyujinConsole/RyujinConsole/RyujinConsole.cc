#include <iostream>
#include "Ryujin/Ryujin.hh"

auto main() -> int {

    std::cout << "Hello World!\n";

    std::unique_ptr<Ryujin> ryujin = std::make_unique<Ryujin>("C:\\Users\\Keowu\\Documents\\GitHub\\Ryujin\\RyujinConsole\\x64\\Debug\\DemoObfuscation.exe", "C:\\Users\\Keowu\\Documents\\GitHub\\Ryujin\\RyujinConsole\\x64\\Debug\\DemoObfuscation.pdb", "C:\\Users\\Keowu\\Documents\\GitHub\\Ryujin\\RyujinConsole\\x64\\Debug\\DemoObfuscation.obfuscated.exe");

    ryujin.get()->listRyujinProcedures();

    RyujinObfuscatorConfig config;
    config.m_isIgnoreOriginalCodeRemove = FALSE;
    config.m_isJunkCode = TRUE;
    config.m_isRandomSection = FALSE;
    config.m_isVirtualized = TRUE;
    config.m_isIatObfuscation = TRUE;
    config.m_isEncryptObfuscatedCode = FALSE;
    std::vector<std::string> procsToObfuscate{
        "sum",
        "main",
        "invoke_main"
        "__scrt_common_main",
        "j___security_init_cookie"
    };
    config.m_strProceduresToObfuscate.assign(procsToObfuscate.begin(), procsToObfuscate.end());

    ryujin.get()->run(config);

    ryujin.reset();

    return 0;
}