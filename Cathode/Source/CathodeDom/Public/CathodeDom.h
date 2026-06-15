#pragma once

#include "CoreMinimal.h"

// The DOM composes THREE built siblings (it wraps no new vendor):
#include "CathodeBinding.h" // 007: FJsValue (+ Mark/FreeRT), INativeClassRegistry, FNativeClassDef,
                            //      FCallableHandle, FinalizeNativeObject, marshaling, ICathodeJsRuntime
#include "CathodeLayout.h"  // 005: ILayoutDocumentSource, ILayoutEngine, EDirtyKind, FResolvedStyle, FLayoutResult
// 006 CathodeSurfaces' ISurfaceDocument is the surface<->DOM binding resolved at INTEGRATION (a surface's
// document resolves to a DOM); it is named in COVERAGE, not coupled into this header.

// Cathode DOM — the live retained document author JavaScript manipulates and the source of truth fed to
// layout. Its surface is deliberately small: that bound is the firewall against rebuilding a browser.
//
// MODEL A (007's ruling, THIS sprint's design obligation): a node's lifetime = its JS wrapper's refcount;
// the source of truth is the JS WRAPPER GRAPH; there is NO independent C++ owning root. 005 PERMITS this
// (its layout seam is markup-serialization — litehtml rebuilds a transient tree from our markup; no DOM C++
// node crosses into it) but does not CONFER it: keeping the id-addressable node structure the 005 seam needs
// (NodeId->node, parent/child, layout-boundary) WRAPPER-GOVERNED rather than a C++ root is ours to uphold
// (007 COVERAGE deferral #4 carried forward). removeChild / removeEventListener are REFERENCE-DROPS, not
// explicit-free triggers — 007's gc_mark + finalizer reclaim.
//
// The DOM is quickjs-type-coupled BY DESIGN (via 007's "expose the spec" public API — JSValue/JSContext/
// JSClassID reach this header through CathodeBinding.h). It composes the sibling APIs; it builds no layout,
// no binding mechanism, no surface — it drives them.
//
// SHAPES ONLY this slice (Slice 1) — signatures + the Model A contract; bodies land in the named slices.

namespace Cathode
{

// A DOM node id — int32, matching 005's ILayoutDocumentSource node-id space (the DOM IS the source 005 reads).
using FDomNodeId = int32;

// One registered quickjs class covers both kinds (element + text node) — Text nodes ignore element-only ops.
enum class EDomNodeType : uint8
{
	Element,
	Text,
};

// --- Event shapes (FROZEN canonical surface; the source for the downstream .d.ts — do not widen) ---
//
// Bubble phase only (capture DEFERRED). Pointer/mouse fields DEFERRED (with pointer input). Minimal Event +
// KeyboardEvent{key,code}.
enum class EDomEventPhase : uint8
{
	AtTarget,
	Bubbling, // capture phase deferred
};

struct FDomEvent
{
	FString Type;                          // "click", "keydown", ...
	FDomNodeId Target = INDEX_NONE;        // the node the event was dispatched at
	FDomNodeId CurrentTarget = INDEX_NONE; // the node whose listener is running (advances as the event bubbles)
	EDomEventPhase Phase = EDomEventPhase::AtTarget;
	bool bBubbles = true;
	bool bDefaultPrevented = false;        // preventDefault() set this
	bool bPropagationStopped = false;      // stopPropagation() set this (ends bubbling)
};

// The frozen KeyboardEvent — the base Event plus { key, code } (keyboard is the live input path).
struct FDomKeyboardEvent
{
	FDomEvent Event;
	FString Key;  // e.g. "a", "Enter"
	FString Code; // e.g. "KeyA", "Enter"
};

// How a selected listener is invoked during dispatch. PRODUCTION wraps the listener in a 007 FCallableHandle
// and Calls it — the FDomEvent -> JS Event object marshaling (the event arg) follows the generate-from-
// interface deferral. Tests inject a RECORDING invoker to assert dispatch order + simulate stopPropagation/
// preventDefault (by setting the Event flags), so the dispatch ALGORITHM is testable in C++ without the
// deferred Event-object exposure.
using FDomListenerInvoker = TFunction<void(const FJsValue& ListenerFn, FDomEvent& Event)>;

// --- The DOM node ---
//
// A C++ object registered as a quickjs class via 007; the JS wrapper owns it (Model A). MODEL A NODE-LIFETIME
// CONTRACT (the heart of this slice):
//   * REGISTRATION: RegisterDomNodeClass (below) registers ONE class via 007 INativeClassRegistry::RegisterClass
//     with (a) a gc_mark that marks the node's held refs — child WRAPPERS, listener callables, JS-valued attrs
//     — via FJsValue::Mark (so -ng's collector traverses the tree + reclaims cross-heap cycles), and (b)
//     FinalizeNativeObject<FDomNode> as the finalizer (frees the native side when the wrapper is collected;
//     held refs released via FJsValue::FreeRT — finalizer-safe, no ctx).
//   * RETAINED TREE: parent->child links are JS REFS (FJsValue to the child's wrapper), gc_mark-marked, so a
//     node's life = its wrapper's refcount and the tree is rooted at the document's JS ref (persists between
//     ticks). NO independent C++ owning root: any C++ traversal pointer (e.g. a back-ref to the document for
//     context/dirty-marking) is NON-OWNING/derived. The id-addressable index (NodeId->node) the 005 seam
//     needs is wrapper-governed — entries are removed by the finalizer, never an owning root.
//   * REFERENCE-DROPS: removeChild drops the parent's held child-wrapper ref; removeEventListener drops the
//     node's matched held listener ref. No explicit free — the GC reclaims (007).
// The exact private representation (the JS-ref child container, the non-owning back-refs) is RQ1 — settled in
// Slice 2 within this non-negotiable principle; not shown here.
class FDomNode
{
public:
	// Identity / kind
	FDomNodeId GetNodeId() const;
	EDomNodeType GetNodeType() const;
	const FString& GetTagName() const; // element only ("" for text nodes)

