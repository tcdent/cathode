#include "CathodeBuildId.h"

#include "Misc/Paths.h"
#include "Modules/ModuleManager.h"

// --- Build-coherence link-ids (sprint 011 + [c]-prep cross-module hardening) ---
//
// A module's link-id is read from its loaded PE image: the linker rewrites the PE TimeDateStamp + CodeView PDB
// GUID on EVERY link of that DLL. Unlike a per-TU __DATE__/__TIME__ (stale unless that .cpp recompiles), the
// link-id advances on ANY relink of the module. CathodeLinkTimeBuildId reads CathodeRuntime's own image (the
// per-load marker). CathodeAggregateBuildId reads EVERY loaded Cathode module's image LIVE at call time and
// folds them into one cross-module anchor, so a relink of ANY Cathode module advances it. Cathode-internal:
// per-module PE reads of our own loaded images via the OS; NO DbgHelp/symbols, NO build-config/linker change,
// NO shared tooling — just the linker's existing output.

#if PLATFORM_WINDOWS
#include "Windows/AllowWindowsPlatformTypes.h"
#include <Windows.h>
#include "Windows/HideWindowsPlatformTypes.h"

namespace
{
// Parse a loaded PE image at Base -> "link:<TimeDateStamp> pdb:<GUID>" (its per-link signature). The single
// source for the PE walk — used for both the own-module id and each aggregated module.
FString LinkIdForImage(const uint8* const Base)
{
	if (Base == nullptr)
	{
		return TEXT("unresolved");
	}
	const IMAGE_DOS_HEADER* const Dos = reinterpret_cast<const IMAGE_DOS_HEADER*>(Base);
	if (Dos->e_magic != IMAGE_DOS_SIGNATURE)
	{
		return TEXT("unavailable");
	}
	const IMAGE_NT_HEADERS* const Nt = reinterpret_cast<const IMAGE_NT_HEADERS*>(Base + Dos->e_lfanew);
	if (Nt->Signature != IMAGE_NT_SIGNATURE)
	{
		return TEXT("unavailable");
	}

	// PE TimeDateStamp: the linker's per-link stamp (present in every PE).
	const uint32 TimeDateStamp = Nt->FileHeader.TimeDateStamp;

	// CodeView PDB GUID (CV_INFO_PDB70 'RSDS'): the linker's per-link signature, when a debug dir is present.
	FString PdbGuid = TEXT("none");
	const IMAGE_DATA_DIRECTORY& DebugDirEntry = Nt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_DEBUG];
	if (DebugDirEntry.VirtualAddress != 0 && DebugDirEntry.Size >= sizeof(IMAGE_DEBUG_DIRECTORY))
	{
		const IMAGE_DEBUG_DIRECTORY* const DebugDir =
			reinterpret_cast<const IMAGE_DEBUG_DIRECTORY*>(Base + DebugDirEntry.VirtualAddress);
		const int32 NumEntries = static_cast<int32>(DebugDirEntry.Size / sizeof(IMAGE_DEBUG_DIRECTORY));
		for (int32 Index = 0; Index < NumEntries; ++Index)
		{
			if (DebugDir[Index].Type == IMAGE_DEBUG_TYPE_CODEVIEW && DebugDir[Index].AddressOfRawData != 0)
			{
				const uint8* const Cv = Base + DebugDir[Index].AddressOfRawData;
				if (*reinterpret_cast<const uint32*>(Cv) == 0x53445352u /* 'RSDS' little-endian */)
				{
					const GUID& G = *reinterpret_cast<const GUID*>(Cv + sizeof(uint32));
					PdbGuid = FString::Printf(
						TEXT("%08X-%04X-%04X-%02X%02X-%02X%02X%02X%02X%02X%02X"),
						G.Data1, G.Data2, G.Data3,
						G.Data4[0], G.Data4[1], G.Data4[2], G.Data4[3],
						G.Data4[4], G.Data4[5], G.Data4[6], G.Data4[7]);
					break;
				}
			}
		}
	}

	return FString::Printf(TEXT("link:%08X pdb:%s"), TimeDateStamp, *PdbGuid);
}

// The loaded base address of a module by its on-disk file name, WITHOUT changing the module refcount.
const uint8* LoadedImageBaseByFileName(const FString& DllFileName)
{
	const HMODULE Handle = ::GetModuleHandleW(*DllFileName);
	return reinterpret_cast<const uint8*>(Handle);
}
} // namespace

FString Cathode::CathodeLinkTimeBuildId()
{
	HMODULE Self = nullptr;
	if (!::GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
			reinterpret_cast<LPCWSTR>(&Cathode::CathodeLinkTimeBuildId), &Self) || Self == nullptr)
	{
		return TEXT("unavailable");
	}
	return LinkIdForImage(reinterpret_cast<const uint8*>(Self));
}

FString Cathode::CathodeAggregateBuildId()
{
	// LIVE: enumerate every currently-loaded Cathode* module and fold its PE link-id into one anchor. The
	// prefix filter auto-covers every Cathode plugin module (new modules included without edit) — no baked list.
	// Sorted by name for determinism (same binaries -> identical aggregate on re-run). A relink of ANY module
	// changes its segment -> the aggregate advances. Walking the actually-loaded images at call (faculty) time
	// is what lets it witness a sibling's out-of-band relink — a build-time-baked manifest could not.
	TArray<FModuleStatus> Modules;
	FModuleManager::Get().QueryModules(Modules);

	TArray<TPair<FString, FString>> Folded;
	for (const FModuleStatus& Module : Modules)
	{
		if (!Module.bIsLoaded || !Module.Name.StartsWith(TEXT("Cathode")))
		{
			continue;
		}
		const FString DllFileName = FPaths::GetCleanFilename(Module.FilePath);
		const uint8* const Base = LoadedImageBaseByFileName(DllFileName);
		Folded.Emplace(Module.Name, LinkIdForImage(Base));
	}
	Folded.Sort([](const TPair<FString, FString>& A, const TPair<FString, FString>& B) { return A.Key < B.Key; });

	if (Folded.Num() == 0)
	{
		return TEXT("aggregate:none");
	}

	FString Aggregate;
	for (int32 Index = 0; Index < Folded.Num(); ++Index)
	{
		if (Index > 0)
		{
			Aggregate += TEXT("; ");
		}
		Aggregate += Folded[Index].Key + TEXT("=") + Folded[Index].Value;
	}
	return Aggregate;
}

#else // !PLATFORM_WINDOWS

FString Cathode::CathodeLinkTimeBuildId() { return TEXT("link-id-unavailable(non-Windows)"); }
FString Cathode::CathodeAggregateBuildId() { return TEXT("aggregate-unavailable(non-Windows)"); }

#endif
