#include "CathodeGameBridge.h"

#include "CathodeBinding.h"         // UnmarshalString (string arg unmarshal)
#include "CathodeBoundMember.h"     // FBoundMember, EBoundMemberKind — the SINGLE binding-definition mechanism
#include "CathodeAuthoringTables.h" // GetGameBoundMembersForCodegen (the codegen accessor, defined here)

namespace Cathode
{

// The backing the game.* thunks call. A SINGLE shared pointer (game is a SINGLETON world — every surface's game.* reaches
// the one world; document, by contrast, is per-surface and uses a per-instance opaque). InstallGameBridgeAsGlobal sets it;
// a different bridge handed in -> the thunks call IT (the stub-swap). Non-owning — the composer/test owns the bridge.
static ICathodeGameBridge* GInstalledGameBridge = nullptr;

// ---- The member thunks: recover the backing from GInstalledGameBridge, call it, marshal the result back to JS. ----
JSValue GameTickCountGetterThunk(JSContext* Ctx, JSValueConst /*ThisVal*/)
{
	if (GInstalledGameBridge == nullptr) { return JS_NULL; }
	return JS_NewInt32(Ctx, GInstalledGameBridge->GetTickCount());
}

JSValue GameGetEntityNameThunk(JSContext* Ctx, JSValueConst /*ThisVal*/, int Argc, JSValueConst* Argv)
{
	if (GInstalledGameBridge == nullptr) { return JS_NULL; }
	FString Id;
	if (Argc < 1 || !UnmarshalString(Ctx, Argv[0], Id)) { return JS_ThrowTypeError(Ctx, "game.getEntityName expects a string id"); }
	FString Name;
	if (!GInstalledGameBridge->GetEntityName(Id, Name)) { return JS_NULL; }
	return JS_NewString(Ctx, TCHAR_TO_UTF8(*Name));
}

// ---- The CATHODE_BIND_GAME_* macros: emit BOTH the thunk reference AND the table entry from ONE declaration (the single
// source — a member added here is BOTH installed AND projected by the codegen, never one without the other). The getter is
// reinterpret_cast because its (Ctx, ThisVal) shape differs from the 4-arg JSCFunction (the DOM getter idiom). ----
#define CATHODE_BIND_GAME_METHOD(JsName, Tag, Sig, ThunkFn) \
	{ JsName, "Game", EBoundMemberKind::Method, Tag, Sig, &ThunkFn, nullptr, nullptr }
#define CATHODE_BIND_GAME_GETTER(JsName, Tag, Sig, GetterFn) \
	{ JsName, "Game", EBoundMemberKind::Property, Tag, Sig, nullptr, reinterpret_cast<JSCFunction*>(&GetterFn), nullptr }

// ---- GGameMembers — THE SINGLE SOURCE (consumed by the install below AND by GetGameBoundMembersForCodegen). The stub's
// real-shaped game.* surface (029 #28 placeholder; the sim defines the real members later). ----
static const FBoundMember GGameMembers[] =
{
	CATHODE_BIND_GAME_GETTER("tickCount",     "required", "number",                        GameTickCountGetterThunk),
	CATHODE_BIND_GAME_METHOD("getEntityName", "required", "(id: string) -> string | null", GameGetEntityNameThunk),
};

// ---- The codegen accessor: returns the SAME table the install reads (Model A — the .d.ts game.* == the installed surface
// by construction; a member add/remove flows through both). ----
void GetGameBoundMembersForCodegen(const FBoundMember*& OutMembers, int32& OutCount)
{
	OutMembers = GGameMembers; OutCount = UE_ARRAY_COUNT(GGameMembers);
}

// ---- The plain-object table-driven install (LEAN 1 — game is a singleton OBJECT, not a wrapped class; a sibling of
// InstallBoundMembers that builds a plain `game` object instead of a class prototype, off the SAME GGameMembers table).
// Sets the backing, builds `game` from the table, sets it as the global `game` (a documented-known global, like document). ----
void InstallGameBridgeAsGlobal(JSContext* Context, ICathodeGameBridge& Bridge)
{
	GInstalledGameBridge = &Bridge; // the thunks recover the (possibly-swapped) backing here — the stub-swap point
	JSValue Game = JS_NewObject(Context);
	for (const FBoundMember& Member : GGameMembers)
	{
		if (Member.Kind == EBoundMemberKind::Method)
		{
			JS_SetPropertyStr(Context, Game, Member.JsName, JS_NewCFunction(Context, Member.Method, Member.JsName, 0));
		}
		else
		{
			const JSValue Getter = Member.Getter
				? JS_NewCFunction2(Context, Member.Getter, Member.JsName, 0, JS_CFUNC_getter, 0)
				: JS_UNDEFINED;
			const JSValue Setter = Member.Setter
				? JS_NewCFunction2(Context, Member.Setter, Member.JsName, 1, JS_CFUNC_setter, 0)
				: JS_UNDEFINED;
			const JSAtom Atom = JS_NewAtom(Context, Member.JsName);
			JS_DefinePropertyGetSet(Context, Game, Atom, Getter, Setter, JS_PROP_CONFIGURABLE | JS_PROP_ENUMERABLE);
			JS_FreeAtom(Context, Atom);
		}
	}
	JSValue Global = JS_GetGlobalObject(Context);
	JS_SetPropertyStr(Context, Global, "game", Game); // the DOCUMENTED-KNOWN `game` global (codegen lists it + GUARD1 expects it)
	JS_FreeValue(Context, Global);
}

// ---- The real-shaped STUB (029 #28 placeholder; deterministic so the reachability proof asserts exact values, and a
// DIFFERENT bridge in the stub-swap proof yields DIFFERENT values). ----
namespace
{
class FStubGameBridge final : public ICathodeGameBridge
{
public:
	virtual int32 GetTickCount() const override { return 42; } // a fixed stub tick (the sim's real tick later)
	virtual bool GetEntityName(const FString& Id, FString& OutName) const override
	{
		OutName = TEXT("stub:") + Id; // a deterministic stub name (the sim's real entity lookup later)
		return true;
	}
};
} // namespace

TUniquePtr<ICathodeGameBridge> CreateStubGameBridge()
{
	return MakeUnique<FStubGameBridge>();
}

} // namespace Cathode
