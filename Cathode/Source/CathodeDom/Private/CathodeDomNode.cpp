#include "CathodeDom.h"

#include "CathodeBoundMember.h" // sprint 012 slice 1: the single binding-definition mechanism (install + enumerate)

// The DOM is quickjs-type-coupled by design (via 007). quickjs.h reaches here through CathodeBinding.h; the
// DOM calls a few quickjs primitives directly to map wrappers<->opaques. Include path wired at execution.
THIRD_PARTY_INCLUDES_START
#include "quickjs.h"
THIRD_PARTY_INCLUDES_END

namespace Cathode
{

namespace
{
// JSValue identity for objects (incl. functions) = the same boxed pointer + tag. Used wherever we match a
// held wrapper against another JS value by identity (reparent reference, child removal, listener removal).
// JS_VALUE_GET_PTR/TAG verified against the real quickjs.h at first compile (was TODO-PREBUILD(signature)).
bool SameJsObject(const FJsValue& Held, JSValueConst Other)
{
	return JS_VALUE_GET_PTR(Held.GetValue()) == JS_VALUE_GET_PTR(Other)
		&& JS_VALUE_GET_TAG(Held.GetValue()) == JS_VALUE_GET_TAG(Other);
}
} // namespace

FDomNode::FDomNode(FDomDocument& InDocument, FDomNodeId InNodeId, EDomNodeType InType)
	: NodeId(InNodeId)
	, Type(InType)
	, Document(&InDocument)
	, Runtime(JS_GetRuntime(static_cast<JSContext*>(InDocument.Runtime.GetNativeContextHandle())))
{
}

namespace
{
// Sprint 012 events: the Event / KeyboardEvent JS class ids, set at RegisterDomEventClasses (below). The dispatch
// wraps the live FDomEvent in an event object via these; the event member thunks recover the native via them.
JSClassID GEventClassId = 0;
JSClassID GKeyboardEventClassId = 0;
} // namespace

FDomNode::~FDomNode()
{
	// Finalizer-driven (FinalizeNativeObject<FDomNode> -> delete), or the document-dtor cascade. De-register
	// from the document's NON-OWNING index via the back-ref IF it is still valid; an ORPHANED node (back-ref
	// nulled by ~FDomDocument because it outlived the document) skips the index entirely — no dangling deref.
	if (Document != nullptr)
	{
		Document->UnregisterNode(NodeId);
	}
	// Release held refs via the RUNTIME (finalizer-safe — no ctx in a finalizer); empties each holder so its
	// own destructor is a no-op. Child wrappers AND listener functions (both are held JS refs; attributes are
	// plain string data, not refs).
	for (FJsValue& Child : Children)
	{
		Child.FreeRT(Runtime);
	}
	for (FDomEventListener& Listener : Listeners)
	{
		Listener.Listener.FreeRT(Runtime);
	}
}

void FDomNode::MarkHeldRefs(JSRuntime* InRuntime, JS_MarkFunc* MarkFunc) const
{
	// gc_mark visibility: mark every held JS ref — child wrappers AND listener functions — so -ng's collector
	// traverses the tree + reclaims cross-heap cycles. Marking the LISTENERS is THE Model-A hazard fix: the
	// node<->listener cycle becomes GC-visible (reclaimable). Attributes are plain string data, not marked.
	for (const FJsValue& Child : Children)
	{
		Child.Mark(InRuntime, MarkFunc);
	}
	for (const FDomEventListener& Listener : Listeners)
	{
		Listener.Listener.Mark(InRuntime, MarkFunc);
	}
}

EDomNodeType FDomNode::GetNodeType() const
{
	return Type;
}

FDomNodeId FDomNode::GetNodeId() const
{
	return NodeId;
}

const FString& FDomNode::GetTagName() const
{
	return TagName;
}

void FDomNode::AppendChild(const FJsValue& ChildWrapper)
{
	// The JS-ref LINK — the parent takes an OWNING ref to the child's wrapper (Model A). Reparent first: if
	// the child has a parent, that parent drops its held ref (a reference-drop). Dirty-mark the parent (the
	// changed subtree) so 005 relayouts only it (S3).
	if (Document == nullptr)
	{
		return;
	}
	FDomNode* ChildNode = Document->NodeFromWrapper(ChildWrapper);
	if (ChildNode == nullptr)
	{
		return;
	}
	if (ChildNode->ParentId != INDEX_NONE)
	{
		if (FDomNode* OldParent = Document->FindNode(ChildNode->ParentId))
		{
			OldParent->RemoveChild(ChildWrapper); // old parent's subtree also dirty-marks (its own RemoveChild)
		}
	}
	Children.Add(ChildWrapper); // FJsValue copy => JS_DupValue: the parent's own owning ref to the child wrapper
	ChildNode->ParentId = NodeId;
	Document->MarkDirty(NodeId, EDirtyKind::Structure);
}

void FDomNode::InsertBefore(const FJsValue& ChildWrapper, const FJsValue& ReferenceWrapper)
{
	// Like AppendChild but positions the child BEFORE the reference among the parent's children. An empty (or
	// unknown) reference appends (lenient). Reference-drop reparent + dirty-mark the parent (S3).
	if (Document == nullptr)
	{
		return;
	}
	FDomNode* ChildNode = Document->NodeFromWrapper(ChildWrapper);
	if (ChildNode == nullptr)
	{
		return;
	}
	if (ChildNode->ParentId != INDEX_NONE)
	{
		if (FDomNode* OldParent = Document->FindNode(ChildNode->ParentId))
		{
			OldParent->RemoveChild(ChildWrapper);
		}
	}
	const int32 InsertAt = ReferenceWrapper.IsValid()
		? Children.IndexOfByPredicate([&ReferenceWrapper](const FJsValue& Held) { return SameJsObject(Held, ReferenceWrapper.GetValue()); })
		: INDEX_NONE;
	if (InsertAt == INDEX_NONE)
	{
		Children.Add(ChildWrapper); // empty/unknown reference => append
	}
	else
	{
		Children.Insert(ChildWrapper, InsertAt);
	}
	ChildNode->ParentId = NodeId;
	Document->MarkDirty(NodeId, EDirtyKind::Structure);
}

void FDomNode::RemoveChild(const FJsValue& ChildWrapper)
{
	// The reference-DROP — drop the parent's held ref to the matched child wrapper (by JS identity). If
	// nothing else references the child, 007's gc_mark/finalizer reclaim it (no explicit free). Dirty-mark
	// the parent on a real removal (S3).
	FDomNode* ChildNode = Document ? Document->NodeFromWrapper(ChildWrapper) : nullptr;
	const int32 Removed = Children.RemoveAll(
		[&ChildWrapper](const FJsValue& Held) { return SameJsObject(Held, ChildWrapper.GetValue()); });
	if (Removed > 0)
	{
		if (ChildNode != nullptr)
		{
			ChildNode->ParentId = INDEX_NONE;
		}
		if (Document != nullptr)
		{
			Document->MarkDirty(NodeId, EDirtyKind::Structure);
		}
	}
}

// --- Tree reads (S3) — return NodeIds (INDEX_NONE when absent); resolve child wrappers via the index ---

FDomNodeId FDomNode::GetParentNode() const
{
	return ParentId;
}

void FDomNode::GetChildNodes(TArray<FDomNodeId>& OutChildren) const
{
	OutChildren.Reset();
	if (Document == nullptr)
	{
		return;
	}
	for (const FJsValue& ChildWrapper : Children)
	{
		if (const FDomNode* Child = Document->NodeFromWrapper(ChildWrapper))
		{
			OutChildren.Add(Child->NodeId);
		}
	}
}

FDomNodeId FDomNode::GetFirstChild() const
{
	if (Document == nullptr || Children.Num() == 0)
	{
		return INDEX_NONE;
	}
	const FDomNode* First = Document->NodeFromWrapper(Children[0]);
	return First ? First->NodeId : INDEX_NONE;
}

FDomNodeId FDomNode::GetNextSibling() const
{
	if (Document == nullptr || ParentId == INDEX_NONE)
	{
		return INDEX_NONE;
	}
	const FDomNode* Parent = Document->FindNode(ParentId);
	if (Parent == nullptr)
	{
		return INDEX_NONE;
	}
	for (int32 Index = 0; Index < Parent->Children.Num(); ++Index)
	{
		if (Document->NodeFromWrapper(Parent->Children[Index]) == this)
		{
			if (Index + 1 < Parent->Children.Num())
			{
				const FDomNode* Next = Document->NodeFromWrapper(Parent->Children[Index + 1]);
				return Next ? Next->NodeId : INDEX_NONE;
			}
			return INDEX_NONE;
		}
	}
	return INDEX_NONE;
}

// --- Attribute / property access (S4; the bounded string-valued surface) ---

bool FDomNode::GetAttribute(const FString& Name, FString& OutValue) const
{
	if (const FString* Found = Attributes.Find(Name))
	{
		OutValue = *Found;
		return true;
	}
	return false;
}

void FDomNode::SetAttribute(const FString& Name, const FString& Value)
{
	Attributes.Add(Name, Value);
	// Conservative: an attribute change can alter geometry (CSS matching), so mark Layout. (A finer
	// attr->kind classification — PaintOnly for purely visual attrs — is a future refinement.)
	if (Document != nullptr)
	{
		Document->MarkDirty(NodeId, EDirtyKind::Layout);
	}
}

void FDomNode::RemoveAttribute(const FString& Name)
{
	if (Attributes.Remove(Name) > 0 && Document != nullptr)
	{
		Document->MarkDirty(NodeId, EDirtyKind::Layout);
	}
}

bool FDomNode::GetValue(FString& OutValue) const
{
	// 'value' is backed by the value attribute in the bounded scaffold (get-only per the freeze).
	return GetAttribute(TEXT("value"), OutValue);
}

FString FDomNode::GetTextContent() const
{
	if (Type == EDomNodeType::Text)
	{
		return TextData;
	}
	FString Result;
	if (Document != nullptr)
	{
		for (const FJsValue& ChildWrapper : Children)
		{
			if (const FDomNode* Child = Document->NodeFromWrapper(ChildWrapper))
			{
				Result += Child->GetTextContent();
			}
		}
	}
	return Result;
}

void FDomNode::SetTextContent(const FString& Text)
{
	// Bulk set: replace ALL children (reference-drops) with a single text node. Children replaced -> Structure.
	if (Document == nullptr)
	{
		return;
	}
	for (const FJsValue& ChildWrapper : Children)
	{
		if (FDomNode* Child = Document->NodeFromWrapper(ChildWrapper))
		{
			Child->ParentId = INDEX_NONE; // detach removed children (reference-drop on Reset below)
		}
	}
	Children.Reset(); // drop the child wrapper refs (reference-drops; 007 reclaims the unreferenced subtree)
	FJsValue TextWrapper = Document->CreateTextNode(Text);
	if (FDomNode* TextNode = Document->NodeFromWrapper(TextWrapper))
	{
		TextNode->ParentId = NodeId;
	}
	Children.Add(TextWrapper);
	Document->MarkDirty(NodeId, EDirtyKind::Structure);
}

// --- Sprint 012 slice 2: thin backing for the node-core member bindings (spec-anchored, additive) ---

FString FDomNode::GetNodeName() const
{
	// Node.nodeName: the element's tag name; "#text" for a text node (the spec node-name table).
	return Type == EDomNodeType::Element ? TagName : FString(TEXT("#text"));
}

bool FDomNode::GetNodeValue(FString& OutValue) const
{
	// Node.nodeValue: a text node's character data; null (false) for an element.
	if (Type == EDomNodeType::Text)
	{
		OutValue = TextData;
		return true;
	}
	return false;
}

void FDomNode::SetNodeValue(const FString& Value)
{
	// Element nodeValue set is a no-op (spec); a text node's set mutates its data (delegates to SetData).
	if (Type == EDomNodeType::Text)
	{
		SetData(Value);
	}
}

FString FDomNode::GetData() const
{
	// CharacterData.data — the text node's character data (empty off a non-text node in the collapsed class).
	return TextData;
}

void FDomNode::SetData(const FString& Value)
{
	// Text mutation: replace the character data + dirty-mark (a text change reflows -> Layout). No-op off a text
	// node (data is a CharacterData member).
	if (Type != EDomNodeType::Text)
	{
		return;
	}
	TextData = Value;
	if (Document != nullptr)
	{
		Document->MarkDirty(NodeId, EDirtyKind::Layout);
	}
}

void FDomNode::Remove()
{
	// ChildNode.remove(): the parent drops its OWNING ref to THIS node's wrapper (a reference-drop; 007 reclaims
	// if nothing else holds it). Self does not hold its own wrapper (Model A), so find it in the parent's Children
	// by native identity. Dirty-mark the parent's subtree on a real removal.
	if (Document == nullptr || ParentId == INDEX_NONE)
	{
		return;
	}
	FDomNode* Parent = Document->FindNode(ParentId);
	if (Parent == nullptr)
	{
		return;
	}
	FDomDocument* const Doc = Document;
	const int32 Removed = Parent->Children.RemoveAll(
		[this, Doc](const FJsValue& Held) { return Doc->NodeFromWrapper(Held) == this; });
	if (Removed > 0)
	{
		ParentId = INDEX_NONE;
		Document->MarkDirty(Parent->NodeId, EDirtyKind::Structure);
	}
}

void FDomNode::GetChildWrappers(TArray<FJsValue>& OutWrappers) const
{
	// childNodes snapshot source: the parent's OWNING child wrappers, read DIRECTLY (O(n)) — assigning the array
	// copies each FJsValue (=> JS_DupValue), so the node marshaler gets its own snapshot refs.
	OutWrappers = Children;
}

FDomDocument* FDomNode::GetOwningDocument() const
{
	return Document;
}

// --- IMatchableNode (012 selectors): the structural surface the CathodeLayout matcher walks over the LIVE tree.
//     Reads this node's current state directly (no serialization) so a freshly-mutated node matches without relayout. ---

FString FDomNode::GetMatchTagName() const
{
	return TagName; // "" for text nodes — they never match a type selector or condition
}

bool FDomNode::GetMatchAttribute(const FString& Name, FString& OutValue) const
{
	// HTML attribute names are case-insensitive: exact key first (the common path), else a case-insensitive scan.
	if (const FString* Exact = Attributes.Find(Name))
	{
		OutValue = *Exact;
		return true;
	}
	for (const TPair<FString, FString>& Pair : Attributes)
	{
		if (Pair.Key.Equals(Name, ESearchCase::IgnoreCase))
		{
			OutValue = Pair.Value;
			return true;
		}
	}
	return false;
}

const IMatchableNode* FDomNode::GetMatchParent() const
{
	return (Document != nullptr) ? Document->FindNode(ParentId) : nullptr;
}

void FDomNode::GetMatchChildren(TArray<const IMatchableNode*>& OutChildren) const
{
	// Child ELEMENTS in document order. Resolve each owning child wrapper to its node; skip text nodes (they never
	// match a selector) so the matcher's pre-order walk only visits matchable elements.
	OutChildren.Reset();
	if (Document == nullptr)
	{
		return;
	}
	for (const FJsValue& ChildWrapper : Children)
	{
		const FDomNode* Child = Document->NodeFromWrapper(ChildWrapper);
		if (Child != nullptr && Child->Type == EDomNodeType::Element)
		{
			OutChildren.Add(Child);
		}
	}
}

// --- Event system (S6) — listeners are held JS refs; dispatch bubbles; removal is a reference-drop ---

void FDomNode::AddEventListener(const FString& EventType, FJsValue Listener, bool bCapture)
{
	// DOM DEDUP: the same (type, fn, capture) triple registers ONCE. Hold the listener function (an OWNING JS ref)
	// keyed by (type, capture); gc_mark marks it (the node<->listener cycle visibility); the finalizer FreeRTs it;
	// removeEventListener drops the matching triple.
	for (const FDomEventListener& Existing : Listeners)
	{
		if (Existing.Type == EventType && Existing.bCapture == bCapture
			&& SameJsObject(Existing.Listener, Listener.GetValue()))
		{
			return; // already registered -> no-op (the held Listener arg's destructor frees its ref)
		}
	}
	Listeners.Add(FDomEventListener{ EventType, MoveTemp(Listener), bCapture });
}

void FDomNode::RemoveEventListener(const FString& EventType, JSValueConst ListenerFn, bool bCapture)
{
	// Reference-DROP: remove the matching (type, fn-by-identity, capture) TRIPLE only — a mismatch on ANY of the
	// three (different fn, type, OR capture) is a no-op. The removed FJsValue's destructor frees the held ref.
	Listeners.RemoveAll([&EventType, ListenerFn, bCapture](const FDomEventListener& Listener)
	{
		return Listener.Type == EventType && Listener.bCapture == bCapture
			&& SameJsObject(Listener.Listener, ListenerFn);
	});
}

void FDomNode::DispatchEvent(FDomEvent& Event, const FDomListenerInvoker& Invoker)
{
	// The dispatch ALGORITHM (extends 008's AtTarget+bubble with the CAPTURE phase): CAPTURE (ancestors top-down,
	// capture listeners) -> AT-TARGET (the target, ALL its listeners in registration order) -> BUBBLE (ancestors
	// bottom-up, non-capture listeners, if bBubbles). ONE shared Event walks the tree (CurrentTarget/Phase
	// advance). The (type, capture) tuple selects per phase. stopPropagation halts the REST of the walk; the SAME
	// node's remaining listeners still run (DOM, not stopImmediate). preventDefault is a flag read afterwards.
	if (Document == nullptr)
	{
		return;
	}
	Event.Target = NodeId;
	Event.Document = Document; // so the JS event object's target/currentTarget resolve node wrappers

	// The ancestor path target -> root (Path[0] = target, Path[Last] = root).
	TArray<FDomNode*> Path;
	for (FDomNode* N = this; N != nullptr; N = (N->ParentId != INDEX_NONE) ? Document->FindNode(N->ParentId) : nullptr)
	{
		Path.Add(N);
	}

	// Fire a node's matching listeners for a phase; returns false to HALT the walk (stopPropagation seen).
	auto FireAt = [&Event, &Invoker](FDomNode* Node, EDomEventPhase Ph, bool bCapturePhase) -> bool
	{
		Event.CurrentTarget = Node->NodeId;
		Event.Phase = Ph;
		for (const FDomEventListener& Listener : Node->Listeners)
		{
			if (Listener.Type != Event.Type)
			{
				continue;
			}
			// AtTarget fires BOTH capture + bubble listeners (reg order); capture phase fires capture-only; bubble
			// phase fires non-capture-only.
			const bool bSelected = (Ph == EDomEventPhase::AtTarget)
				|| (bCapturePhase ? Listener.bCapture : !Listener.bCapture);
			if (bSelected)
			{
				Invoker(Listener.Listener, Event); // a listener may set bPropagationStopped / bDefaultPrevented
			}
		}
		return !Event.bPropagationStopped; // same-node remaining already ran; halt the REST of the walk
	};

	// CAPTURE: root -> ... -> parent (top-down ancestors).
	for (int32 Index = Path.Num() - 1; Index >= 1; --Index)
	{
		if (!FireAt(Path[Index], EDomEventPhase::Capturing, /*bCapturePhase*/ true))
		{
			return;
		}
	}
	// AT-TARGET: the target.
	if (!FireAt(Path[0], EDomEventPhase::AtTarget, /*bCapturePhase*/ false))
	{
		return;
	}
	// BUBBLE: parent -> ... -> root (bottom-up ancestors), only if the event bubbles.
	if (!Event.bBubbles)
	{
		return;
	}
	for (int32 Index = 1; Index < Path.Num(); ++Index)
	{
		if (!FireAt(Path[Index], EDomEventPhase::Bubbling, /*bCapturePhase*/ false))
		{
			return;
		}
	}
}

void FDomNode::DispatchEvent(FDomEvent& Event)
{
	// Production: build the LIVE shared Event JS object (NON-owning — its opaque is the caller's FDomEvent; the
	// dispatch advances CurrentTarget/Phase + a listener's preventDefault/stopPropagation MUTATE it, all read by
	// the same object's getters) and invoke each selected listener via a transient 007 FCallableHandle with it.
	if (Document == nullptr)
	{
		return;
	}
	FJsValue EventObj = Document->Registry.CreateWrappedObject(GEventClassId, &Event);
	DispatchEvent(Event, [&EventObj](const FJsValue& ListenerFn, FDomEvent& /*Event*/)
	{
		FCallableHandle Handle(ListenerFn);
		JSValue Arg = EventObj.GetValue();
		Handle.Call(JS_UNDEFINED, MakeArrayView(&Arg, 1));
	});
	// The FDomEvent is the caller's stack — invalidate the wrapper so a listener that STORED it reads safely
	// (its getters return defaults) instead of dereferencing a dangling opaque after dispatch.
	if (EventObj.IsValid())
	{
		JS_SetOpaque(EventObj.GetValue(), nullptr);
	}
}

void FDomNode::DispatchKeyboardEvent(FDomKeyboardEvent& Keyboard)
{
	// As DispatchEvent, but the live object is a KeyboardEvent (key/code) over the FDomKeyboardEvent; the walk
	// runs on its base FDomEvent (Keyboard.Event).
	if (Document == nullptr)
	{
		return;
	}
	FJsValue EventObj = Document->Registry.CreateWrappedObject(GKeyboardEventClassId, &Keyboard);
	DispatchEvent(Keyboard.Event, [&EventObj](const FJsValue& ListenerFn, FDomEvent& /*Event*/)
	{
		FCallableHandle Handle(ListenerFn);
		JSValue Arg = EventObj.GetValue();
		Handle.Call(JS_UNDEFINED, MakeArrayView(&Arg, 1));
	});
	if (EventObj.IsValid())
	{
		JS_SetOpaque(EventObj.GetValue(), nullptr);
	}
}

// --- Observation reads (S5) — OFF the author surface; COMPOSE 005's geometry/style by NodeId ---

bool FDomNode::GetBoundingClientRect(FBox2f& OutBox) const
{
	if (Document == nullptr)
	{
		return false;
	}
	for (const FLayoutNodeGeometry& Geometry : Document->LastLayout.Geometry)
	{
		if (Geometry.NodeId == NodeId)
		{
			OutBox = Geometry.Box; // the box VALUE is 005's (real layout = validation-pass); the lookup is ours
			return true;
		}
	}
	return false; // no laid-out geometry for this node (not yet relaid out)
}

bool FDomNode::GetComputedStyle(FResolvedStyle& OutStyle) const
{
	if (Document == nullptr)
	{
		return false;
	}
	for (const FLayoutNodeGeometry& Geometry : Document->LastLayout.Geometry)
	{
		if (Geometry.NodeId == NodeId)
		{
			OutStyle = Geometry.Style; // 005's bounded resolved style; the lookup-by-NodeId is ours
			return true;
		}
	}
	return false;
}

// --- Sprint 012 slice 1: the DOM node member bindings (the single binding-definition table) ---

namespace
{
// The DOM node class id, set at registration; the per-member thunks recover the native instance via it.
JSClassID GDomNodeClassId = 0;

// Shape-template thunks — one C++ method-ptr instantiates one install thunk. The CATHODE_BIND_* macros emit
// BOTH the thunk (this instantiation) AND the FBoundMember entry from ONE declaration, so install + enumerate
// then both consume the single table.
template <const FString& (FDomNode::*Getter)() const>
JSValue StrGetterThunk(JSContext* Ctx, JSValueConst ThisVal)
{
	FDomNode* Self = static_cast<FDomNode*>(JS_GetOpaque2(Ctx, ThisVal, GDomNodeClassId));
	if (Self == nullptr) { return JS_EXCEPTION; }
	return MarshalString(Ctx, (Self->*Getter)()).Release();
}

template <bool (FDomNode::*Method)(const FString&, FString&) const>
JSValue StrOptStrMethodThunk(JSContext* Ctx, JSValueConst ThisVal, int Argc, JSValueConst* Argv)
{
	FDomNode* Self = static_cast<FDomNode*>(JS_GetOpaque2(Ctx, ThisVal, GDomNodeClassId));
	if (Self == nullptr) { return JS_EXCEPTION; }
	FString Name;
	if (Argc < 1 || !UnmarshalString(Ctx, Argv[0], Name)) { return JS_ThrowTypeError(Ctx, "expected a string argument"); }
	FString Out;
	if (!(Self->*Method)(Name, Out)) { return JS_NULL; } // absent attribute -> JS null
	return MarshalString(Ctx, Out).Release();
}

// === Sprint 012 slice 2: the full node-core surface — marshalers + thunks (every member via the ONE table) ===

// Node marshaler (the identity-preserving deep oracle): an FDomNodeId -> the node's EXISTING wrapper (JS ===),
// or JS null. Resolves via the owning wrapper graph (FDomDocument::GetWrapperForNode) — never mints a second.
JSValue MarshalNode(JSContext* Ctx, FDomDocument* Doc, FDomNodeId Id)
{
	if (Doc == nullptr || Id == INDEX_NONE)
	{
		return JS_NULL;
	}
	FJsValue Wrapper = Doc->GetWrapperForNode(Id); // a NEW owning ref to the SAME stored wrapper (or empty)
	return Wrapper.IsValid() ? Wrapper.Release() : JS_NULL;
}

// Node.nodeType spec constants (the collapsed class carries both kinds).
double NodeTypeToSpec(EDomNodeType NodeType)
{
	return NodeType == EDomNodeType::Text ? 3.0 /* TEXT_NODE */ : 1.0 /* ELEMENT_NODE */;
}

// The NodeList TYPE (behavioral-contract #8 reference instance): the EXACT spec method surface — item/forEach/
// keys/values/entries/[Symbol.iterator] reading off the instance's indexed props + length — authored in Cathode
// JS, with NO array superset (the silent map/filter divergence exact-spec-typing prevents). Iterators are plain
// closures (no generator dependency on the bare runtime). Installed once per context; idempotent.
void InstallNodeListType(JSContext* Ctx)
{
	static const char* const NodeListSource =
		"(function(){"
		"  if (globalThis.NodeList) { return; }"
		"  function NodeList(){ throw new TypeError('Illegal constructor'); }"
		"  NodeList.prototype.item = function(i){ i = i | 0; return (i >= 0 && i < this.length) ? this[i] : null; };"
		"  NodeList.prototype.forEach = function(cb, thisArg){ for (var i = 0; i < this.length; i++) { cb.call(thisArg, this[i], i, this); } };"
		"  function makeIter(get, host){ var i = 0; var it = { next: function(){ return i < host.length ? { value: get(i++), done: false } : { value: undefined, done: true }; } }; it[Symbol.iterator] = function(){ return this; }; return it; }"
		"  NodeList.prototype.values = function(){ var s = this; return makeIter(function(i){ return s[i]; }, s); };"
		"  NodeList.prototype.keys = function(){ var s = this; return makeIter(function(i){ return i; }, s); };"
		"  NodeList.prototype.entries = function(){ var s = this; return makeIter(function(i){ return [i, s[i]]; }, s); };"
		"  NodeList.prototype[Symbol.iterator] = NodeList.prototype.values;"
		"  globalThis.NodeList = NodeList;"
		"})();";
	JSValue Result = JS_Eval(Ctx, NodeListSource, FCStringAnsi::Strlen(NodeListSource), "cathode-nodelist.js", JS_EVAL_TYPE_GLOBAL);
	JS_FreeValue(Ctx, Result);
}

// childNodes -> a NodeList-TYPED SNAPSHOT: an object whose [[Prototype]] is the context's NodeList.prototype
// (instanceof + the spec surface), carrying the child wrappers as indexed own props + length. Snapshot depth.
FJsValue MarshalNodeList(JSContext* Ctx, const TArray<FJsValue>& Wrappers)
{
	JSValue Global = JS_GetGlobalObject(Ctx);
	JSValue Ctor = JS_GetPropertyStr(Ctx, Global, "NodeList");
	JSValue Proto = JS_GetPropertyStr(Ctx, Ctor, "prototype");
	JSValue List = JS_NewObjectProto(Ctx, Proto); // [[Prototype]] = NodeList.prototype
	JS_FreeValue(Ctx, Proto);
	JS_FreeValue(Ctx, Ctor);
	JS_FreeValue(Ctx, Global);
	for (int32 Index = 0; Index < Wrappers.Num(); ++Index)
	{
		JS_SetPropertyUint32(Ctx, List, static_cast<uint32>(Index), JS_DupValue(Ctx, Wrappers[Index].GetValue()));
	}
	JS_SetPropertyStr(Ctx, List, "length", JS_NewInt32(Ctx, Wrappers.Num()));
	return FJsValue(Ctx, List);
}

// --- Template thunks (one C++ member-ptr -> one install thunk), mirroring slice 1. ---

// By-value string getter (return FString) — the const-ref StrGetterThunk's sibling.
template <FString (FDomNode::*Getter)() const>
JSValue StrValGetterThunk(JSContext* Ctx, JSValueConst ThisVal)
{
	FDomNode* Self = static_cast<FDomNode*>(JS_GetOpaque2(Ctx, ThisVal, GDomNodeClassId));
	if (Self == nullptr) { return JS_EXCEPTION; }
	return MarshalString(Ctx, (Self->*Getter)()).Release();
}

// Optional-string property getter (bool(FString&) -> string | null) — the no-arg analog of getAttribute.
template <bool (FDomNode::*Getter)(FString&) const>
JSValue OptStrGetterThunk(JSContext* Ctx, JSValueConst ThisVal)
{
	FDomNode* Self = static_cast<FDomNode*>(JS_GetOpaque2(Ctx, ThisVal, GDomNodeClassId));
	if (Self == nullptr) { return JS_EXCEPTION; }
	FString Out;
	if (!(Self->*Getter)(Out)) { return JS_NULL; }
	return MarshalString(Ctx, Out).Release();
}

// nodeType: enum getter -> the spec number.
template <EDomNodeType (FDomNode::*Getter)() const>
JSValue NodeTypeGetterThunk(JSContext* Ctx, JSValueConst ThisVal)
{
	FDomNode* Self = static_cast<FDomNode*>(JS_GetOpaque2(Ctx, ThisVal, GDomNodeClassId));
	if (Self == nullptr) { return JS_EXCEPTION; }
	return MarshalNumber(Ctx, NodeTypeToSpec((Self->*Getter)())).Release();
}

// Node getter (FDomNodeId -> Node | null), identity-preserving via the node marshaler.
template <FDomNodeId (FDomNode::*Getter)() const>
JSValue NodeGetterThunk(JSContext* Ctx, JSValueConst ThisVal)
{
	FDomNode* Self = static_cast<FDomNode*>(JS_GetOpaque2(Ctx, ThisVal, GDomNodeClassId));
	if (Self == nullptr) { return JS_EXCEPTION; }
	return MarshalNode(Ctx, Self->GetOwningDocument(), (Self->*Getter)());
}

// childNodes: the parent's OWNING child wrappers (read directly) -> a NodeList-typed snapshot.
JSValue ChildNodesGetterThunk(JSContext* Ctx, JSValueConst ThisVal)
{
	FDomNode* Self = static_cast<FDomNode*>(JS_GetOpaque2(Ctx, ThisVal, GDomNodeClassId));
	if (Self == nullptr) { return JS_EXCEPTION; }
	TArray<FJsValue> Wrappers;
	Self->GetChildWrappers(Wrappers);
	return MarshalNodeList(Ctx, Wrappers).Release();
}

// String property setter (void(const FString&)).
template <void (FDomNode::*Setter)(const FString&)>
JSValue StrSetterThunk(JSContext* Ctx, JSValueConst ThisVal, JSValueConst Val)
{
	FDomNode* Self = static_cast<FDomNode*>(JS_GetOpaque2(Ctx, ThisVal, GDomNodeClassId));
	if (Self == nullptr) { return JS_EXCEPTION; }
	FString In;
	if (!UnmarshalString(Ctx, Val, In)) { return JS_ThrowTypeError(Ctx, "expected a string value"); }
	(Self->*Setter)(In);
	return JS_UNDEFINED;
}

// Two-string -> void method (setAttribute).
template <void (FDomNode::*Method)(const FString&, const FString&)>
JSValue StrStrVoidMethodThunk(JSContext* Ctx, JSValueConst ThisVal, int Argc, JSValueConst* Argv)
{
	FDomNode* Self = static_cast<FDomNode*>(JS_GetOpaque2(Ctx, ThisVal, GDomNodeClassId));
	if (Self == nullptr) { return JS_EXCEPTION; }
	FString A, B;
	if (Argc < 2 || !UnmarshalString(Ctx, Argv[0], A) || !UnmarshalString(Ctx, Argv[1], B))
	{
		return JS_ThrowTypeError(Ctx, "expected two string arguments");
	}
	(Self->*Method)(A, B);
	return JS_UNDEFINED;
}

// One-string -> void method (removeAttribute).
template <void (FDomNode::*Method)(const FString&)>
JSValue StrVoidMethodThunk(JSContext* Ctx, JSValueConst ThisVal, int Argc, JSValueConst* Argv)
{
	FDomNode* Self = static_cast<FDomNode*>(JS_GetOpaque2(Ctx, ThisVal, GDomNodeClassId));
	if (Self == nullptr) { return JS_EXCEPTION; }
	FString A;
	if (Argc < 1 || !UnmarshalString(Ctx, Argv[0], A)) { return JS_ThrowTypeError(Ctx, "expected a string argument"); }
	(Self->*Method)(A);
	return JS_UNDEFINED;
}

// No-arg -> void method (remove).
template <void (FDomNode::*Method)()>
JSValue VoidMethodThunk(JSContext* Ctx, JSValueConst ThisVal, int /*Argc*/, JSValueConst* /*Argv*/)
{
	FDomNode* Self = static_cast<FDomNode*>(JS_GetOpaque2(Ctx, ThisVal, GDomNodeClassId));
	if (Self == nullptr) { return JS_EXCEPTION; }
	(Self->*Method)();
	return JS_UNDEFINED;
}

// node-arg, node-return (appendChild/removeChild): pass the arg wrapper to the primitive, RETURN THE SAME wrapper
// (=== the arg — identity trivially preserved; no re-resolve).
template <void (FDomNode::*Method)(const FJsValue&)>
JSValue NodeArgMethodThunk(JSContext* Ctx, JSValueConst ThisVal, int Argc, JSValueConst* Argv)
{
	FDomNode* Self = static_cast<FDomNode*>(JS_GetOpaque2(Ctx, ThisVal, GDomNodeClassId));
	if (Self == nullptr) { return JS_EXCEPTION; }
	if (Argc < 1 || JS_GetOpaque(Argv[0], GDomNodeClassId) == nullptr)
	{
		return JS_ThrowTypeError(Ctx, "expected a DOM node argument");
	}
	FJsValue Arg(Ctx, JS_DupValue(Ctx, Argv[0]));
	(Self->*Method)(Arg);
	return JS_DupValue(Ctx, Argv[0]); // the same wrapper -> appendChild(c) === c
}

// insertBefore(node, ref | null) -> node: a node arg + a nullable node ref; returns the inserted node (the arg).
JSValue InsertBeforeThunk(JSContext* Ctx, JSValueConst ThisVal, int Argc, JSValueConst* Argv)
{
	FDomNode* Self = static_cast<FDomNode*>(JS_GetOpaque2(Ctx, ThisVal, GDomNodeClassId));
	if (Self == nullptr) { return JS_EXCEPTION; }
	if (Argc < 1 || JS_GetOpaque(Argv[0], GDomNodeClassId) == nullptr)
	{
		return JS_ThrowTypeError(Ctx, "expected a DOM node argument");
	}
	FJsValue Child(Ctx, JS_DupValue(Ctx, Argv[0]));
	FJsValue Reference;
	if (Argc >= 2 && !JS_IsNull(Argv[1]) && !JS_IsUndefined(Argv[1]))
	{
		if (JS_GetOpaque(Argv[1], GDomNodeClassId) == nullptr)
		{
			return JS_ThrowTypeError(Ctx, "insertBefore reference is not a DOM node");
		}
		Reference = FJsValue(Ctx, JS_DupValue(Ctx, Argv[1]));
	}
	Self->InsertBefore(Child, Reference);
	return JS_DupValue(Ctx, Argv[0]); // the inserted node (=== the arg)
}

// === Sprint 012 slice 3: reflected properties (behavioral-contract #1 — name-in-dom routing) ===
// A reflected property's JS member is backed by an ATTRIBUTE (not a private field): the getter reads, the setter
// writes, the live attribute store — so it can NEVER desync from setAttribute/getAttribute (Preact writes both
// paths). PURITY is the correctness guarantee: NO cached value anywhere; always read/write the attribute.
// The attribute name is a const char* non-type template param, so one template pair serves every reflected prop.
constexpr char ReflectAttrId[] = "id";       // JS 'id'        -> attribute 'id'
constexpr char ReflectAttrClass[] = "class"; // JS 'className'  -> attribute 'class' (the name translation)

template <const char* AttrName>
JSValue ReflectedGetterThunk(JSContext* Ctx, JSValueConst ThisVal)
{
	FDomNode* Self = static_cast<FDomNode*>(JS_GetOpaque2(Ctx, ThisVal, GDomNodeClassId));
	if (Self == nullptr) { return JS_EXCEPTION; }
	FString Out; // default-empty: reflected-DOMString semantics — an ABSENT attribute marshals as "" (NOT null,
	             // unlike the getAttribute METHOD). GetAttribute leaves Out untouched when absent, so Out stays "".
	Self->GetAttribute(FString(ANSI_TO_TCHAR(AttrName)), Out);
	return MarshalString(Ctx, Out).Release();
}

template <const char* AttrName>
JSValue ReflectedSetterThunk(JSContext* Ctx, JSValueConst ThisVal, JSValueConst Val)
{
	FDomNode* Self = static_cast<FDomNode*>(JS_GetOpaque2(Ctx, ThisVal, GDomNodeClassId));
	if (Self == nullptr) { return JS_EXCEPTION; }
	FString In;
	if (!UnmarshalString(Ctx, Val, In)) { return JS_ThrowTypeError(Ctx, "expected a string value"); }
	Self->SetAttribute(FString(ANSI_TO_TCHAR(AttrName)), In); // write the live attribute store (no cached field)
	return JS_UNDEFINED;
}

// === Sprint 012 events: the node event members + the live Event/KeyboardEvent object thunks ===

// addEventListener(type, fn, useCapture?) — function-marshaling: VALIDATE a JS function, hold an OWNING dup keyed
// by (type, capture) (008's store; dedup + identity in FDomNode). useCapture is the optional 3rd arg.
JSValue AddEventListenerThunk(JSContext* Ctx, JSValueConst ThisVal, int Argc, JSValueConst* Argv)
{
	FDomNode* Self = static_cast<FDomNode*>(JS_GetOpaque2(Ctx, ThisVal, GDomNodeClassId));
	if (Self == nullptr) { return JS_EXCEPTION; }
	FString Type;
	if (Argc < 2 || !UnmarshalString(Ctx, Argv[0], Type) || !JS_IsFunction(Ctx, Argv[1]))
	{
		return JS_ThrowTypeError(Ctx, "addEventListener(type: string, listener: function, useCapture?: boolean)");
	}
	const bool bCapture = (Argc >= 3) && (JS_ToBool(Ctx, Argv[2]) != 0);
	Self->AddEventListener(Type, FJsValue(Ctx, JS_DupValue(Ctx, Argv[1])), bCapture);
	return JS_UNDEFINED;
}

JSValue RemoveEventListenerThunk(JSContext* Ctx, JSValueConst ThisVal, int Argc, JSValueConst* Argv)
{
	FDomNode* Self = static_cast<FDomNode*>(JS_GetOpaque2(Ctx, ThisVal, GDomNodeClassId));
	if (Self == nullptr) { return JS_EXCEPTION; }
	FString Type;
	if (Argc < 2 || !UnmarshalString(Ctx, Argv[0], Type) || !JS_IsFunction(Ctx, Argv[1]))
	{
		return JS_ThrowTypeError(Ctx, "removeEventListener(type: string, listener: function, useCapture?: boolean)");
	}
	const bool bCapture = (Argc >= 3) && (JS_ToBool(Ctx, Argv[2]) != 0);
	Self->RemoveEventListener(Type, Argv[1], bCapture); // identity match: SameJsObject(fn) AND type AND capture
	return JS_UNDEFINED;
}

// --- The live Event / KeyboardEvent object (#2: extensible, mutated through the walk) ---
// Recover the base FDomEvent* from an Event OR KeyboardEvent wrapper (FDomKeyboardEvent's first member is the
// FDomEvent, offset 0). After dispatch the wrapper's opaque is NULLED -> nullptr -> the getters return safe
// defaults (a stored event reads safely, no dangling deref).
FDomEvent* EventBaseFrom(JSValueConst ThisVal)
{
	JSClassID ClassId = 0;
	void* Opaque = JS_GetAnyOpaque(ThisVal, &ClassId);
	if (Opaque == nullptr || (ClassId != GEventClassId && ClassId != GKeyboardEventClassId))
	{
		return nullptr;
	}
	return static_cast<FDomEvent*>(Opaque);
}

FDomKeyboardEvent* KeyboardFrom(JSValueConst ThisVal)
{
	JSClassID ClassId = 0;
	void* Opaque = JS_GetAnyOpaque(ThisVal, &ClassId);
	return (Opaque != nullptr && ClassId == GKeyboardEventClassId) ? static_cast<FDomKeyboardEvent*>(Opaque) : nullptr;
}

JSValue EventTypeGetter(JSContext* Ctx, JSValueConst ThisVal)
{
	FDomEvent* E = EventBaseFrom(ThisVal);
	return MarshalString(Ctx, E ? E->Type : FString()).Release();
}
JSValue EventTargetGetter(JSContext* Ctx, JSValueConst ThisVal)
{
	FDomEvent* E = EventBaseFrom(ThisVal);
	return (E != nullptr) ? MarshalNode(Ctx, E->Document, E->Target) : JS_NULL; // target stays the dispatch target
}
JSValue EventCurrentTargetGetter(JSContext* Ctx, JSValueConst ThisVal)
{
	FDomEvent* E = EventBaseFrom(ThisVal);
	return (E != nullptr) ? MarshalNode(Ctx, E->Document, E->CurrentTarget) : JS_NULL; // ADVANCES per node
}
JSValue EventPhaseGetter(JSContext* Ctx, JSValueConst ThisVal)
{
	FDomEvent* E = EventBaseFrom(ThisVal);
	double Phase = 0.0; // NONE
	if (E != nullptr)
	{
		Phase = (E->Phase == EDomEventPhase::Capturing) ? 1.0 : (E->Phase == EDomEventPhase::AtTarget) ? 2.0 : 3.0;
	}
	return MarshalNumber(Ctx, Phase).Release();
}
JSValue EventBubblesGetter(JSContext* Ctx, JSValueConst ThisVal)
{
	FDomEvent* E = EventBaseFrom(ThisVal);
	return MarshalBool(Ctx, E != nullptr && E->bBubbles).Release();
}
JSValue EventDefaultPreventedGetter(JSContext* Ctx, JSValueConst ThisVal)
{
	FDomEvent* E = EventBaseFrom(ThisVal);
	return MarshalBool(Ctx, E != nullptr && E->bDefaultPrevented).Release(); // a LATER listener reads this back
}
JSValue EventPreventDefaultMethod(JSContext* Ctx, JSValueConst ThisVal, int /*Argc*/, JSValueConst* /*Argv*/)
{
	if (FDomEvent* E = EventBaseFrom(ThisVal)) { E->bDefaultPrevented = true; } // MUTATE the shared event
	return JS_UNDEFINED;
}
JSValue EventStopPropagationMethod(JSContext* Ctx, JSValueConst ThisVal, int /*Argc*/, JSValueConst* /*Argv*/)
{
	if (FDomEvent* E = EventBaseFrom(ThisVal)) { E->bPropagationStopped = true; } // halts the REST of the walk
	return JS_UNDEFINED;
}
JSValue KeyboardKeyGetter(JSContext* Ctx, JSValueConst ThisVal)
{
	FDomKeyboardEvent* K = KeyboardFrom(ThisVal);
	return MarshalString(Ctx, K ? K->Key : FString()).Release();
}
JSValue KeyboardCodeGetter(JSContext* Ctx, JSValueConst ThisVal)
{
	FDomKeyboardEvent* K = KeyboardFrom(ThisVal);
	return MarshalString(Ctx, K ? K->Code : FString()).Release();
}

// dispatchEvent(event): re-dispatch a (still-live) Cathode Event object through the production walk; returns
// !defaultPrevented. (Cathode-synthetic events are dispatched C++-side via DispatchEvent/DispatchKeyboardEvent.)
JSValue DispatchEventThunk(JSContext* Ctx, JSValueConst ThisVal, int Argc, JSValueConst* Argv)
{
	FDomNode* Self = static_cast<FDomNode*>(JS_GetOpaque2(Ctx, ThisVal, GDomNodeClassId));
	if (Self == nullptr) { return JS_EXCEPTION; }
	if (Argc < 1) { return JS_ThrowTypeError(Ctx, "dispatchEvent(event)"); }
	FDomEvent* E = EventBaseFrom(Argv[0]);
	if (E == nullptr) { return JS_ThrowTypeError(Ctx, "dispatchEvent expects a live Cathode Event"); }
	Self->DispatchEvent(*E);
	return MarshalBool(Ctx, !E->bDefaultPrevented).Release();
}

// querySelector (Element): the FIRST element in tree PRE-ORDER matching the selector, via the CathodeLayout matcher
// walking the LIVE FDomNode tree. Element-scoped = DESCENDANTS ONLY (bIncludeRoot=false -> EXCLUDE-SELF). Identity
// (===) via the node marshaler; no match -> null; an invalid/unsupported selector -> a SyntaxError (never silently
// mis-matched). The matched IMatchableNode is one of Self's descendants, which are FDomNodes -> safe downcast.
JSValue QuerySelectorThunk(JSContext* Ctx, JSValueConst ThisVal, int Argc, JSValueConst* Argv)
{
	FDomNode* Self = static_cast<FDomNode*>(JS_GetOpaque2(Ctx, ThisVal, GDomNodeClassId));
	if (Self == nullptr) { return JS_EXCEPTION; }
	FString Selector;
	if (Argc < 1 || !UnmarshalString(Ctx, Argv[0], Selector)) { return JS_ThrowTypeError(Ctx, "querySelector expects a selector string"); }
	FDomDocument* Doc = Self->GetOwningDocument();
	const ISelectorService* Service = (Doc != nullptr) ? Doc->GetSelectorService() : nullptr;
	if (Service == nullptr) { return JS_ThrowInternalError(Ctx, "querySelector: no selector service installed"); }
	const IMatchableNode* Match = nullptr;
	const ESelectorStatus Status = Service->QueryFirst(*Self, /*bIncludeRoot*/ false, Selector, Match);
	if (Status == ESelectorStatus::InvalidSelector) { return JS_ThrowSyntaxError(Ctx, "querySelector: invalid or unsupported selector"); }
	if (Status == ESelectorStatus::Matched && Match != nullptr)
	{
		return MarshalNode(Ctx, Doc, static_cast<const FDomNode*>(Match)->GetNodeId());
	}
	return JS_NULL;
}

// querySelectorAll (Element): ALL matching DESCENDANTS (exclude-self) in pre-order -> a STATIC NodeList snapshot.
// Same matcher + invalid-selector contract as querySelector; empty list (not null) when nothing matches.
JSValue QuerySelectorAllThunk(JSContext* Ctx, JSValueConst ThisVal, int Argc, JSValueConst* Argv)
{
	FDomNode* Self = static_cast<FDomNode*>(JS_GetOpaque2(Ctx, ThisVal, GDomNodeClassId));
	if (Self == nullptr) { return JS_EXCEPTION; }
	FString Selector;
	if (Argc < 1 || !UnmarshalString(Ctx, Argv[0], Selector)) { return JS_ThrowTypeError(Ctx, "querySelectorAll expects a selector string"); }
	FDomDocument* Doc = Self->GetOwningDocument();
	const ISelectorService* Service = (Doc != nullptr) ? Doc->GetSelectorService() : nullptr;
	if (Service == nullptr) { return JS_ThrowInternalError(Ctx, "querySelectorAll: no selector service installed"); }
	TArray<const IMatchableNode*> Matches;
	const ESelectorStatus Status = Service->QueryAll(*Self, /*bIncludeRoot*/ false, Selector, Matches);
	if (Status == ESelectorStatus::InvalidSelector) { return JS_ThrowSyntaxError(Ctx, "querySelectorAll: invalid or unsupported selector"); }
	return MarshalMatchListToNodeList(Ctx, Doc, Matches);
}

// matches (Element): does THIS element match the selector (the primitive tested on self) -> boolean.
JSValue MatchesThunk(JSContext* Ctx, JSValueConst ThisVal, int Argc, JSValueConst* Argv)
{
	FDomNode* Self = static_cast<FDomNode*>(JS_GetOpaque2(Ctx, ThisVal, GDomNodeClassId));
	if (Self == nullptr) { return JS_EXCEPTION; }
	FString Selector;
	if (Argc < 1 || !UnmarshalString(Ctx, Argv[0], Selector)) { return JS_ThrowTypeError(Ctx, "matches expects a selector string"); }
	FDomDocument* Doc = Self->GetOwningDocument();
	const ISelectorService* Service = (Doc != nullptr) ? Doc->GetSelectorService() : nullptr;
	if (Service == nullptr) { return JS_ThrowInternalError(Ctx, "matches: no selector service installed"); }
	bool bMatches = false;
	const ESelectorStatus Status = Service->Matches(*Self, Selector, bMatches);
	if (Status == ESelectorStatus::InvalidSelector) { return JS_ThrowSyntaxError(Ctx, "matches: invalid or unsupported selector"); }
	return MarshalBool(Ctx, bMatches).Release();
}

// closest (Element): nearest of self-or-ANCESTOR matching the selector (INCLUDES self, unlike querySelector), or null.
JSValue ClosestThunk(JSContext* Ctx, JSValueConst ThisVal, int Argc, JSValueConst* Argv)
{
	FDomNode* Self = static_cast<FDomNode*>(JS_GetOpaque2(Ctx, ThisVal, GDomNodeClassId));
	if (Self == nullptr) { return JS_EXCEPTION; }
	FString Selector;
	if (Argc < 1 || !UnmarshalString(Ctx, Argv[0], Selector)) { return JS_ThrowTypeError(Ctx, "closest expects a selector string"); }
	FDomDocument* Doc = Self->GetOwningDocument();
	const ISelectorService* Service = (Doc != nullptr) ? Doc->GetSelectorService() : nullptr;
	if (Service == nullptr) { return JS_ThrowInternalError(Ctx, "closest: no selector service installed"); }
	const IMatchableNode* Match = nullptr;
	const ESelectorStatus Status = Service->Closest(*Self, Selector, Match);
	if (Status == ESelectorStatus::InvalidSelector) { return JS_ThrowSyntaxError(Ctx, "closest: invalid or unsupported selector"); }
	if (Status == ESelectorStatus::Matched && Match != nullptr)
	{
		return MarshalNode(Ctx, Doc, static_cast<const FDomNode*>(Match)->GetNodeId());
	}
	return JS_NULL;
}
} // namespace

// Shared (declared in CathodeDom.h): a selector-match list -> a STATIC NodeList of the matches' EXISTING wrappers
// (=== identity), reusing the slice-2 MarshalNodeList recipe. querySelectorAll on Element + Document both call this
// (compose-not-duplicate). The snapshot is inherently static — it captures the wrappers now; later DOM mutations do
// not alter a returned list.
JSValue MarshalMatchListToNodeList(JSContext* Ctx, FDomDocument* Doc, const TArray<const IMatchableNode*>& Matches)
{
	TArray<FJsValue> Wrappers;
	Wrappers.Reserve(Matches.Num());
	for (const IMatchableNode* Match : Matches)
	{
		if (Match == nullptr || Doc == nullptr) { continue; }
		FJsValue Wrapper = Doc->GetWrapperForNode(static_cast<const FDomNode*>(Match)->GetNodeId());
		if (Wrapper.IsValid()) { Wrappers.Add(MoveTemp(Wrapper)); }
	}
	return MarshalNodeList(Ctx, Wrappers).Release();
}

// Each macro emits the thunk (template instantiation) + the table entry from ONE declaration. A getter thunk is
// getter-shaped; the reinterpret_cast into JSCFunction* is the quickjs idiom (JS_NewCFunction2 dispatches on the
// JS_CFUNC_getter cproto).
#define CATHODE_BIND_GETTER(JsName, Iface, Tag, Sig, CppGetter) \
	{ JsName, Iface, EBoundMemberKind::Property, Tag, Sig, nullptr, \
	  reinterpret_cast<JSCFunction*>(&StrGetterThunk<&FDomNode::CppGetter>), nullptr }
#define CATHODE_BIND_OPT_STR_METHOD(JsName, Iface, Tag, Sig, CppMethod) \
	{ JsName, Iface, EBoundMemberKind::Method, Tag, Sig, \
	  &StrOptStrMethodThunk<&FDomNode::CppMethod>, nullptr, nullptr }
// Slice 2 shapes — each instantiates its template thunk from the C++ member name (or names a one-off thunk).
#define CATHODE_BIND_VAL_GETTER(JsName, Iface, Tag, Sig, CppGetter) \
	{ JsName, Iface, EBoundMemberKind::Property, Tag, Sig, nullptr, \
	  reinterpret_cast<JSCFunction*>(&StrValGetterThunk<&FDomNode::CppGetter>), nullptr }
#define CATHODE_BIND_NODETYPE_GETTER(JsName, Iface, Tag, Sig, CppGetter) \
	{ JsName, Iface, EBoundMemberKind::Property, Tag, Sig, nullptr, \
	  reinterpret_cast<JSCFunction*>(&NodeTypeGetterThunk<&FDomNode::CppGetter>), nullptr }
#define CATHODE_BIND_NODE_GETTER(JsName, Iface, Tag, Sig, CppGetter) \
	{ JsName, Iface, EBoundMemberKind::Property, Tag, Sig, nullptr, \
	  reinterpret_cast<JSCFunction*>(&NodeGetterThunk<&FDomNode::CppGetter>), nullptr }
#define CATHODE_BIND_GETTER_FN(JsName, Iface, Tag, Sig, GetterFn) \
	{ JsName, Iface, EBoundMemberKind::Property, Tag, Sig, nullptr, \
	  reinterpret_cast<JSCFunction*>(&GetterFn), nullptr }
#define CATHODE_BIND_STR_RW(JsName, Iface, Tag, Sig, CppGetter, CppSetter) \
	{ JsName, Iface, EBoundMemberKind::Property, Tag, Sig, nullptr, \
	  reinterpret_cast<JSCFunction*>(&StrValGetterThunk<&FDomNode::CppGetter>), \
	  reinterpret_cast<JSCFunction*>(&StrSetterThunk<&FDomNode::CppSetter>) }
#define CATHODE_BIND_OPT_RW(JsName, Iface, Tag, Sig, CppGetter, CppSetter) \
	{ JsName, Iface, EBoundMemberKind::Property, Tag, Sig, nullptr, \
	  reinterpret_cast<JSCFunction*>(&OptStrGetterThunk<&FDomNode::CppGetter>), \
	  reinterpret_cast<JSCFunction*>(&StrSetterThunk<&FDomNode::CppSetter>) }
#define CATHODE_BIND_STR_STR_METHOD(JsName, Iface, Tag, Sig, CppMethod) \
	{ JsName, Iface, EBoundMemberKind::Method, Tag, Sig, \
	  &StrStrVoidMethodThunk<&FDomNode::CppMethod>, nullptr, nullptr }
#define CATHODE_BIND_STR_METHOD(JsName, Iface, Tag, Sig, CppMethod) \
	{ JsName, Iface, EBoundMemberKind::Method, Tag, Sig, \
	  &StrVoidMethodThunk<&FDomNode::CppMethod>, nullptr, nullptr }
#define CATHODE_BIND_VOID_METHOD(JsName, Iface, Tag, Sig, CppMethod) \
	{ JsName, Iface, EBoundMemberKind::Method, Tag, Sig, \
	  &VoidMethodThunk<&FDomNode::CppMethod>, nullptr, nullptr }
#define CATHODE_BIND_NODE_METHOD(JsName, Iface, Tag, Sig, CppMethod) \
	{ JsName, Iface, EBoundMemberKind::Method, Tag, Sig, \
	  &NodeArgMethodThunk<&FDomNode::CppMethod>, nullptr, nullptr }
#define CATHODE_BIND_METHOD_FN(JsName, Iface, Tag, Sig, ThunkFn) \
	{ JsName, Iface, EBoundMemberKind::Method, Tag, Sig, &ThunkFn, nullptr, nullptr }
// Slice 3 — a reflected property: getter+setter both route to the attribute store (AttrConst), no backing field.
#define CATHODE_BIND_REFLECTED(JsName, Iface, Tag, Sig, AttrConst) \
	{ JsName, Iface, EBoundMemberKind::Property, Tag, Sig, nullptr, \
	  reinterpret_cast<JSCFunction*>(&ReflectedGetterThunk<AttrConst>), \
	  reinterpret_cast<JSCFunction*>(&ReflectedSetterThunk<AttrConst>) }

// THE ONE binding-definition table for the DOM node. INSTALL (InstallBoundMembers) + ENUMERATE
// (EnumerateBoundMembersJson) BOTH consume THIS — no second list, so install<->enumerate cannot drift. The
// node-core surface (sprint 012 S2) extends the slice-1 first members; the contract grows HERE. Names are
// distinct across ifaces (no cross-iface name clash), so the name-keyed lockstep dedupes nothing.
static const FBoundMember GDomNodeMembers[] =
{
	// Element — attributes + tag (tagName/getAttribute landed slice 1)
	CATHODE_BIND_GETTER("tagName", "Element", "margin", "string", GetTagName),
	CATHODE_BIND_OPT_STR_METHOD("getAttribute", "Element", "margin", "(qualifiedName: string) -> string | null", GetAttribute),
	CATHODE_BIND_STR_STR_METHOD("setAttribute", "Element", "required", "(qualifiedName: string, value: string) -> void", SetAttribute),
	CATHODE_BIND_STR_METHOD("removeAttribute", "Element", "required", "(qualifiedName: string) -> void", RemoveAttribute),
	// Node — kind + name/value
	CATHODE_BIND_NODETYPE_GETTER("nodeType", "Node", "required", "number", GetNodeType),
	CATHODE_BIND_VAL_GETTER("nodeName", "Node", "obs", "string", GetNodeName),
	CATHODE_BIND_OPT_RW("nodeValue", "Node", "obs", "string | null", GetNodeValue, SetNodeValue),
	// Node — tree reads (identity-preserving)
	CATHODE_BIND_NODE_GETTER("parentNode", "Node", "required", "Node | null", GetParentNode),
	CATHODE_BIND_NODE_GETTER("firstChild", "Node", "required", "Node | null", GetFirstChild),
	CATHODE_BIND_NODE_GETTER("nextSibling", "Node", "required", "Node | null", GetNextSibling),
	CATHODE_BIND_GETTER_FN("childNodes", "Node", "required", "NodeList", ChildNodesGetterThunk),
	// Node — mutations
	CATHODE_BIND_METHOD_FN("insertBefore", "Node", "required", "(node: Node, child: Node | null) -> Node", InsertBeforeThunk),
	CATHODE_BIND_NODE_METHOD("appendChild", "Node", "margin", "(node: Node) -> Node", AppendChild),
	CATHODE_BIND_NODE_METHOD("removeChild", "Node", "margin", "(child: Node) -> Node", RemoveChild),
	CATHODE_BIND_VOID_METHOD("remove", "Node", "required", "() -> void", Remove),
	CATHODE_BIND_STR_RW("textContent", "Node", "margin", "string", GetTextContent, SetTextContent),
	// CharacterData (Text)
	CATHODE_BIND_STR_RW("data", "CharacterData", "required", "string", GetData, SetData),
	// Element — reflected properties (behavioral-contract #1: name-in-dom routing; getter/setter -> attribute,
	// no backing field). className reflects the 'class' attribute (the name translation).
	CATHODE_BIND_REFLECTED("id", "Element", "reflected", "string", ReflectAttrId),
	CATHODE_BIND_REFLECTED("className", "Element", "reflected", "string", ReflectAttrClass),
	// EventTarget — the event surface (sprint 012 events): function-marshaling listener registry + dispatch.
	CATHODE_BIND_METHOD_FN("addEventListener", "EventTarget", "required", "(type: string, listener: EventListener, useCapture?: boolean) -> void", AddEventListenerThunk),
	CATHODE_BIND_METHOD_FN("removeEventListener", "EventTarget", "required", "(type: string, listener: EventListener, useCapture?: boolean) -> void", RemoveEventListenerThunk),
	CATHODE_BIND_METHOD_FN("dispatchEvent", "EventTarget", "margin", "(event: Event) -> boolean", DispatchEventThunk),
	// Element — selectors (sprint 012): querySelector scopes to descendants (exclude-self), first match in pre-order.
	CATHODE_BIND_METHOD_FN("querySelector", "Element", "required", "(selectors: string) -> Element | null", QuerySelectorThunk),
	// Element — the selector TRIO over the SAME matcher: all-vs-first (static NodeList) / self-bool / ancestor-or-self.
	CATHODE_BIND_METHOD_FN("querySelectorAll", "Element", "margin", "(selectors: string) -> NodeList", QuerySelectorAllThunk),
	CATHODE_BIND_METHOD_FN("matches", "Element", "margin", "(selectors: string) -> boolean", MatchesThunk),
	CATHODE_BIND_METHOD_FN("closest", "Element", "margin", "(selectors: string) -> Element | null", ClosestThunk),
};

#undef CATHODE_BIND_GETTER
#undef CATHODE_BIND_OPT_STR_METHOD
#undef CATHODE_BIND_VAL_GETTER
#undef CATHODE_BIND_NODETYPE_GETTER
#undef CATHODE_BIND_NODE_GETTER
#undef CATHODE_BIND_GETTER_FN
#undef CATHODE_BIND_STR_RW
#undef CATHODE_BIND_OPT_RW
#undef CATHODE_BIND_STR_STR_METHOD
#undef CATHODE_BIND_STR_METHOD
#undef CATHODE_BIND_VOID_METHOD
#undef CATHODE_BIND_NODE_METHOD
#undef CATHODE_BIND_METHOD_FN
#undef CATHODE_BIND_REFLECTED

FString GetDomNodeDescriptorJson()
{
	// ENUMERATE side: the descriptor emitted from the SAME table the members install from (lockstep source).
	return EnumerateBoundMembersJson(GDomNodeMembers, UE_ARRAY_COUNT(GDomNodeMembers));
}

JSClassID RegisterDomNodeClass(INativeClassRegistry& Registry, JSContext* Context)
{
	// One class for both node kinds. gc_mark marks the node's held refs (cycle visibility); the finalizer
	// frees the native side (de-register + FreeRT held refs). 007 owns the mechanism; the DOM supplies the hooks.
	FNativeClassDef Def;
	Def.Name = "CathodeDomNode";
	Def.Finalizer = &FinalizeNativeObject<FDomNode>;
	Def.GcMark = [](JSRuntime* Runtime, JSValueConst Value, JS_MarkFunc* MarkFunc)
	{
		JSClassID ClassId;
		if (FDomNode* Node = static_cast<FDomNode*>(JS_GetAnyOpaque(Value, &ClassId)))
		{
			Node->MarkHeldRefs(Runtime, MarkFunc);
		}
	};
	const JSClassID ClassId = Registry.RegisterClass(Def);
	GDomNodeClassId = ClassId; // the member thunks recover the native via this id

	// INSTALL side (sprint 012 slice 1): build the proto + install every bound member from the one table +
	// JS_SetClassProto. CreateWrappedObject's JS_GetClassProto path then gives every node instance the members.
	InstallBoundMembers(Context, ClassId, GDomNodeMembers, UE_ARRAY_COUNT(GDomNodeMembers));

	// Slice 2: install the NodeList TYPE (childNodes' return type — behavioral-contract #8) so MarshalNodeList can
	// mint typed snapshots. Idempotent (no-op if already present on this context's global).
	InstallNodeListType(Context);
	return ClassId;
}

// === Sprint 012 events: the Event + KeyboardEvent JS classes (NEW bound classes via the ONE mechanism) ===

namespace
{
#define CATHODE_EVENT_GETTER(JsName, Iface, Tag, Sig, GetterFn) \
	{ JsName, Iface, EBoundMemberKind::Property, Tag, Sig, nullptr, reinterpret_cast<JSCFunction*>(&GetterFn), nullptr }
#define CATHODE_EVENT_METHOD(JsName, Iface, Tag, Sig, MethodFn) \
	{ JsName, Iface, EBoundMemberKind::Method, Tag, Sig, &MethodFn, nullptr, nullptr }

// The base Event members. The same getter/method thunks serve a KeyboardEvent instance too (its opaque's first
// member is the FDomEvent) — KeyboardEvent is STANDALONE (its proto carries the base members + key/code), so the
// per-class lockstep reconciles each proto against its OWN descriptor with no conflation.
static const FBoundMember GEventMembers[] =
{
	CATHODE_EVENT_GETTER("type", "Event", "required", "string", EventTypeGetter),
	CATHODE_EVENT_GETTER("target", "Event", "required", "EventTarget | null", EventTargetGetter),
	CATHODE_EVENT_GETTER("currentTarget", "Event", "margin", "EventTarget | null", EventCurrentTargetGetter),
	CATHODE_EVENT_GETTER("eventPhase", "Event", "obs", "number", EventPhaseGetter),
	CATHODE_EVENT_GETTER("bubbles", "Event", "required", "boolean", EventBubblesGetter),
	CATHODE_EVENT_GETTER("defaultPrevented", "Event", "obs", "boolean", EventDefaultPreventedGetter),
	CATHODE_EVENT_METHOD("preventDefault", "Event", "required", "() -> void", EventPreventDefaultMethod),
	CATHODE_EVENT_METHOD("stopPropagation", "Event", "required", "() -> void", EventStopPropagationMethod),
};

static const FBoundMember GKeyboardEventMembers[] =
{
	CATHODE_EVENT_GETTER("type", "KeyboardEvent", "required", "string", EventTypeGetter),
	CATHODE_EVENT_GETTER("target", "KeyboardEvent", "required", "EventTarget | null", EventTargetGetter),
	CATHODE_EVENT_GETTER("currentTarget", "KeyboardEvent", "margin", "EventTarget | null", EventCurrentTargetGetter),
	CATHODE_EVENT_GETTER("eventPhase", "KeyboardEvent", "obs", "number", EventPhaseGetter),
	CATHODE_EVENT_GETTER("bubbles", "KeyboardEvent", "required", "boolean", EventBubblesGetter),
	CATHODE_EVENT_GETTER("defaultPrevented", "KeyboardEvent", "obs", "boolean", EventDefaultPreventedGetter),
	CATHODE_EVENT_METHOD("preventDefault", "KeyboardEvent", "required", "() -> void", EventPreventDefaultMethod),
	CATHODE_EVENT_METHOD("stopPropagation", "KeyboardEvent", "required", "() -> void", EventStopPropagationMethod),
	CATHODE_EVENT_GETTER("key", "KeyboardEvent", "required", "string", KeyboardKeyGetter),
	CATHODE_EVENT_GETTER("code", "KeyboardEvent", "margin", "string", KeyboardCodeGetter),
};
#undef CATHODE_EVENT_GETTER
#undef CATHODE_EVENT_METHOD
} // namespace

void RegisterDomEventClasses(INativeClassRegistry& Registry, JSContext* Context,
	JSClassID* OutEventClassId, JSClassID* OutKeyboardEventClassId)
{
	// NON-OWNING event wrappers (the FDomEvent is the dispatch's stack; the dispatch NULLs the opaque after) ->
	// NULL finalizer; NO gc_mark (the event holds no JS refs of its own — target/currentTarget resolve via the
	// document, not held). The members install from the one table (crit-1).
	FNativeClassDef EventDef;
	EventDef.Name = "CathodeDomEvent";
	EventDef.Finalizer = nullptr;
	EventDef.GcMark = nullptr;
	GEventClassId = Registry.RegisterClass(EventDef);
	InstallBoundMembers(Context, GEventClassId, GEventMembers, UE_ARRAY_COUNT(GEventMembers));

	FNativeClassDef KeyboardDef;
	KeyboardDef.Name = "CathodeDomKeyboardEvent";
	KeyboardDef.Finalizer = nullptr;
	KeyboardDef.GcMark = nullptr;
	GKeyboardEventClassId = Registry.RegisterClass(KeyboardDef);
	InstallBoundMembers(Context, GKeyboardEventClassId, GKeyboardEventMembers, UE_ARRAY_COUNT(GKeyboardEventMembers));

	if (OutEventClassId != nullptr) { *OutEventClassId = GEventClassId; }
	if (OutKeyboardEventClassId != nullptr) { *OutKeyboardEventClassId = GKeyboardEventClassId; }
}

FString GetDomEventDescriptorJson()
{
	return EnumerateBoundMembersJson(GEventMembers, UE_ARRAY_COUNT(GEventMembers));
}

FString GetDomKeyboardEventDescriptorJson()
{
	return EnumerateBoundMembersJson(GKeyboardEventMembers, UE_ARRAY_COUNT(GKeyboardEventMembers));
}

} // namespace Cathode
