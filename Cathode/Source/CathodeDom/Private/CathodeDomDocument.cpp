#include "CathodeDom.h"

#include "CathodeBoundMember.h" // sprint 012: the single binding-definition mechanism (install + enumerate)

THIRD_PARTY_INCLUDES_START
#include "quickjs.h"
THIRD_PARTY_INCLUDES_END

namespace Cathode
{

FDomDocument::FDomDocument(ICathodeJsRuntime& InRuntime, INativeClassRegistry& InRegistry, ILayoutEngine& InLayout,
	JSClassID InNodeClassId)
	: Runtime(InRuntime)
	, Registry(InRegistry)
	, Layout(InLayout)
	, Context(static_cast<JSContext*>(InRuntime.GetNativeContextHandle()))
	, NodeClassId(InNodeClassId)
{
	// The document OWNS the root (Model A, human ruling): create + hold the root element's wrapper. Disposing
	// the document drops this ref -> the tree reclaims by GC.
	RootWrapper = CreateElement(TEXT("html"));
	if (const FDomNode* Root = NodeFromWrapper(RootWrapper))
	{
		RootNodeId = Root->GetNodeId();
	}
	Layout.SetDocument(*this); // the DOM registers itself as 005's layout source (structure + markup)
}

// === TEARDOWN (007's pattern: gc-at-runtime + JS_FreeRuntime fallback) ===
//
// The document OWNS the root (RootWrapper). The NON-OWNING NodeIndex lets node finalizers de-register through
// a back-ref. Two paths, neither with an ordering requirement:
//
//   Dispose() — the RUNTIME RELEASE (runtime alive): drop the root ref -> the root's refcount falls to zero ->
//   its finalizer fires synchronously -> ~FDomNode de-registers (document alive) + FreeRTs its children ->
//   the acyclic tree reclaims via the cascade; node<->listener CYCLES reclaim on the next JS_RunGC (S6). This
//   is "dispose the document disposes the tree."
//
//   ~FDomDocument — PASSIVE / CONTEXT-FREE (touches NO runtime/context; safe in ANY order):
//     (1) ABANDON the root ref without freeing (FJsValue::Release — no quickjs call). If Dispose() already
//         ran, RootWrapper is empty and this is a no-op; if not, the ref is left for JS_FreeRuntime to reap
//         (the FALLBACK) — by design, not a leak.
//     (2) ORPHAN any surviving indexed nodes (JS refs kept them alive past the document): null their back-ref
//         (pure C++ pointer-null, no quickjs API). Their later finalizer (at JS_FreeRuntime) sees a null
//         back-ref and SKIPS the gone index.
//   Neither step touches the runtime/context, so the destructor cannot UAF whether disposed or not, runtime
//   alive or dead — there is NO ordering requirement.
FDomDocument::~FDomDocument()
{
	RootWrapper.Release(); // (1) context-free abandon — reaped by JS_FreeRuntime if undisposed (the fallback)

	for (TPair<FDomNodeId, FDomNode*>& Entry : NodeIndex) // (2) orphan survivors (pure C++; no quickjs)
	{
		if (Entry.Value != nullptr)
		{
			Entry.Value->Document = nullptr; // its later finalizer skips the dying index
		}
	}
}

void FDomDocument::Dispose()
{
	// The runtime release: drop the document's owned root ref WHILE THE RUNTIME IS ALIVE. Move-assigning an
	// empty holder frees the old root ref (ctx-based; runtime alive) -> refcount cascade reclaims the acyclic
	// tree; cycles reclaim on the next JS_RunGC. Idempotent (a second Dispose is a no-op on an empty holder).
	RootWrapper = FJsValue();
	RootNodeId = INDEX_NONE;
}

void FDomDocument::UnregisterNode(FDomNodeId InNodeId)
{
	NodeIndex.Remove(InNodeId); // non-owning de-register (called from ~FDomNode)
}

void FDomDocument::MarkDirty(FDomNodeId InNodeId, EDirtyKind Kind)
{
	// Dirty-mark on mutation (S3): tell 005 to relayout only the affected region. The DOM serializes + drives;
	// it does not lay out.
	Layout.MarkNodeDirty(InNodeId, Kind);
}

void FDomDocument::Relayout(int32 AvailableWidth)
{
	// Drive 005 + cache the result; the observation reads compose its geometry/style. Real layout output is
	// 005's (validation-pass) — the DOM only drives + reads.
	LastLayout = Layout.Relayout(AvailableWidth);
}

// === 005 ILayoutDocumentSource — structure reads (wrapper-governed index) + markup serialization (S5) ===

int32 FDomDocument::GetRootNode() const
{
	return RootNodeId;
}

int32 FDomDocument::GetParentNode(int32 NodeId) const
{
	const FDomNode* Node = FindNode(NodeId);
	return Node ? Node->GetParentNode() : INDEX_NONE;
}

void FDomDocument::GetChildNodes(int32 NodeId, TArray<int32>& OutChildren) const
{
	OutChildren.Reset();
	if (const FDomNode* Node = FindNode(NodeId))
	{
		Node->GetChildNodes(OutChildren);
	}
}

bool FDomDocument::IsLayoutBoundary(int32 NodeId) const
{
	// Conservative + safe: only the root is a guaranteed boundary. Over-claiming a boundary would make 005
	// relayout too SMALL a subtree (wrong); under-claiming only relayouts larger (safe). Finer boundary
	// detection (explicit dims / replaced elements) needs care + is a future refinement — root-only keeps
	// 005 correct (every dirty walks to the root => a full relayout, the safe baseline).
	return NodeId == RootNodeId;
}

namespace
{
// HTML escaping bounded by litehtml's createFromString expectations. '&' MUST be replaced first.
FString EscapeHtmlText(const FString& In)
{
	FString Out = In;
	Out.ReplaceInline(TEXT("&"), TEXT("&amp;"));
	Out.ReplaceInline(TEXT("<"), TEXT("&lt;"));
	Out.ReplaceInline(TEXT(">"), TEXT("&gt;"));
	return Out;
}

FString EscapeHtmlAttr(const FString& In)
{
	FString Out = In;
	Out.ReplaceInline(TEXT("&"), TEXT("&amp;"));
	Out.ReplaceInline(TEXT("\""), TEXT("&quot;"));
	Out.ReplaceInline(TEXT("<"), TEXT("&lt;"));
	Out.ReplaceInline(TEXT(">"), TEXT("&gt;"));
	return Out;
}
} // namespace

void FDomDocument::SerializeNode(const FDomNode* Node, FString& Out) const
{
	if (Node == nullptr)
	{
		return;
	}
	if (Node->Type == EDomNodeType::Text)
	{
		Out += EscapeHtmlText(Node->TextData);
		return;
	}

	Out += TEXT("<");
	Out += Node->TagName;
	// data-node-id (sprint 011 [c] T1): the correlation handle the layout engine reads back via get_attr to map
	// litehtml's placed element to our NodeId for the post-render geometry read. Emitted first, deterministic.
	Out += FString::Printf(TEXT(" data-node-id=\"%d\""), Node->GetNodeId());
	// Attributes in sorted-key order for DETERMINISTIC output (HTML attribute order is not semantic).
	TArray<FString> Keys;
	Node->Attributes.GetKeys(Keys);
	Keys.Sort();
	for (const FString& Key : Keys)
	{
		Out += TEXT(" ");
		Out += Key;
		Out += TEXT("=\"");
		Out += EscapeHtmlAttr(Node->Attributes[Key]);
		Out += TEXT("\"");
	}
	Out += TEXT(">");

	for (const FJsValue& ChildWrapper : Node->Children)
	{
		SerializeNode(NodeFromWrapper(ChildWrapper), Out);
	}

	Out += TEXT("</");
	Out += Node->TagName;
	Out += TEXT(">");
}

FString FDomDocument::GetDocumentHtml() const
{
	FString Html;
	SerializeNode(FindNode(RootNodeId), Html);
	return Html;
}

FString FDomDocument::GetSubtreeHtml(int32 NodeId) const
{
	FString Html;
	SerializeNode(FindNode(NodeId), Html);
	return Html;
}

FString FDomDocument::GetDocumentCss() const
{
	// CSS sourcing (style elements / a document stylesheet) is a future concern; the seam exists. Empty for now.
	return FString();
}

FDomNode* FDomDocument::FindNode(FDomNodeId InNodeId) const
{
	FDomNode* const* Found = NodeIndex.Find(InNodeId);
	return Found ? *Found : nullptr;
}

FDomNode* FDomDocument::NodeFromWrapper(const FJsValue& Wrapper) const
{
	if (!Wrapper.IsValid())
	{
		return nullptr;
	}
	// TODO-PREBUILD(signature): JS_GetOpaque(obj, class_id) -> void* (quickjs.h:1040) — see 007.
	return static_cast<FDomNode*>(JS_GetOpaque(Wrapper.GetValue(), NodeClassId));
}

FDomNodeId FDomDocument::GetRootNodeId() const
{
	return RootNodeId;
}

FJsValue FDomDocument::GetWrapperForNode(FDomNodeId InNodeId) const
{
	// The node marshaler (S2): resolve a node's EXISTING wrapper via its NON-OWNING borrow (set once at creation) —
	// O(1) and complete for root / attached / DETACHED. JS_DupValue gives the caller a NEW OWNING ref to the SAME
	// JS object (JS identity === preserved) WITHOUT minting a second wrapper; the stored borrow stays non-owning.
	// SOUND (no read-after-finalize): a node is in NodeIndex only while its wrapper is alive — the finalizer
	// (FinalizeNativeObject<FDomNode>) destroys the node, whose ~FDomNode de-registers it from NodeIndex — so a
	// FindNode hit is always a LIVE node with a LIVE wrapper. Empty (-> JS null) for INDEX_NONE / unknown / unset.
	if (InNodeId == INDEX_NONE)
	{
		return FJsValue();
	}
	const FDomNode* Node = FindNode(InNodeId); // a hit => the node (and thus its wrapper) is alive
	if (Node == nullptr || JS_IsUndefined(Node->WrapperBorrow))
	{
		return FJsValue();
	}
	return FJsValue(Context, JS_DupValue(Context, Node->WrapperBorrow));
}

FJsValue FDomDocument::CreateElement(const FString& TagName)
{
	const FDomNodeId Id = NextNodeId++;
	FDomNode* Node = new FDomNode(*this, Id, EDomNodeType::Element);
	Node->TagName = TagName;
	NodeIndex.Add(Id, Node);                                       // NON-OWNING index entry
	FJsValue Wrapper = Registry.CreateWrappedObject(NodeClassId, Node); // OWNING wrapper -> the caller
	Node->SetWrapperBorrow(Wrapper.GetValue());                   // S2: NON-OWNING reverse-lookup borrow (set once)
	return Wrapper;
}

FJsValue FDomDocument::CreateTextNode(const FString& Data)
{
	const FDomNodeId Id = NextNodeId++;
	FDomNode* Node = new FDomNode(*this, Id, EDomNodeType::Text);
	Node->TextData = Data;
	NodeIndex.Add(Id, Node);
	FJsValue Wrapper = Registry.CreateWrappedObject(NodeClassId, Node);
	Node->SetWrapperBorrow(Wrapper.GetValue());                   // S2: NON-OWNING reverse-lookup borrow (set once)
	return Wrapper;
}

TUniquePtr<FDomDocument> CreateDomDocument(
	ICathodeJsRuntime& Runtime, INativeClassRegistry& Registry, ILayoutEngine& Layout)
{
	// sprint 012 slice 1: RegisterDomNodeClass now also installs the bound members onto the class proto, so it
	// needs the live JSContext (002's confined context).
	JSContext* const Context = static_cast<JSContext*>(Runtime.GetNativeContextHandle());
	const JSClassID NodeClassId = RegisterDomNodeClass(Registry, Context);
	// sprint 012 slice 5: register the FDomDocument JS class (the 2nd class via the one mechanism) + install its
	// members, so the document can be wrapped + exposed as the `document` global (InstallAsGlobal).
	RegisterDomDocumentClass(Registry, Context);
	return MakeUnique<FDomDocument>(Runtime, Registry, Layout, NodeClassId);
}

// === Sprint 012 slice 5: the FDomDocument JS class (the 2nd class via the one binding-definition mechanism) ===

namespace
{
// The document class id, set at registration; the document member thunks recover the native via it.
JSClassID GDomDocumentClassId = 0;

// getElementById(id): find the element whose id ATTRIBUTE == id (the slice-3 reflected id) -> its EXISTING
// wrapper (S2 identity) or null. Find-by-attribute, NOT a node-id/shadow.
JSValue DocGetElementByIdThunk(JSContext* Ctx, JSValueConst ThisVal, int Argc, JSValueConst* Argv)
{
	FDomDocument* Self = static_cast<FDomDocument*>(JS_GetOpaque2(Ctx, ThisVal, GDomDocumentClassId));
	if (Self == nullptr) { return JS_EXCEPTION; }
	FString ElementId;
	if (Argc < 1 || !UnmarshalString(Ctx, Argv[0], ElementId)) { return JS_ThrowTypeError(Ctx, "expected a string id"); }
	const FDomNodeId Found = Self->FindElementById(ElementId);
	if (Found == INDEX_NONE) { return JS_NULL; } // non-existent id -> null
	FJsValue Wrapper = Self->GetWrapperForNode(Found); // the SAME wrapper (identity ===)
	return Wrapper.IsValid() ? Wrapper.Release() : JS_NULL;
}

// createElement(localName) -> a real element wrapper (the existing C++ factory).
JSValue DocCreateElementThunk(JSContext* Ctx, JSValueConst ThisVal, int Argc, JSValueConst* Argv)
{
	FDomDocument* Self = static_cast<FDomDocument*>(JS_GetOpaque2(Ctx, ThisVal, GDomDocumentClassId));
	if (Self == nullptr) { return JS_EXCEPTION; }
	FString Tag;
	if (Argc < 1 || !UnmarshalString(Ctx, Argv[0], Tag)) { return JS_ThrowTypeError(Ctx, "expected a tag name"); }
	return Self->CreateElement(Tag).Release();
}

// createTextNode(data) -> a real text node wrapper.
JSValue DocCreateTextNodeThunk(JSContext* Ctx, JSValueConst ThisVal, int Argc, JSValueConst* Argv)
{
	FDomDocument* Self = static_cast<FDomDocument*>(JS_GetOpaque2(Ctx, ThisVal, GDomDocumentClassId));
	if (Self == nullptr) { return JS_EXCEPTION; }
	FString Data;
	if (Argc < 1 || !UnmarshalString(Ctx, Argv[0], Data)) { return JS_ThrowTypeError(Ctx, "expected text data"); }
	return Self->CreateTextNode(Data).Release();
}

// createElementNS(namespace, qualifiedName) -> a real element. Preact calls this UNIVERSALLY with the HTML ns;
// the ns is ACCEPTED but NOT stored this slice (no namespaceURI consumer; SVG ns deferred) — create by the
// qualifiedName.
JSValue DocCreateElementNSThunk(JSContext* Ctx, JSValueConst ThisVal, int Argc, JSValueConst* Argv)
{
	FDomDocument* Self = static_cast<FDomDocument*>(JS_GetOpaque2(Ctx, ThisVal, GDomDocumentClassId));
	if (Self == nullptr) { return JS_EXCEPTION; }
	FString QualifiedName;
	if (Argc < 2 || !UnmarshalString(Ctx, Argv[1], QualifiedName)) { return JS_ThrowTypeError(Ctx, "expected (namespace, qualifiedName)"); }
	return Self->CreateElement(QualifiedName).Release();
}

// THE 2nd binding-definition table — install (InstallBoundMembers) + enumerate (EnumerateBoundMembersJson) BOTH
// consume it (crit-1, now PER CLASS). Document-iface members; names distinct from the node set (no conflation).
#define CATHODE_BIND_DOC_METHOD(JsName, Iface, Tag, Sig, ThunkFn) \
	{ JsName, Iface, EBoundMemberKind::Method, Tag, Sig, &ThunkFn, nullptr, nullptr }
static const FBoundMember GDomDocumentMembers[] =
{
	CATHODE_BIND_DOC_METHOD("getElementById",  "Document", "required", "(elementId: string) -> Element | null", DocGetElementByIdThunk),
	CATHODE_BIND_DOC_METHOD("createElement",   "Document", "margin",   "(localName: string) -> Element", DocCreateElementThunk),
	CATHODE_BIND_DOC_METHOD("createTextNode",  "Document", "required", "(data: string) -> Text", DocCreateTextNodeThunk),
	CATHODE_BIND_DOC_METHOD("createElementNS", "Document", "required", "(namespace: string | null, qualifiedName: string) -> Element", DocCreateElementNSThunk),
};
#undef CATHODE_BIND_DOC_METHOD
} // namespace

// --- FDomDocument slice-5 backings ---

FDomNodeId FDomDocument::FindElementById(const FString& ElementId) const
{
	// Tree walk (pre-order) from the root, matching the id ATTRIBUTE. The mount looks up rarely, so no by-id
	// index (no maintenance/invalidation burden). Children pushed reversed so the stack pops document-order.
	if (ElementId.IsEmpty())
	{
		return INDEX_NONE;
	}
	TArray<FDomNodeId> Stack;
	if (RootNodeId != INDEX_NONE)
	{
		Stack.Add(RootNodeId);
	}
	while (Stack.Num() > 0)
	{
		const FDomNodeId Current = Stack.Pop();
		const FDomNode* Node = FindNode(Current);
		if (Node == nullptr)
		{
			continue;
		}
		FString Value;
		if (Node->GetAttribute(TEXT("id"), Value) && Value == ElementId)
		{
			return Current; // first in document order
		}
		TArray<FDomNodeId> Children;
		Node->GetChildNodes(Children);
		for (int32 Index = Children.Num() - 1; Index >= 0; --Index)
		{
			Stack.Add(Children[Index]);
		}
	}
	return INDEX_NONE;
}

void FDomDocument::InstallAsGlobal()
{
	// Wrap THIS document via the document JS class (NON-OWNING: the wrapper's opaque is this C++/TUniquePtr-owned
	// document; the class finalizer is null so GC never deletes it) and set globalThis.document. The production
	// install method; the per-surface OnInstallDocument seam fires it at surface-load (gap-doc'd follow-on).
	if (Context == nullptr)
	{
		return;
	}
	FJsValue DocumentWrapper = Registry.CreateWrappedObject(GDomDocumentClassId, this);
	JSValue Global = JS_GetGlobalObject(Context);
	JS_SetPropertyStr(Context, Global, "document", DocumentWrapper.Release());
	JS_FreeValue(Context, Global);
}

JSClassID RegisterDomDocumentClass(INativeClassRegistry& Registry, JSContext* Context)
{
	// The 2nd class via the SAME mechanism. Finalizer is NON-OWNING (nullptr): the FDomDocument is
	// C++/TUniquePtr-owned; deleting it on GC would double-free. ~FDomDocument (passive, order-safe) handles the
	// C++ teardown. NO gc_mark (nullptr) — intentionally absent: no JS-ref cycle runs through the document wrapper
	// (node->document non-owning + no event API bound to JS), so there is nothing for a document gc_mark to
	// collect; reclaim is Dispose() + the node gc_mark/T4, and RootWrapper's C++ refcount governs survival. See
	// the lifetime note in CathodeDom/COVERAGE.md (carry-not-rediscover: gc_mark returns when the event API binds).
	FNativeClassDef Def;
	Def.Name = "CathodeDomDocument";
	Def.Finalizer = nullptr; // non-owning wrapper — do NOT delete the C++-owned document on GC
	Def.GcMark = nullptr;    // intentionally absent (no wrapper-cycle to collect) — see COVERAGE.md lifetime note
	const JSClassID ClassId = Registry.RegisterClass(Def);
	GDomDocumentClassId = ClassId; // the member thunks recover the native via this id

	// INSTALL side: build the proto + install every document member from the one table. CreateWrappedObject's
	// JS_GetClassProto path then gives the document wrapper the members.
	InstallBoundMembers(Context, ClassId, GDomDocumentMembers, UE_ARRAY_COUNT(GDomDocumentMembers));
	return ClassId;
}

FString GetDomDocumentDescriptorJson()
{
	// ENUMERATE side: the document descriptor from the SAME table the members install from (the per-class
	// lockstep source — the faculty reconciles it vs the live document proto).
	return EnumerateBoundMembersJson(GDomDocumentMembers, UE_ARRAY_COUNT(GDomDocumentMembers));
}

} // namespace Cathode
