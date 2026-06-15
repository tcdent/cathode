#include "CathodeDom.h"

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
	const JSClassID NodeClassId =
		RegisterDomNodeClass(Registry, static_cast<JSContext*>(Runtime.GetNativeContextHandle()));
	return MakeUnique<FDomDocument>(Runtime, Registry, Layout, NodeClassId);
}

} // namespace Cathode
