#include "CathodeAuthoringDts.h"

#include "CathodeAuthoringTables.h"
#include "CathodeBoundMember.h"
#include "CathodeContractGlobalNames.h" // 025 S1 — GetContractGlobalNames (the install set the globals section binds to)

namespace Cathode
{
namespace
{
	// The standard DOM inheritance (the extends graph) — STRUCTURAL, not a member/signature list (the members + their
	// signatures stay FBoundMember-projected). The Iface-coverage guard (the coherence test) asserts every FBoundMember
	// Iface appears here, so a binding on a NEW interface flags. Base-first order (a base must be declared before a
	// subtype that extends it).
	struct FIfaceExtends { const TCHAR* Iface; const TCHAR* Base; };
	static const FIfaceExtends GIfaceExtends[] =
	{
		{ TEXT("EventTarget"),       nullptr },
		{ TEXT("Node"),              TEXT("EventTarget") },
		{ TEXT("Element"),           TEXT("Node") },
		{ TEXT("HTMLElement"),       TEXT("Element") },
		{ TEXT("HTMLInputElement"),  TEXT("HTMLElement") },
		{ TEXT("CharacterData"),     TEXT("Node") },
		{ TEXT("Text"),              TEXT("CharacterData") }, // createTextNode -> Text (no own members; inherits CharacterData.data)
		{ TEXT("Document"),          TEXT("Node") },
		{ TEXT("Event"),             nullptr },
		{ TEXT("KeyboardEvent"),     TEXT("Event") },
	};

