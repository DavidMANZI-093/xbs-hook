# SOP-DEV-01: EDK II Build Environment — Arch Linux

**Document Control**

| Field | Value |
|---|---|
| Scope | EDK II toolchain setup and xbs-hook C package build on Arch Linux |
| Host OS | Arch Linux (x86_64) |
| Date Verified | May 2026 |
| Status | Validated |
| Related | [SOP-SRV-01: iSCSI Target & PXE Provisioning](./sop-srv-01.md) |

---

## 1. Toolchain Installation

```bash
sudo pacman -S clang bear llvm nasm
```

| Package | Role in build |
|---|---|
| `clang` / `llvm` | Required by EDK II's GCC toolchain profile for compilation and linking |
| `bear` | Wraps the build command to emit `compile_commands.json` for clangd LSP |
| `nasm` | EDK II requires it for x86 assembly stubs in MdePkg |

---

## 2. EDK II Clone & BaseTools

EDK II lives at `~/edk2`, separate from the xbs-hook package repo. The `PACKAGES_PATH` environment variable bridges them at build time.

```bash
git clone https://github.com/tianocore/edk2.git ~/edk2
cd ~/edk2
git submodule update --init
make -C BaseTools
```

`make -C BaseTools` compiles the host-side build tools (parsers, generators). This only needs to run once unless the edk2 checkout changes.

---

## 3. Package Structure

| File | Role |
|---|---|
| `XbsHookPkg.dec` | Declares the package to the build system. Required even if no APIs are exported. |
| `XbsHookPkg.dsc` | Platform descriptor — resolves all library class implementations used by the linker. |
| `Application/XbsHook/XbsHook.inf` | Module descriptor — declares source files, library dependencies, and the entry point symbol. |
| `Application/XbsHook/main.c` | The application source. |

---

## 4. Build

Run these from the xbs-hook repo root. Both environment steps are required each session.

```bash
# 4.1 Initialize the EDK II build environment
cd ~/edk2
source edksetup.sh

# 4.2 Set the path so the build system can find XbsHookPkg
cd ~/git-repos/xbs-hook
export PACKAGES_PATH=~/git-repos/xbs-hook

# 4.3 Build (bear wraps the command to produce compile_commands.json)
bear -- build -a X64 -t GCC -p XbsHookPkg.dsc
```

Output EFI: `~/edk2/Build/XbsHook/DEBUG_GCC/X64/XbsHook.efi`

> `source edksetup.sh` and `export PACKAGES_PATH` must be run each session. They are not persisted to the shell profile.

---

## 5. Build Shell.efi (UEFI Shell)

The UEFI Shell provides `ls`, `cd`, `map`, `FS[n]:` — required for filesystem navigation at the UEFI layer. Build it once from the `ShellPkg` already included in the edk2 clone.

```bash
# From the edk2 directory, with edksetup.sh already sourced
build -a X64 -t GCC -p ShellPkg/ShellPkg.dsc
```

Output EFI: `~/edk2/Build/Shell/DEBUG_GCC/X64/Shell_EA4BB293-2D7F-4456-A681-1F22F42CD0BC.efi`

The `.pdb` copy warning during this build is harmless — no debug symbols on Linux.

---

## 6. Deploy for Testing

Transfer the following files to `/srv/tftp/` on the Alpine server. Use whichever transfer method suits your setup (scp, rsync, shared mount, etc.):

```
Source:      ~/edk2/Build/XbsHook/DEBUG_GCC/X64/XbsHook.efi
Destination: /srv/tftp/XbsHook.efi   (on the Alpine server)

Source:      ~/edk2/Build/Shell/DEBUG_GCC/X64/Shell_EA4BB293-2D7F-4456-A681-1F22F42CD0BC.efi
Destination: /srv/tftp/Shell.efi     (on the Alpine server — rename on transfer)
```

Reboot the client — iPXE will fetch both binaries over TFTP and execute them in sequence.

---

## 7. Teardown

Remove the development environment from the Arch machine.

```bash
# Remove the edk2 clone and all build artifacts
rm -rf ~/edk2

# Remove toolchain packages
sudo pacman -Rns clang llvm bear nasm
```

The xbs-hook repo (`~/git-repos/xbs-hook`) is not touched — it contains only source files and docs.
