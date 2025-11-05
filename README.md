# Ryūjin Protector

**Ryūjin Protector** is an open-source **Bin2Bin** obfuscation, protection, and DRM tool for **Windows PE** binaries targeting the **Intel x64 architecture(Only)**.

<p align="center">
  <img src="imgs/ryujinreadme.png" alt="Ryūjin Protector Banner" width="80%">
</p>

---

## Features

- Junk Code Insertion + Code Mutation (Fully randomic without breaking the original logic)
- IAT Call Access Obfuscation(With obfuscated handlers access)
- Random Section naming(Default name: Ryujin)
- Mathematical Operators Virtualization(aka: Ryūjin MiniVM)
- Obfuscated code Encryption(Using TeaDelKew Algorithm)
- Anti-Debug User + Kernel
- Troll Reversers(Exclusive)
- Anti-Dump
- Anti-Disassembly + Anti-Decompiler
- Memory Protection(CRC32)
- Custom Passes(MBA Obfuscation Pass and Custom Passes from Ryujin Community)

---

## Demos and Presentations

Ryūjin was designed and developed for the study of obfuscators with Bin2Bin capabilities, making it a viable project for use by third parties as well as serious information security students. This includes: Commercial Developers, Indie Developers/Cheat Developers, Anti-Cheat Developers, Malware Developers, Malware Analysts, and Security Researchers.

**A Simple Comparison on a "main" function. before and after applying Ryūjin:**

![Ryūjin Protector Demo](imgs/demo.png)

This is only a small demo with only one Ryūjin feature, others feature together produce a better result.

**Really Easy to Use:**

**Ryūjin** is extremely easy to use — you can choose between the GUI mode or the CLI mode. Both will produce the same result in a precise, functional, and stable way.

GUI Mode Demonstration:
![Ryūjin Protector Demo GUI](imgs/demo1.png)

CLI Mode Demonstration:
![Ryūjin Protector Demo Console](imgs/demo2.png)

For both options, you will need exclusively a PE file (Apanas, executable, for now) along with a PDB file containing the symbols for that PE file, so that you can protect and generate a new binary. Additionally, you can consult the WIKI at any time to discover other options and possibilities, such as custom passes.

---

## Custom Pass Support

In addition to the standard techniques available via CLI or GUI options, Ryūjin also supports a Custom Pass feature. This allows anyone to implement a callback that is invoked during the obfuscation flow, enabling users to extend Ryūjin’s capabilities with custom features specific to their code.

To do this, the user must follow a specific callback model:

```c++
void RyujinCustomPassDemo(RyujinProcedure* proc);
```

Each time the callback is invoked, a ```RyujinProcedure``` instance is provided, allowing the user to modify execution scopes, basic block structures, and more. The class definition can be found in [RyujinProcedure.hh](https://github.com/keowu/Ryujin/blob/main/RyujinCore/Ryujin/Models/RyujinProcedure.hh). Additionally, a usage example is available in [RyujinConsole.cc](https://github.com/keowu/Ryujin/blob/main/RyujinConsole/RyujinConsole/RyujinConsole.cc#L10), No additional configuration file changes are required. The ```RyujinObfuscatorConfig``` class already includes all necessary settings for immediate use.

## Dependencies

To compile RyujinCore, RyujinConsole, and RyujinGUI, critical dependencies must be installed via [Microsoft VCPKG](https://github.com/microsoft/vcpkg), You can install them using the following commands:

```
vcpkg install asmjit
vcpkg install zydis
vcpkg install z3
```

For a consistent development environment, consider the following versions:

```
asmjit:x64-windows - 2024-06-28
zycore:x64-windows - 1.5.0
zydis:x64-windows - 4.1.0
z3:x64-windows - 4.13.0
```

In addition to these critical dependencies, some optional ones exist, for example, ```wxWidgets```, which is required to build RyujinGUI. It can be obtained from the [wxWidgets website](https://wxwidgets.org/downloads/).

## Research Paper

If you enjoy understanding how things work technically and exploring deep concepts, consider reading the **Ryūjin paper**:

**TODO**

## Getting Started

For more detailed usage information and explanations of each feature intended for daily use, consider reading the [**Ryūjin Wiki**](https://github.com/keowu/Ryujin/wiki).
