#include "AutoGen.h"
#include "ProcessorBind.h"
#include "Uefi/UefiBaseType.h"
#include "Uefi/UefiMultiPhase.h"
#include "Uefi/UefiSpec.h"
#include <Library/BaseMemoryLib.h>
#include <Library/MemoryAllocationLib.h>
#include <Library/UefiBootServicesTableLib.h>
#include <Library/UefiLib.h>
#include <Uefi.h>

STATIC VOID WaitForKeyPress(VOID) {
  UINTN Index;
  EFI_INPUT_KEY Key;
  Print(L"[...] Press any key to continue\n");
  gBS->WaitForEvent(1, &gST->ConIn->WaitForKey, &Index);
  gST->ConIn->ReadKeyStroke(gST->ConIn, &Key);
}

EFI_EXIT_BOOT_SERVICES OriginalExitBootServices = NULL;

// NdisMResetMiniport(...) function signature in Ndis.sys (Tiny 11 23H2)
// 40 53          PUSH       RBX
// 48 83 ec 30    SUB        RSP,0x30
// 48 8b d9       MOV        RBX, param_1
// First 9 bytes pattern - register-only operations contain no embedded
// absolute addresses that PE ASLR would modify, making the sequence stable
// regardless of where ndis.sys is loaded in memory.
UINT8 NdisResetSignature[] = {0x40, 0x53, 0x48, 0x83, 0xEC,
                              0x30, 0x48, 0x8B, 0xD9};
UINTN SignatureLength = 9;

// x86_64 RET instruction (1 byte)
UINT8 PatchByte = 0xC3;

/*
 * PatchNdisReset linearly searches for the NdisResetMiniport function signature
 * in EfiLoaderCode regions and patches it with the RET instruction to disable
 * it.
 */
EFI_STATUS EFIAPI PatchNdisReset(VOID) {
  EFI_STATUS Status;
  UINTN MemoryMapSize = 0;
  EFI_MEMORY_DESCRIPTOR *MemoryMap = NULL;
  UINTN MapKey, DescriptorSize;
  UINT32 DescriptorVersion;

  Status = gBS->GetMemoryMap(&MemoryMapSize, MemoryMap, &MapKey,
                             &DescriptorSize, &DescriptorVersion);

  MemoryMapSize += (DescriptorSize * 2);
  MemoryMap = AllocatePool(MemoryMapSize);

  Status = gBS->GetMemoryMap(&MemoryMapSize, MemoryMap, &MapKey,
                             &DescriptorSize, &DescriptorVersion);
  if (EFI_ERROR(Status)) {
    FreePool(MemoryMap);
    return Status;
  }

  UINTN NumEntries = MemoryMapSize / DescriptorSize;
  EFI_MEMORY_DESCRIPTOR *CurrentDescriptor = MemoryMap;

  for (UINTN i = 0; i < NumEntries; i++) {
    if (CurrentDescriptor->Type == EfiLoaderCode) {
      UINT8 *ScanPtr = (UINT8 *)(UINTN)CurrentDescriptor->PhysicalStart;
      UINTN BlockSize = CurrentDescriptor->NumberOfPages * EFI_PAGE_SIZE;

      for (UINTN j = 0; j < (BlockSize - SignatureLength); j++) {
        // Micro-optimization: skip if the first byte of the signature doesn't
        // match
        if (ScanPtr[j] == NdisResetSignature[0]) {
          if (CompareMem(&ScanPtr[j], NdisResetSignature, SignatureLength) ==
              0) {
            ScanPtr[j] = PatchByte;
            gST->ConOut->OutputString(
                gST->ConOut,
                L"[+] XBS-Hook: NdisResetMiniport suppressed!\r\n");

            FreePool(MemoryMap);
            return EFI_SUCCESS;
          }
        }
      }
    }

    CurrentDescriptor =
        (EFI_MEMORY_DESCRIPTOR *)((UINT8 *)CurrentDescriptor + DescriptorSize);
  }

  FreePool(MemoryMap);
  Print(L"[-] XBS-Hook: Signature not found in EfiLoaderCode regions.\n");
  return EFI_NOT_FOUND;
}

/*
 * HookedExitBootServices overrides the original ExitBootServices function and
 * calls PatchNdisReset() to disable the NdisResetMiniport function.
 */
EFI_STATUS EFIAPI HookedExitBootServices(IN EFI_HANDLE ImageHandle,
                                         IN UINTN MapKey) {
  EFI_STATUS PatchStatus = PatchNdisReset();
  if (EFI_ERROR(PatchStatus)) {
    Print(L"[-] XBS-Hook: Patch FAILED (Status: %r). Proceeding anyway.\n",
          PatchStatus);
  }
  WaitForKeyPress();

  // Must call the original ExitBootServices function.
  // Otherwise the Windows kernel will BSOD on startup.
  return OriginalExitBootServices(ImageHandle, MapKey);
}

EFI_STATUS EFIAPI UefiMain(IN EFI_HANDLE ImageHandle,
                           IN EFI_SYSTEM_TABLE *SystemTable) {
  EFI_BOOT_SERVICES *BS = SystemTable->BootServices;

  Print(L"[*] XBS-Hook: Initializing NDIS Patcher...\n");

  OriginalExitBootServices = BS->ExitBootServices;

  BS->ExitBootServices = HookedExitBootServices;

  // Must recalculate the CRC32 field in the Boot Services Table header after
  // modifying it. The UEFI spec requires this field to remain valid; firmware
  // or early-boot integrity checks may reject the table if the checksum is stale.
  BS->Hdr.CRC32 = 0;
  BS->CalculateCrc32(BS, BS->Hdr.HeaderSize, &BS->Hdr.CRC32);

  Print(L"[+] XBS-Hook: Patch installed and awaiting iSCSI Windows boot\n");
  WaitForKeyPress();

  return EFI_SUCCESS;
}
