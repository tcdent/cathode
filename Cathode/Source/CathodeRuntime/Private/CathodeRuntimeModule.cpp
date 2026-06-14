#include "CathodeRuntimeModule.h"

#include "CathodeBuildId.h"
#include "CathodeRuntimeWorker.h"

#include "CoreMinimal.h"
#include "Modules/ModuleManager.h"

// Build-coherence marker (sprint 010): a log category for the build signature below.
DEFINE_LOG_CATEGORY_STATIC(LogCathodeBuild, Log, All);

// --- Build-coherence: a LINK-TIME build-id (sprint 011 coherence-hardening) ---
//
// Read from THIS module's own loaded PE image at startup: the linker rewrites the PE TimeDateStamp + the
// CodeView PDB GUID on EVERY link of CathodeRuntime.dll. Unlike a per-TU __DATE__/__TIME__ (stale unless
// THIS .cpp recompiles), these advance on ANY relink — including a relink that recompiles only a NON-marker
// TU — so [CATHODE-BUILD-SIGNATURE] becomes a true per-result-run coherence anchor (retiring the per-TU
// caveat). Cathode-internal: reads our own loaded image; NO DbgHelp/symbols, NO build-config/linker-flag
// change, NO shared tooling — just the linker's existing output.
#if PLATFORM_WINDOWS
#include "Windows/AllowWindowsPlatformTypes.h"
#include <Windows.h>
#include "Windows/HideWindowsPlatformTypes.h"

FString Cathode::CathodeLinkTimeBuildId()
{
	HMODULE Self = nullptr;
	if (!::GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
			reinterpret_cast<LPCWSTR>(&Cathode::CathodeLinkTimeBuildId), &Self) || Self == nullptr)
	{
		return TEXT("unavailable");
	}

	const uint8* const Base = reinterpret_cast<const uint8*>(Self);
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
#else
FString Cathode::CathodeLinkTimeBuildId() { return TEXT("link-id-unavailable(non-Windows)"); }
#endif

FCathodeRuntimeModule::~FCathodeRuntimeModule() = default;

void FCathodeRuntimeModule::StartupModule()
{
	// BUILD-COHERENCE MARKER (sprint 010 + 011 hardening): the [CATHODE-BUILD-SIGNATURE] line carries a
	// LINK-TIME build-id (read from this module's own PE image — advances on ANY CathodeRuntime relink,
	// including relinks that do NOT recompile this TU). That build-id is the per-result-run coherence anchor:
	// grep a run log for [CATHODE-BUILD-SIGNATURE] and compare the build-id to confirm the run is on the
	// current binary. The "(module-TU compiled ...)" suffix is the compile time of THIS TU only —
	// supplementary context, NOT the anchor (it is exactly the per-TU value the link-time build-id supersedes).
	UE_LOG(LogCathodeBuild, Display, TEXT("[CATHODE-BUILD-SIGNATURE] Cathode plugin loaded; build-id %s (module-TU compiled %s %s)"),
		*Cathode::CathodeLinkTimeBuildId(), ANSI_TO_TCHAR(__DATE__), ANSI_TO_TCHAR(__TIME__));

	// Auto-start on module load (human-ruled): create the worker AND start its thread, which runs the
	// ordered Startup() sequence (create -> budgets -> assemble globals -> load author JS -> begin
	// ticking). The linked-native game-state object (game.*) attaches AT startup, not lazily — deferring
	// the worker would defer that attach and risk inconsistent state. A plugin included in the engine is
	// assumed to be in use, so there is no idle-spin-up to avoid.
	Worker = MakeUnique<Cathode::FCathodeRuntimeWorker>();
	Worker->Start();
}

void FCathodeRuntimeModule::ShutdownModule()
{
	TeardownWorker();
}

void FCathodeRuntimeModule::TeardownWorker()
{
	// SINGLE-SOURCE teardown (shared by ShutdownModule + the test-only StopRuntimeForTesting): Worker::Shutdown
	// joins the worker thread and tears the runtime down ON that thread (affinity-correct), then
	// ~FQuickJsRuntime releases the single-instance claim. Idempotent: no-op if no worker is live.
	if (Worker.IsValid())
	{
		Worker->Shutdown();
		Worker.Reset();
	}
}

#if WITH_DEV_AUTOMATION_TESTS
void FCathodeRuntimeModule::StopRuntimeForTesting()
{
	// Run the EXACT production teardown (the shared helper) — no test-specific path that could diverge.
	// After this the claim is free + no runtime is live (coherent), so an automation test can
	// CreateJsRuntime() its own instance on the TEST thread.
	TeardownWorker();
}
#endif

IMPLEMENT_MODULE(FCathodeRuntimeModule, CathodeRuntime)