	// Bounded structural API. MODEL A: the lifetime-governing parent->child link is the child's WRAPPER
	// (FJsValue) held in the parent — so the ops traffic in wrappers, not raw FDomNode* (a node never holds
	// its own wrapper; that would self-root it = Model B). S2 lands AppendChild/RemoveChild as the JS-ref
	// link/drop (the retained-tree mechanism); S3 adds InsertBefore, the dirty-mark on mutation, and the
	// tree-read bodies. removeChild is a REFERENCE-DROP (drop the held child-wrapper ref; 007 reclaims).
	void AppendChild(const FJsValue& ChildWrapper);
	void InsertBefore(const FJsValue& ChildWrapper, const FJsValue& ReferenceWrapper); // empty Reference => append
	void RemoveChild(const FJsValue& ChildWrapper);

	// Tree reads (Slice 3) — return NodeIds (INDEX_NONE when absent)
	FDomNodeId GetParentNode() const;
	void GetChildNodes(TArray<FDomNodeId>& OutChildren) const;
	FDomNodeId GetFirstChild() const;
	FDomNodeId GetNextSibling() const;

	// Attribute / property access (Slice 4; values cross via 007 marshaling)
	bool GetAttribute(const FString& Name, FString& OutValue) const;
	void SetAttribute(const FString& Name, const FString& Value);
	void RemoveAttribute(const FString& Name);
	bool GetValue(FString& OutValue) const;          // form value (get) — FROZEN in
	FString GetTextContent() const;                   // bulk textContent (get) — FROZEN in
	void SetTextContent(const FString& Text);         // bulk textContent (set) — FROZEN in

	// Event system (Slice 6). A listener is a HELD JS function stored as FJsValue (the held ref): it must be
	// directly gc_mark-markable (the node<->listener cycle is THE Model-A hazard) and identity-matchable for
	// removal — 007's FCallableHandle hides its function and 007 is frozen, so the listener is held as FJsValue
	// and FCallableHandle is used transiently for INVOCATION. removeEventListener = the reference-DROP.
	void AddEventListener(const FString& EventType, FJsValue Listener);
	void RemoveEventListener(const FString& EventType, JSValueConst ListenerFn); // match by JS fn identity; reference-drop
	void DispatchEvent(FDomEvent& Event);             // production: invoke via 007 FCallableHandle (event arg deferred)
	void DispatchEvent(FDomEvent& Event, const FDomListenerInvoker& Invoker); // the testable dispatch algorithm

