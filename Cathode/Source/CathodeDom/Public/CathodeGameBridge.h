#pragma once

#include "CoreMinimal.h"

THIRD_PARTY_INCLUDES_START
#include "quickjs.h"
THIRD_PARTY_INCLUDES_END

namespace Cathode
{

// 029 #28 — the game.* HOST BRIDGE backing: the real-shaped surface the bound game.* thunks call. A STUB implements it for
// this slice; the sim hands in the real object later. The bridge is NOT hardcoded to the stub — InstallGameBridgeAsGlobal
// takes the backing, so a DIFFERENT ICathodeGameBridge wires in unchanged (the stub-swap proof).
class CATHODEDOM_API ICathodeGameBridge
{
public:
	virtual ~ICathodeGameBridge() = default;
	virtual int32 GetTickCount() const = 0;                                   // backs game.tickCount (a read-only property)
	virtual bool GetEntityName(const FString& Id, FString& OutName) const = 0; // backs game.getEntityName(id); false => null
};

// The real-shaped STUB backing (029 #28 placeholder — deterministic values; the sim defines the real surface later).
CATHODEDOM_API TUniquePtr<ICathodeGameBridge> CreateStubGameBridge();

// Install the `game` GLOBAL object onto the live context — table-driven from the GGameMembers FBoundMember table (the SOLE
// source the codegen also reads via GetGameBoundMembersForCodegen, so install-set == codegen-set by construction), with
// Bridge as the backing the thunks call. The COMPOSER call-site (beside FDomDocument::InstallAsGlobal); game is a
// DOCUMENTED-KNOWN global like document, NOT a contract-globals seam entry (029 #28, design B — the document precedent).
CATHODEDOM_API void InstallGameBridgeAsGlobal(JSContext* Context, ICathodeGameBridge& Bridge);

} // namespace Cathode
