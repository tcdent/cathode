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
	for (TPair<FString, FJsValue>& Listener : Listeners)
	{
		Listener.Value.FreeRT(Runtime);
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
	for (const TPair<FString, FJsValue>& Listener : Listeners)
	{
		Listener.Value.Mark(InRuntime, MarkFunc);
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

// --- Event system (S6) — listeners are held JS refs; dispatch bubbles; removal is a reference-drop ---

void FDomNode::AddEventListener(const FString& EventType, FJsValue Listener)
{
	// Hold the listener function (a JS ref) keyed by type. gc_mark marks it (cycle visibility); the finalizer
	// FreeRTs it; removeEventListener drops it.
	Listeners.Add(TPair<FString, FJsValue>(EventType, MoveTemp(Listener)));
}

void FDomNode::RemoveEventListener(const FString& EventType, JSValueConst ListenerFn)
{
	// Reference-DROP: remove the (type, fn-by-identity) listener; the removed FJsValue's destructor frees the
	// held ref (ctx-based — removeEventListener is a normal-op call, runtime alive).
	Listeners.RemoveAll([&EventType, ListenerFn](const TPair<FString, FJsValue>& Listener)
	{
		return Listener.Key == EventType && SameJsObject(Listener.Value, ListenerFn);
	});
}

void FDomNode::DispatchEvent(FDomEvent& Event, const FDomListenerInvoker& Invoker)
{
	// The dispatch ALGORITHM: AtTarget (this node), then bubble up the parentNode chain (if Event.bBubbles).
	// At each node, invoke its type-matching listeners in registration order. stopPropagation halts the ASCENT
	// (same-node remaining listeners still run, per DOM); preventDefault is a flag the caller reads afterwards.
	if (Document == nullptr)
	{
		return;
	}
	Event.Target = NodeId;
	FDomNode* Current = this;
	EDomEventPhase Phase = EDomEventPhase::AtTarget;
	while (Current != nullptr)
	{
		Event.CurrentTarget = Current->NodeId;
		Event.Phase = Phase;
		for (const TPair<FString, FJsValue>& Listener : Current->Listeners)
		{
			if (Listener.Key == Event.Type)
			{
				Invoker(Listener.Value, Event); // a listener may set bPropagationStopped / bDefaultPrevented
			}
		}
		if (Event.bPropagationStopped || !Event.bBubbles)
		{
			break; // stopPropagation halts ascent; a non-bubbling event fires AtTarget only
		}
		Current = (Current->ParentId != INDEX_NONE) ? Document->FindNode(Current->ParentId) : nullptr;
		Phase = EDomEventPhase::Bubbling;
	}
}

void FDomNode::DispatchEvent(FDomEvent& Event)
{
	// Production: invoke each selected listener via a transient 007 FCallableHandle. The FDomEvent -> JS Event
	// object marshaling (the event arg) follows the generate-from-interface deferral — the listener is invoked
	// here but does not yet receive a marshaled Event object.
	DispatchEvent(Event, [](const FJsValue& ListenerFn, FDomEvent& /*Event*/)
	{
		FCallableHandle Handle(ListenerFn); // transient: copies (dups) the held listener, frees on destruction
		Handle.Call(JS_UNDEFINED, TArrayView<const JSValue>());
	});
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
} // namespace

// Each macro emits the thunk (template instantiation) + the table entry from ONE declaration. A getter thunk is
// getter-shaped; the reinterpret_cast into JSCFunction* is the quickjs idiom (JS_NewCFunction2 dispatches on the
// JS_CFUNC_getter cproto).
#define CATHODE_BIND_GETTER(JsName, Iface, Tag, Sig, CppGetter) \
	{ JsName, Iface, EBoundMemberKind::Property, Tag, Sig, nullptr, \
	  reinterpret_cast<JSCFunction*>(&StrGetterThunk<&FDomNode::CppGetter>), nullptr }
#define CATHODE_BIND_OPT_STR_METHOD(JsName, Iface, Tag, Sig, CppMethod) \
	{ JsName, Iface, EBoundMemberKind::Method, Tag, Sig, \
	  &StrOptStrMethodThunk<&FDomNode::CppMethod>, nullptr, nullptr }

// THE ONE binding-definition table for the DOM node. INSTALL (InstallBoundMembers) + ENUMERATE
// (EnumerateBoundMembersJson) BOTH consume THIS — no second list, so install<->enumerate cannot drift. Slice 1
// proves the mechanism with the first members; later slices extend the table (the member contract grows here).
static const FBoundMember GDomNodeMembers[] =
{
	CATHODE_BIND_GETTER("tagName", "Element", "margin", "string", GetTagName),
	CATHODE_BIND_OPT_STR_METHOD("getAttribute", "Element", "margin", "(qualifiedName: string) -> string | null", GetAttribute),
};

#undef CATHODE_BIND_GETTER
#undef CATHODE_BIND_OPT_STR_METHOD

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
	return ClassId;
}

} // namespace Cathode