	// Observation reads (Slice 5) — OFF the author surface (not in the .d.ts, no flag); backed by 005.
	bool GetBoundingClientRect(FBox2f& OutBox) const;
	bool GetComputedStyle(FResolvedStyle& OutStyle) const;

	// --- Internal mechanism (Model A) — not the author surface ---
	// Finalizer-driven (FinalizeNativeObject<FDomNode> -> delete): de-register from the document's NON-OWNING
	// index via the back-ref (if still valid), then FreeRT held refs (rt-based — a finalizer has no ctx).
	~FDomNode();
	// gc_mark helper: mark the node's held refs (child wrappers now; listeners/JS-attrs in S6/S4) so -ng's
	// collector traverses the tree + reclaims cross-heap cycles.
	void MarkHeldRefs(JSRuntime* InRuntime, JS_MarkFunc* MarkFunc) const;

	// True once the document orphaned this node (the document was destroyed while a surviving JS ref kept the
	// node in the index): its finalizer then SKIPS the gone index. Internal Model-A state, not the author surface.
	bool IsOrphaned() const { return Document == nullptr; }

private:
	friend class FDomDocument;
	FDomNode(FDomDocument& InDocument, FDomNodeId InNodeId, EDomNodeType InType);

	FDomNodeId NodeId = INDEX_NONE;
	EDomNodeType Type = EDomNodeType::Element;
	FString TagName;                   // element only
	FString TextData;                  // text node only
	FDomDocument* Document = nullptr;  // NON-OWNING back-ref (de-register/context/dirty-mark); NULLED if orphaned
	JSRuntime* Runtime = nullptr;      // for FreeRT on the finalizer path (no ctx available there)
	FDomNodeId ParentId = INDEX_NONE;  // NON-OWNING parent back-ref (resolved via the index)
	TArray<FJsValue> Children;         // OWNING JS refs to child WRAPPERS (Model A; gc_mark-marked) — NOT FDomNode*
	TMap<FString, FString> Attributes; // plain STRING data (the bounded surface is string-valued) — NOT held JS refs
	TArray<TPair<FString, FJsValue>> Listeners; // (type, held JS function) — HELD JS refs, gc_mark-marked (the
	                                            // node<->listener cycle, THE Model-A hazard); FreeRT'd in the finalizer
};

// --- The document ---
//
// The root of the retained tree + the node factory + the 005 layout handshake. It IS-A ILayoutDocumentSource
// (005 reads structure + markup from it) and drives ILayoutEngine (SetDocument/MarkNodeDirty/Relayout). The
// NodeId->node index it maintains for the seam is wrapper-governed (Model A). Each mutating API call dirty-
// marks the affected region via MarkNodeDirty so 005 relayouts only it.
class FDomDocument : public ILayoutDocumentSource
{
public:
	FDomDocument(ICathodeJsRuntime& InRuntime, INativeClassRegistry& InRegistry, ILayoutEngine& InLayout,
		JSClassID InNodeClassId);

	// DISPOSE = the runtime release (007's pattern: gc-at-runtime + JS_FreeRuntime fallback). Drops the
	// document's OWNED root ref WHILE THE RUNTIME IS ALIVE, so the tree reclaims by GC (acyclic refcount
	// cascade now; cycles on the next JS_RunGC). "Dispose the document disposes the tree."
	void Dispose();

	// The C++ destructor is PASSIVE / CONTEXT-FREE — it touches NO runtime/context, so it is safe in ANY
	// order (disposed or not, runtime alive or dead): it ABANDONS the root ref without freeing (Release — no
	// quickjs call; anything left undisposed is reaped by JS_FreeRuntime, the fallback) and ORPHANS any
	// surviving indexed nodes (pure C++ back-ref nulling) so their later finalizer skips the gone index. NO
	// ordering requirement (the runtime release lives in Dispose, not here). See CathodeDomDocument.cpp.
	virtual ~FDomDocument() override;

	// Node factory — creates a wrapped node (007 CreateWrappedObject) and registers it in the non-owning
	// index. Returns the OWNING WRAPPER (FJsValue): the caller holds the lifetime handle (as JS holds a
	// createElement result). A node never holds its own wrapper (that would self-root it = Model B).
	FJsValue CreateElement(const FString& TagName);
	FJsValue CreateTextNode(const FString& Data);