	// Project ONE FBoundMember to a TS member declaration — name + signature BOTH from the binding's own fields (the
	// engine source). Method: "(params) -> Ret" -> `name(params): Ret;`. Property: `[readonly] name: Type;` (readonly
	// when the binding has no Setter). The "->"->": " + readonly are mechanical TS-syntax, not a hand-list.
	FString MemberToTs(const FBoundMember& M)
	{
		const FString Name = ANSI_TO_TCHAR(M.JsName);
		const FString Sig = M.Signature ? FString(ANSI_TO_TCHAR(M.Signature)) : FString(TEXT("unknown"));
		if (M.Kind == EBoundMemberKind::Method)
		{
			FString Params, Ret;
			if (Sig.Split(TEXT(" -> "), &Params, &Ret))
			{
				return Name + Params + TEXT(": ") + Ret + TEXT(";");
			}
			return Name + TEXT("(): ") + Sig + TEXT(";"); // signatureless fallback (a binding with no Signature)
		}
		const FString ReadOnly = (M.Setter == nullptr) ? TEXT("readonly ") : TEXT("");
		return ReadOnly + Name + TEXT(": ") + Sig + TEXT(";");
	}
}

FString GenerateAuthoringDts()
{
	// Gather the PRODUCTION binding tables (the SAME tables InstallBoundMembers registers).
	struct FTbl { const FBoundMember* Members = nullptr; int32 Count = 0; };
	FTbl Tables[4];
	GetDomNodeBoundMembersForCodegen(Tables[0].Members, Tables[0].Count);
	GetDomDocumentBoundMembersForCodegen(Tables[1].Members, Tables[1].Count);
	GetDomEventBoundMembersForCodegen(Tables[2].Members, Tables[2].Count);
	GetDomKeyboardEventBoundMembersForCodegen(Tables[3].Members, Tables[3].Count);

	// Group every member under its declaring Iface (the per-member Iface flows through from the table).
	TMap<FString, TArray<const FBoundMember*>> ByIface;
	for (const FTbl& T : Tables)
	{
		for (int32 i = 0; i < T.Count; ++i)
		{
			ByIface.FindOrAdd(FString(ANSI_TO_TCHAR(T.Members[i].Iface))).Add(&T.Members[i]);
		}
	}

	FString Out;
	// --- header ---
	Out += TEXT("// lib.cathode.d.ts -- the Cathode authoring type surface (GENERATED -- do not hand-edit).\n");
	Out += TEXT("// 025 S1 (Model A): the DOM/JS member interfaces are PROJECTED from the production FBoundMember binding\n");
	Out += TEXT("// tables (signatures from each member's own field). Regenerate via the regenerate-mode test; the coherence\n");
	Out += TEXT("// test asserts this committed file == the regeneration. Authors compile against this file with a tsconfig\n");
	Out += TEXT("// that OMITS \"dom\" -- any unsupported web API (fetch/localStorage/innerHTML/...) is a type error.\n");
	// NO `export {};` here: the .d.ts must be an AMBIENT-GLOBAL script, NOT a module — so its globals/interfaces resolve
	// in author code WITHOUT an import. (025 S4 proved an `export {};` makes it a module -> tsc rejects even console/document
	// -> the gate is broken. The ambient form is the only one where the gate discriminates supported-vs-banned.)
	Out += TEXT("\n");

	// --- the standard globals: BOUND to the install set (GetContractGlobalNames + document via InstallAsGlobal). One
	// global per install-set NAME; the SIGNATURE hand-authored per name (no source for runtime-installed signatures) --
	// a name with NO authored signature emits an `// ERROR` the guard catches, so a NEW install global can't silently miss. ---
	Out += TEXT("// --- Standard globals (Cathode-provided; BOUND to the install set; an install add/remove flows through) ---\n");
	{
		const TMap<FString, FString> GlobalSignatures =
		{
			{ TEXT("console"),               TEXT("declare const console: { log(...args: unknown[]): void; warn(...args: unknown[]): void; error(...args: unknown[]): void; };") },
			{ TEXT("setTimeout"),            TEXT("declare function setTimeout(handler: () => void, timeout?: number): number;") },
			{ TEXT("clearTimeout"),          TEXT("declare function clearTimeout(id: number): void;") },
			{ TEXT("requestAnimationFrame"), TEXT("declare function requestAnimationFrame(callback: (time: number) => void): number;") },
			{ TEXT("cancelAnimationFrame"),  TEXT("declare function cancelAnimationFrame(id: number): void;") },
			{ TEXT("document"),              TEXT("declare const document: Document;") },
		};
		TArray<FString> GlobalNames = GetContractGlobalNames(); // the CathodeContractGlobals install set (the single source)
		GlobalNames.Add(TEXT("document"));                     // the documented known: FDomDocument::InstallAsGlobal (per-surface)
		for (const FString& Name : GlobalNames)
		{
			if (const FString* Sig = GlobalSignatures.Find(Name)) { Out += *Sig + TEXT("\n"); }
			else { Out += TEXT("// ERROR: install-set global '") + Name + TEXT("' has NO authored signature -- add it to GlobalSignatures.\n"); }
		}
	}
	Out += TEXT("\n");

	// --- auxiliary referenced types (named in projected signatures; no binding table; RESOLVE-guarded) ---
	Out += TEXT("// --- Auxiliary referenced types (referenced by signatures; no binding table; RESOLVE-guarded) ---\n");
	Out += TEXT("type EventListener = (event: Event) => void;\n");
	Out += TEXT("interface NodeList { readonly length: number; item(index: number): Node | null; [index: number]: Node; }\n");
	Out += TEXT("interface NamedNodeMap { readonly length: number; }\n");
	Out += TEXT("interface DOMRect { readonly x: number; readonly y: number; readonly width: number; readonly height: number; readonly top: number; readonly right: number; readonly bottom: number; readonly left: number; }\n");
	Out += TEXT("interface CSSStyleDeclaration { getPropertyValue(property: string): string; setProperty(property: string, value: string): void; removeProperty(property: string): string; cssText: string; }\n\n");

	// --- the FBoundMember-projected interfaces (the generated member CORE; the extends edges structural) ---
	for (const FIfaceExtends& IE : GIfaceExtends)
	{
		const FString Iface = IE.Iface;
		Out += TEXT("interface ") + Iface;
		if (IE.Base != nullptr) { Out += TEXT(" extends ") + FString(IE.Base); }
		Out += TEXT(" {\n");
		if (const TArray<const FBoundMember*>* Members = ByIface.Find(Iface))
		{
			for (const FBoundMember* M : *Members)
			{
				Out += TEXT("\t") + MemberToTs(*M) + TEXT("\n");
			}
		}
		Out += TEXT("}\n\n");
	}

	return Out;
}

} // namespace Cathode
