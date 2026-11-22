#pragma once

#include "CoreMinimal.h"

THIRD_PARTY_INCLUDES_START
#include "quickjs.h"
THIRD_PARTY_INCLUDES_END

namespace Cathode
{

// The single binding-definition mechanism: each bindable member is described ONCE by an
// FBoundMember, and BOTH paths consume the SAME table — InstallBoundMembers installs the quickjs member, and
// EnumerateBoundMembersJson emits the descriptor. There is NO second list, so a member installed-but-not-
// enumerated (or vice versa) is structurally impossible — "the interface IS the bindings". The CATHODE_BIND_*
// macros (per owning module) emit BOTH the thunk and the table entry from one declaration.

enum class EBoundMemberKind : uint8
{
	Method,
	Property,
};

// One bindable member's definition. The thunk(s) are owned by the declaring module (they call its concrete
// C++ methods + marshalers); this struct carries the install handles + the descriptor metadata together.
struct FBoundMember
{
	const char* JsName = nullptr;                         // the JS member name
	const char* Iface = nullptr;                          // contract interface (Element/Node/...) — DECLARED metadata
	EBoundMemberKind Kind = EBoundMemberKind::Method;
	const char* Tag = nullptr;                            // required / margin / reflected / obs (carried metadata)
	const char* Signature = nullptr;                      // TS-form signature (carried metadata; optional)
	JSCFunction* Method = nullptr;                        // Method: the install thunk (generic 4-arg JSCFunction)
	JSCFunction* Getter = nullptr;                        // Property: the getter thunk (getter-shaped; see .cpp)
	JSCFunction* Setter = nullptr;                        // Property: optional setter thunk
};

// INSTALL: build a fresh prototype, install every member onto it, and JS_SetClassProto(ClassId, proto) — so the
// class's instances (created via the registry's JS_GetClassProto path) carry the members. Loops the SAME table
// EnumerateBoundMembersJson reads.
CATHODEBINDING_API void InstallBoundMembers(JSContext* Context, JSClassID ClassId, const FBoundMember* Members, int32 Count);

// ENUMERATE: emit the descriptor as JSON in the CathodeVerify/SCHEMA.md section-2 shape —
//   { "dom": [ { "iface", "name", "kind", "tag", "signature" }, ... ], "globals": [] }
// from the SAME FBoundMember[] the install loops. The CathodeVerify faculty consumes this as __CATHODE_DECLARED__.
CATHODEBINDING_API FString EnumerateBoundMembersJson(const FBoundMember* Members, int32 Count);

} // namespace Cathode