	FDomNodeId GetRootNodeId() const;
	FDomNode* FindNode(FDomNodeId NodeId) const; // wrapper-governed index lookup (non-owning); nullptr if unknown
	FDomNode* NodeFromWrapper(const FJsValue& Wrapper) const; // wrapper -> its C++ node via the opaque (derived)

	// Drive 005 (S5): relayout at the available width + cache the result so the observation reads can compose
	// its geometry/style. The DOM serializes + drives; it does NOT lay out.
	void Relayout(int32 AvailableWidth);

	// --- 005 ILayoutDocumentSource (Slice 5): structure reads + markup serialization ---
	virtual int32 GetRootNode() const override;
	virtual int32 GetParentNode(int32 NodeId) const override;
	virtual void GetChildNodes(int32 NodeId, TArray<int32>& OutChildren) const override;
	virtual bool IsLayoutBoundary(int32 NodeId) const override;
	virtual FString GetDocumentHtml() const override;        // serialize the whole tree to markup
	virtual FString GetSubtreeHtml(int32 NodeId) const override; // serialize a subtree (the efficient re-feed path)
	virtual FString GetDocumentCss() const override;

private:
	friend class FDomNode;
	void UnregisterNode(FDomNodeId NodeId); // called by ~FDomNode — de-register from the NON-OWNING index
	void MarkDirty(FDomNodeId NodeId, EDirtyKind Kind); // mutation -> 005 ILayoutEngine::MarkNodeDirty (S3)
	void SerializeNode(const FDomNode* Node, FString& Out) const; // recursive DOM -> HTML markup (S5)

	ICathodeJsRuntime& Runtime;
	INativeClassRegistry& Registry;
	ILayoutEngine& Layout;
	JSContext* Context = nullptr;          // 002's live context (wrapping/marshaling), via GetNativeContextHandle
	JSClassID NodeClassId = 0;             // the registered DOM-node class id
	int32 NextNodeId = 0;
	TMap<FDomNodeId, FDomNode*> NodeIndex; // NON-OWNING index (Model A): the wrapper graph owns; this only looks up
	FJsValue RootWrapper;                  // the document's OWNING ref to the root node's wrapper (the GC root)
	FDomNodeId RootNodeId = INDEX_NONE;
	FLayoutResult LastLayout;              // the last Relayout result; the observation reads compose its geometry/style
};

// --- Registration + construction (composing the siblings) ---

// Register the single DOM node class via 007 — wires the gc_mark (held-ref marking) + the finalizer
// (FinalizeNativeObject<FDomNode>), and (sprint 012 slice 1) INSTALLS the bound members onto the class proto
// from the single binding-definition table (needs the live JSContext). Returns the class id used to wrap nodes.
CATHODEDOM_API JSClassID RegisterDomNodeClass(INativeClassRegistry& Registry, JSContext* Context);

// The DOM node's declared-member DESCRIPTOR (sprint 012 slice 1), emitted from the SAME binding-definition
// table the members install from — CathodeVerify/SCHEMA.md section-2 shape. The 011 faculty consumes this as
// __CATHODE_DECLARED__ (the 011<->012 lockstep). Projection/test seam; no production faculty coupling.
CATHODEDOM_API FString GetDomNodeDescriptorJson();

// Construct a DOM document over 007's runtime/registry (for wrapping + lifetime) and 005's layout engine
// (for the handshake). Composes the siblings; stands up no second runtime, builds no layout. (Body Slice 2+.)
CATHODEDOM_API TUniquePtr<FDomDocument> CreateDomDocument(
	ICathodeJsRuntime& Runtime, INativeClassRegistry& Registry, ILayoutEngine& Layout);

// Marshal a JS value to a DOM attribute/property STRING via 007's bounded vocabulary: String as-is, Number
// stringified, Bool -> "true"/"false"; REJECTS exotic + non-scalar (Null/Undefined/Array/Record/NativeObject/
// Callable/exotic) returning false. This is the value-crossing the generated JS thunks use; the C++ attribute
// API itself is string-typed. (S4.)
CATHODEDOM_API bool MarshalAttributeValue(JSContext* Context, JSValueConst Value, FString& OutString);

} // namespace Cathode
