#include "CathodeBoundMember.h"

namespace Cathode
{

void InstallBoundMembers(JSContext* Context, JSClassID ClassId, const FBoundMember* Members, int32 Count)
{
	// One prototype carries every member; the class's instances pick it up via JS_GetClassProto at creation.
	JSValue Proto = JS_NewObject(Context);
	for (int32 Index = 0; Index < Count; ++Index)
	{
		const FBoundMember& Member = Members[Index];
		if (Member.Kind == EBoundMemberKind::Method)
		{
			// A method is a plain function-valued property on the prototype.
			JS_SetPropertyStr(Context, Proto, Member.JsName,
				JS_NewCFunction(Context, Member.Method, Member.JsName, 0));
		}
		else
		{
			// A property is an accessor. The getter/setter thunks carry the getter/setter calling conventions;
			// JS_NewCFunction2 dispatches on the cproto (JS_CFUNC_getter / JS_CFUNC_setter), the quickjs idiom.
			const JSValue Getter = Member.Getter
				? JS_NewCFunction2(Context, Member.Getter, Member.JsName, 0, JS_CFUNC_getter, 0)
				: JS_UNDEFINED;
			const JSValue Setter = Member.Setter
				? JS_NewCFunction2(Context, Member.Setter, Member.JsName, 1, JS_CFUNC_setter, 0)
				: JS_UNDEFINED;
			const JSAtom Atom = JS_NewAtom(Context, Member.JsName);
			JS_DefinePropertyGetSet(Context, Proto, Atom, Getter, Setter,
				JS_PROP_CONFIGURABLE | JS_PROP_ENUMERABLE);
			JS_FreeAtom(Context, Atom);
		}
	}
	JS_SetClassProto(Context, ClassId, Proto); // consumes Proto
}

namespace
{
// Minimal JSON string escaping for the carried metadata (controlled inputs; defensive on " and backslash).
FString EscapeJson(const char* Ansi)
{
	FString Out;
	if (Ansi == nullptr)
	{
		return Out;
	}
	const FString In = ANSI_TO_TCHAR(Ansi);
	for (const TCHAR Ch : In)
	{
		if (Ch == TEXT('"') || Ch == TEXT('\\'))
		{
			Out += TEXT('\\');
		}
		Out += Ch;
	}
	return Out;
}
} // namespace

FString EnumerateBoundMembersJson(const FBoundMember* Members, int32 Count)
{
	FString Dom;
	for (int32 Index = 0; Index < Count; ++Index)
	{
		const FBoundMember& Member = Members[Index];
		if (Index > 0)
		{
			Dom += TEXT(",");
		}
		Dom += FString::Printf(
			TEXT("{\"iface\":\"%s\",\"name\":\"%s\",\"kind\":\"%s\",\"tag\":\"%s\",\"signature\":\"%s\"}"),
			*EscapeJson(Member.Iface),
			*EscapeJson(Member.JsName),
			Member.Kind == EBoundMemberKind::Method ? TEXT("method") : TEXT("property"),
			*EscapeJson(Member.Tag),
			*EscapeJson(Member.Signature));
	}
	return FString::Printf(TEXT("{\"dom\":[%s],\"globals\":[]}"), *Dom);
}

} // namespace Cathode
