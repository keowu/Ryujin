# Ryūjin Protector

**Ryūjin Protector** is an open-source Bin2Bin obfuscation, protection, and DRM tool for Windows PE binaries targeting the Intel x64 architecture (x86_64 only).

<p align="center">
  <img src="imgs/ryujinreadme.png" alt="Ryūjin Protector Banner" width="70%">
</p>

---
## Overview

Ryūjin is a research-grade protector and obfuscator built to explore Bin2Bin transformation techniques. It is suitable for security researchers, reverse engineers, anti-cheat and anti-tamper developers, and others studying binary protection. Ryūjin focuses on transforming binaries while preserving original program semantics.

---
## Features

- Junk code insertion and code mutation (randomized while preserving original logic)
- IAT call access obfuscation (with obfuscated handler access)
- Random section naming (default: `Ryujin`)
- Mathematical operator virtualization (Ryūjin MiniVM)
- Mathematical operator virtualization with Hyper-V isolation (MiniVM + Hyper-V)
- Encrypted code sections (TeaDelKew algorithm)
- Anti-debug (user-mode and kernel-mode)
- TrollReversers (can induce BSOD during reversing attempts)
- Anti-dump protections
- Anti-disassembly and anti-decompiler techniques
- Memory integrity protection (CRC32-based)
- Custom pass support (includes **MBA-linear Obfuscation Pass** and community passes)
- And more, see the wiki for a complete list

---

## Demos and Presentations

Ryūjin is intended primarily as a study and research tool. Below are small demos showing a `main` function before and after a Ryūjin pass:

![Ryūjin Protector Demo](imgs/demo.png)

> This demo shows one feature; combining multiple features produces stronger obfuscation. See the Ryūjin Wiki for full examples and explanations.

### Modes of use

Ryūjin supports both CLI and GUI:

- **CLI:** Full functionality and advanced options for expert/research users.
- **GUI:** Simplified interface for quick runs (requires `wxWidgets`).

CLI demo:
![Ryūjin Protector Demo Console](imgs/demo2.png)

GUI demo:
![Ryūjin Protector Demo GUI](imgs/demo1.png)

**Input requirements:** a PE executable (EXE) for x64 and its PDB file containing symbols. The PDB is required to enable several transformation passes that rely on symbol information.

---
## Custom Pass Support

Ryūjin supports custom passes through a simple callback model. A custom pass receives a `RyujinProcedure` instance and can modify basic blocks, scopes, or other procedure-level structures.

Example callback signature:

```c++
void RyujinCustomPassDemo(RyujinProcedure* proc);
````

See the class definition here:  
**[RyujinProcedure.hh](https://github.com/keowu/Ryujin/blob/main/RyujinCore/Ryujin/Models/RyujinProcedure.hh).**

Example usage is included here:  
**[RyujinCustomPasses.hh](https://github.com/keowu/Ryujin/blob/main/RyujinConsole/RyujinConsole/RyujinCustomPasses.hh#L11).**

No additional configuration changes are required. `RyujinObfuscatorConfig` already exposes the settings needed to register and run custom passes.

---
## Ryūjin Bin2Bin Obfuscator Core - Structure/Design Diagram 

<div style="max-width:100%; max-height:600px; overflow:auto; border:1px solid #ddd; padding:4px;">
  <img src="Diagrams/ryujin_core.png" alt="Large view" style="display:block; max-width:none;">
</div>

---
## Dependencies

Install dependencies via [Microsoft vcpkg](https://github.com/microsoft/vcpkg):

```bash
vcpkg install asmjit
vcpkg install zydis
vcpkg install z3
```

Recommended versions for a consistent build environment:

```
asmjit:x64-windows - 2024-06-28
zycore:x64-windows  - 1.5.0
zydis:x64-windows   - 4.1.0
z3:x64-windows      - 4.13.0
```

Optional dependency for the GUI:

- `wxWidgets` - obtain from [https://wxwidgets.org/downloads/](https://wxwidgets.org/downloads/)

---
## Research Paper

> To better understand Ryūjin, please read the research paper:
[**Ryūjin - Writing a Bin2Bin Obfuscator from Scratch for Windows PE x64 and Fully Deobfuscating It**](https://keowu.re/posts/Ry%C5%ABjin---Writing-a-Bin2Bin-Obfuscator-from-Scratch-for-Windows-PE-x64-and-Fully-Deobfuscating-It)

---
## Getting Started

For usage examples, flags, advanced options, and full explanations of each feature, see the   
[Ryūjin Wiki](https://github.com/keowu/Ryujin/wiki/Ry%C5%ABjin-Wiki).

---
## Detection & Analysis

Found malware protected with this? Wondering what to do?

Read the article carefully. from start to finish, don’t skip any part. and you’ll be fully prepared to analyze malware protected with this project.

**This section is reserved for the write-up of the first company that identifies and successfully reverses malware using this project.**

---
## Licenses

- Ryujin and all its components are distributed under the **GPL-2** license.
- Ryujin’s paper is distributed under the **CC BY 4.0** license.