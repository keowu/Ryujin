#include <iostream>
#include "Ryujin.hh"

auto main() -> int {

    std::cout << "Hello World!\n";

    std::unique_ptr<Ryujin> ryujin = std::make_unique<Ryujin>("C:\\Users\\Keowu\\Documents\\GitHub\\MoFei\\x64\\Debug\\DemoObfuscation.exe", "C:\\Users\\Keowu\\Documents\\GitHub\\MoFei\\x64\\Debug\\DemoObfuscation.pdb", "C:\\Users\\Keowu\\Documents\\GitHub\\MoFei\\x64\\Debug\\DemoObfuscation2.exe");

    ryujin.get()->listRyujinProcedures();

    ryujin.get()->run();

    ryujin.reset();

    return 0;
}