#pragma once

#include "CoreMinimal.h"

// The DOM composes THREE built siblings (it wraps no new vendor):
#include "CathodeBinding.h" // FJsValue (+ Mark/FreeRT), INativeClassRegistry, FNativeClassDef,
                            //      FCallableHandle, FinalizeNativeObject, marshaling, ICathodeJsRuntime
#include "CathodeLayout.h"  // ILayoutDocumentSource, ILayoutEngine, EDirtyKind, FResolvedStyle, FLayoutResult
#include "CathodeSelector.h" // selectors: IMatchableNode (FDomNode implements) + ISelectorService (NEUTRAL seam — no litehtml)
// CathodeSurfaces' ISurfaceDocument is the surface<->DOM binding resolved at INTEGRATION (a surface's
// document resolves to a DOM); it is named in COVERAGE, not coupled into this header.

// Cathode DOM — the live retained document author JavaScript manipulates and the source of truth fed to
// layout. Its surface is deliberately small: that bound is the firewall against rebuilding a browser.
//
// MODEL A (the node-lifetime model): a node's lifetime = its JS wrapper's refcount;
// the source of truth is the JS WRAPPER GRAPH; there is NO independent C++ owning root. CathodeLayout PERMITS this
// (its layout seam is markup-serialization — litehtml rebuilds a transient tree from our markup; no DOM C++
// node crosses into it) but does not CONFER it: keeping the id-addressable node structure the CathodeLayout seam needs
// (NodeId->node, parent/child, layout-boundary) WRAPPER-GOVERNED rather than a C++ root is the DOM's to uphold.
// removeChild / removeEventListener are REFERENCE-DROPS, not
// explicit-free triggers — CathodeBinding's gc_mark + finalizer reclaim.
//
// The DOM is quickjs-type-coupled BY DESIGN (via CathodeBinding's "expose the spec" public API — JSValue/JSContext/
// JSClassID reach this header through CathodeBinding.h). It composes the sibling APIs; it builds no layout,
// no binding mechanism, no surface — it drives them.

namespace Cathode
{

// A DOM node id — int32, matching CathodeLayout's ILayoutDocumentSource node-id space (the DOM IS the source CathodeLayout reads).
using FDomNodeId = int32;

class FDomDocument; // forward — FDomEvent carries a non-owning back-ref so target/currentTarget marshal to wrappers

// One registered quickjs class covers both kinds (element + text node) — Text nodes ignore element-only ops.
enum class EDomNodeType : uint8
{
	Element,
	Text,
};

// --- Event shapes (FROZEN canonical surface; the source for the downstream .d.ts — do not widen) ---
//
// Capture + bubble phases. Pointer/mouse fields are not implemented (no pointer input). Minimal Event +
// KeyboardEvent{key,code}.
enum class EDomEventPhase : uint8
{
	Capturing, // the capture phase (ancestors, top-down) before the target
	AtTarget,
	Bubbling,
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
	// The document EventTarget phase: the document sits ABOVE the root node in the bubble path, but it is not
	// an FDomNode (no node id), so CurrentTarget can't name it. During the document capture/bubble pass this is set true;
	// EventCurrentTargetGetter then marshals the DOCUMENT wrapper (not a node) so `e.currentTarget === document`.
	bool bCurrentTargetIsDocument = false;
	FDomDocument* Document = nullptr;      // non-owning — set by the dispatch so the JS event
	                                       // object's target/currentTarget getters resolve node wrappers (the node marshaler)
};

// The frozen KeyboardEvent — the base Event plus { key, code } (keyboard is the live input path).
struct FDomKeyboardEvent
{
	FDomEvent Event;
	FString Key;  // e.g. "a", "Enter"
	FString Code; // e.g. "KeyA", "Enter"
};

// How a selected listener is invoked during dispatch. PRODUCTION wraps the listener in a CathodeBinding
// FCallableHandle and Calls it with the JS Event object wrapping the live FDomEvent. Tests inject a RECORDING invoker to assert dispatch order + simulate stopPropagation/
// preventDefault (by setting the Event flags), so the dispatch ALGORITHM is testable in C++ without a
// JS Event object.
using FDomListenerInvoker = TFunction<void(const FJsValue& ListenerFn, FDomEvent& Event)>;

// A registered listener: the HELD JS function (owning FJsValue — gc_mark-marked, the
// node<->listener cycle visibility) keyed by (type, capture). removeEventListener matches the full triple.
struct FDomEventListener
{
	FString Type;
	FJsValue Listener;
	bool bCapture = false;
};

// --- The DOM node ---
//
// A C++ object registered as a quickjs class via CathodeBinding; the JS wrapper owns it (Model A). MODEL A
// NODE-LIFETIME CONTRACT:
//   * REGISTRATION: RegisterDomNodeClass (below) registers ONE class via CathodeBinding's INativeClassRegistry::RegisterClass
//     with (a) a gc_mark that marks the node's held refs — child WRAPPERS, listener callables, JS-valued attrs
//     — via FJsValue::Mark (so -ng's collector traverses the tree + reclaims cross-heap cycles), and (b)
//     FinalizeNativeObject<FDomNode> as the finalizer (frees the native side when the wrapper is collected;
//     held refs released via FJsValue::FreeRT — finalizer-safe, no ctx).
//   * RETAINED TREE: parent->child links are JS REFS (FJsValue to the child's wrapper), gc_mark-marked, so a
//     node's life = its wrapper's refcount and the tree is rooted at the document's JS ref (persists between
//     ticks). NO independent C++ owning root: any C++ traversal pointer (e.g. a back-ref to the document for
//     context/dirty-marking) is NON-OWNING/derived. The id-addressable index (NodeId->node) the CathodeLayout seam
//     needs is wrapper-governed — entries are removed by the finalizer, never an owning root.
//   * REFERENCE-DROPS: removeChild drops the parent's held child-wrapper ref; removeEventListener drops the
//     node's matched held listener ref. No explicit free — the GC reclaims (CathodeBinding).
class FDomDocument; // the owning document (FDomNode holds a non-owning back-ref + exposes GetOwningDocument)
// FDomNode IS-A IMatchableNode (selectors): the matcher walks the LIVE retained tree through this neutral
// interface (NOT litehtml's per-layout snapshot), so a freshly-mutated node matches without a relayout.
class FDomNode : public IMatchableNode
{
public:
	// Identity / kind
	FDomNodeId GetNodeId() const;
	EDomNodeType GetNodeType() const;
	const FString& GetTagName() const; // element only ("" for text nodes)

	// Bounded structural API. MODEL A: the lifetime-governing parent->child link is the child's WRAPPER
	// (FJsValue) held in the parent — so the ops traffic in wrappers, not raw FDomNode* (a node never holds
	// its own wrapper; that would self-root it = Model B). AppendChild/InsertBefore/RemoveChild are the JS-ref
	// link/drop (the retained-tree mechanism) and dirty-mark on mutation. removeChild is a REFERENCE-DROP
	// (drop the held child-wrapper ref; CathodeBinding reclaims).
	void AppendChild(const FJsValue& ChildWrapper);
	void InsertBefore(const FJsValue& ChildWrapper, const FJsValue& ReferenceWrapper); // empty Reference => append
	void RemoveChild(const FJsValue& ChildWrapper);

	// Tree reads — return NodeIds (INDEX_NONE when absent)
	FDomNodeId GetParentNode() const;
	void GetChildNodes(TArray<FDomNodeId>& OutChildren) const;
	FDomNodeId GetFirstChild() const;
	FDomNodeId GetNextSibling() const;

	// Attribute / property access (values cross via CathodeBinding marshaling)
	bool GetAttribute(const FString& Name, FString& OutValue) const;
	void SetAttribute(const FString& Name, const FString& Value);
	void RemoveAttribute(const FString& Name);
	// Form LIVE state — value/checked are the LIVE input state, DISTINCT from the value/checked
	// ATTRIBUTES (= defaultValue/defaultChecked). The dirty-value model: the getter returns the live state once set,
	// else the attribute default; the setter writes the LIVE state and does NOT touch the attribute. NOT serialized
	// (runtime DOM state; SerializeNode emits the attribute defaults). See LiveValue/LiveChecked below.
	FString GetValue() const;                         // el.value: LiveValue if set, else the 'value' attribute ("" if none)
	void SetValue(const FString& Value);              // el.value = x: sets the LIVE value (dirty); attribute untouched
	bool GetChecked() const;                          // el.checked: LiveChecked if set, else the 'checked' attribute presence
	void SetChecked(bool bChecked);                   // el.checked = b: sets the LIVE checked (dirty); attribute untouched
	// Focus (HTMLElement) — sets/clears the DOCUMENT's focus STATE (runtime, not serialized;
	// the C++ input router is the future consumer). focus() -> document.activeElement; blur() clears it if this node holds it.
	void Focus();
	void Blur();
	FString GetTextContent() const;                   // bulk textContent (get) — FROZEN in
	void SetTextContent(const FString& Text);         // bulk textContent (set) — FROZEN in

	// Event system. A listener is a HELD JS function stored as FJsValue (the held ref): it must be
	// directly gc_mark-markable (the node<->listener cycle is THE Model-A hazard) and identity-matchable for
	// removal — CathodeBinding's FCallableHandle hides its function, so the listener is held as FJsValue
	// and FCallableHandle is used transiently for INVOCATION. removeEventListener = the reference-DROP.
	// (type, fn, capture) — the listener is keyed by the (type, capture) tuple + fn identity.
	// DOM DEDUP: adding the same (type, fn, capture) triple twice registers ONCE. removeEventListener removes the
	// matching triple only (SameJsObject(fn) AND type AND capture); a mismatch on ANY of the three is a no-op.
	void AddEventListener(const FString& EventType, FJsValue Listener, bool bCapture = false);
	void RemoveEventListener(const FString& EventType, JSValueConst ListenerFn, bool bCapture = false);
	// Dispatch through the live shared event: capture (ancestors top-down) -> AtTarget -> bubble (ancestors
	// bottom-up); the (type,capture) tuple selects which listeners fire per phase. The Invoker is handed the live
	// shared Event each step (currentTarget/phase advance; preventDefault/stopPropagation mutate it).
	void DispatchEvent(FDomEvent& Event);             // production: build the JS Event object + invoke via CathodeBinding's FCallableHandle
	CATHODEDOM_API void DispatchKeyboardEvent(FDomKeyboardEvent& Event); // exported — the key-dispatch seam (CathodeComposition) delivers the worker-posted keydown via this; a KeyboardEvent object (key/code) over the base
	void DispatchEvent(FDomEvent& Event, const FDomListenerInvoker& Invoker); // the testable dispatch algorithm

	// Observation reads — the C++ backing; geometry/style come from CathodeLayout.
	bool GetBoundingClientRect(FBox2f& OutBox) const;
	bool GetComputedStyle(FResolvedStyle& OutStyle) const;

	// --- Thin backing for the node-core member bindings (spec-anchored, additive — no change
	//     to existing behavior). The bound thunks marshal these; data/remove are the Preact-required deltas. ---
	FString GetNodeName() const;                 // Node.nodeName — element: tag name; text: "#text"
	bool GetNodeValue(FString& OutValue) const;  // Node.nodeValue get — text: data (true); element: null (false)
	void SetNodeValue(const FString& Value);     // Node.nodeValue set — text: set data; element: no-op (spec)
	FString GetData() const;                     // CharacterData.data get — the text node's character data
	void SetData(const FString& Value);          // CharacterData.data set — text mutation (dirty-marks Layout)
	void Remove();                               // ChildNode.remove() — detach self from its parent (ref-drop)
	void GetChildWrappers(TArray<FJsValue>& OutWrappers) const; // childNodes snapshot: the OWNING child wrappers
	FDomDocument* GetOwningDocument() const;     // the owning document (for the node marshaler); null if orphaned

	// --- IMatchableNode (selectors): the neutral structural surface the CathodeLayout matcher walks. Reads the
	//     LIVE node (tag/attrs/parent/child elements) off the retained tree — never a serialized snapshot. ---
	virtual FString GetMatchTagName() const override;
	virtual bool GetMatchAttribute(const FString& Name, FString& OutValue) const override; // case-insensitive (HTML attr names)
	virtual const IMatchableNode* GetMatchParent() const override;
	virtual void GetMatchChildren(TArray<const IMatchableNode*>& OutChildren) const override; // child ELEMENTS, in order

	// The node's attributes as (name, value) pairs in deterministic (sorted-key) order —
	// the source for the Element.attributes NamedNodeMap SNAPSHOT (minimal { name, value } items, no Attr class).
	void GetAttributesSnapshot(TArray<TPair<FString, FString>>& OutAttributes) const;

	// Node.namespaceURI — the element's namespace (STORED at creation). Elements -> the HTML ns (createElement) or the passed ns (createElementNS);
	// text nodes -> false (JS null).
	bool GetNamespaceURI(FString& OutValue) const;

	// --- Internal mechanism (Model A) — not the author surface ---
	// Finalizer-driven (FinalizeNativeObject<FDomNode> -> delete): de-register from the document's NON-OWNING
	// index via the back-ref (if still valid), then FreeRT held refs (rt-based — a finalizer has no ctx).
	~FDomNode();
	// gc_mark helper: mark the node's held refs (child wrappers + listener callables) so -ng's
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
	FString NamespaceURI;              // element ns: HTML ns (createElement) / passed ns (createElementNS)
	FDomDocument* Document = nullptr;  // NON-OWNING back-ref (de-register/context/dirty-mark); NULLED if orphaned
	JSRuntime* Runtime = nullptr;      // for FreeRT on the finalizer path (no ctx available there)
	FDomNodeId ParentId = INDEX_NONE;  // NON-OWNING parent back-ref (resolved via the index)
	TArray<FJsValue> Children;         // OWNING JS refs to child WRAPPERS (Model A; gc_mark-marked) — NOT FDomNode*
	TMap<FString, FString> Attributes; // plain STRING data (the bounded surface is string-valued) — NOT held JS refs
	// Form LIVE state, DISTINCT from the value/checked attributes (= defaultValue/defaultChecked).
	// Unset (TOptional empty) = not-yet-dirty -> el.value/checked reflect the attribute default; set = dirty -> the
	// live state wins. RUNTIME-only (NOT serialized — SerializeNode emits the attributes, not these).
	TOptional<FString> LiveValue;      // el.value live state (the dirty-value-flag); unset -> the 'value' attribute
	TOptional<bool> LiveChecked;       // el.checked live state; unset -> the 'checked' attribute presence (defaultChecked)
	TArray<FDomEventListener> Listeners;        // (type, held JS function, capture) — HELD JS refs, gc_mark-marked (the
	                                            // node<->listener cycle, THE Model-A hazard); FreeRT'd in the finalizer

	// A NON-OWNING (borrowed, never JS_DupValue'd) reverse-lookup ref to THIS node's OWN
	// wrapper, set once at creation. Model A invariant: no OWNING self-reference, but a
	// non-owning borrow is permitted — it adds NO refcount (so NO self-root / Model-B leak), is NOT gc_marked and
	// NOT FreeRT'd, and cannot dangle (the wrapper OWNS the node via the opaque + finalizer, so the node never
	// outlives its wrapper). Powers GetWrapperForNode in O(1), complete for root/attached/detached.
	// See CathodeDom/COVERAGE.md.
	JSValue WrapperBorrow = JS_UNDEFINED;
	void SetWrapperBorrow(JSValue InWrapper) { WrapperBorrow = InWrapper; } // borrow: stored WITHOUT a dup
};

// --- The document ---
//
// The root of the retained tree + the node factory + the CathodeLayout handshake. It IS-A ILayoutDocumentSource
// (CathodeLayout reads structure + markup from it) and drives ILayoutEngine (SetDocument/MarkNodeDirty/Relayout). The
// NodeId->node index it maintains for the seam is wrapper-governed (Model A). Each mutating API call dirty-
// marks the affected region via MarkNodeDirty so CathodeLayout relayouts only it.
class FDomDocument : public ILayoutDocumentSource
{
public:
	FDomDocument(ICathodeJsRuntime& InRuntime, INativeClassRegistry& InRegistry, ILayoutEngine& InLayout,
		JSClassID InNodeClassId);

	// DISPOSE = the runtime release (CathodeBinding's pattern: gc-at-runtime + JS_FreeRuntime fallback). Drops the
	// document's OWNED root ref WHILE THE RUNTIME IS ALIVE, so the tree reclaims by GC (acyclic refcount
	// cascade now; cycles on the next JS_RunGC). "Dispose the document disposes the tree."
	// CATHODEDOM_API — the production owner (CathodeComposition) posts this onto the worker at surface teardown
	// (cross-module). MUST run on the worker/runtime thread (quickjs C-API calls).
	CATHODEDOM_API void Dispose();

	// The C++ destructor is PASSIVE / CONTEXT-FREE — it touches NO runtime/context, so it is safe in ANY
	// order (disposed or not, runtime alive or dead): it ABANDONS the root ref without freeing (Release — no
	// quickjs call; anything left undisposed is reaped by JS_FreeRuntime, the fallback) and ORPHANS any
	// surviving indexed nodes (pure C++ back-ref nulling) so their later finalizer skips the gone index. NO
	// ordering requirement (the runtime release lives in Dispose, not here). See CathodeDomDocument.cpp.
	virtual ~FDomDocument() override;

	// Node factory — creates a wrapped node (CathodeBinding's CreateWrappedObject) and registers it in the non-owning
	// index. Returns the OWNING WRAPPER (FJsValue): the caller holds the lifetime handle (as JS holds a
	// createElement result). A node never holds its own wrapper (that would self-root it = Model B).
	FJsValue CreateElement(const FString& TagName);
	// createElementNS: like CreateElement but STORES the passed namespace (-> Node.namespaceURI).
	FJsValue CreateElementNS(const FString& Namespace, const FString& QualifiedName);
	FJsValue CreateTextNode(const FString& Data);

	CATHODEDOM_API FDomNodeId GetRootNodeId() const;             // exported — the DispatchKeyToSurface seam (CathodeComposition) resolves the dispatch target
	CATHODEDOM_API FDomNode* FindNode(FDomNodeId NodeId) const;  // wrapper-governed index lookup (non-owning); nullptr if unknown
	FDomNode* NodeFromWrapper(const FJsValue& Wrapper) const; // wrapper -> its C++ node via the opaque (derived)

	// Resolve a node id to its EXISTING wrapper (the node marshaler) via the node's NON-OWNING wrapper borrow
	// (set once at creation) — O(1) and complete for root / attached / DETACHED (no parent-reachability needed).
	// Returns a NEW OWNING ref to the SAME wrapper (JS_DupValue) so JS identity (===) holds; the borrow stays
	// non-owning. Empty (-> JS null) for INDEX_NONE / an unknown id / a node with no wrapper set. Sound because a
	// node is only in the index while its wrapper is alive (finalizer de-registers) -> no read of a dead borrow.
	FJsValue GetWrapperForNode(FDomNodeId NodeId) const;

	// --- The document subsystem ---
	// getElementById's backing: find an element by its id ATTRIBUTE (the reflected `id`) — a tree walk
	// from the root checking GetAttribute("id"). Returns the matching node id, INDEX_NONE if none. The JS binding
	// marshals the result via GetWrapperForNode so it is the SAME wrapper (identity ===).
	FDomNodeId FindElementById(const FString& ElementId) const;

	// document.body — the first <body> element in document order (a pre-order tree walk
	// by tag, like FindElementById; no stored ref). INDEX_NONE if none (-> JS null). On the document, like getElementById.
	CATHODEDOM_API FDomNodeId FindBody() const; // exported — the key-dispatch seam's keydown-target fallback (focused element -> body -> root)

	// Focus STATE (runtime — NOT serialized): the focused node (document.activeElement). focus() sets it,
	// blur() clears it (only if the given node currently holds focus). The C++ input router is the future consumer.
	void SetFocusedNode(FDomNodeId NodeId);
	void BlurNode(FDomNodeId NodeId);
	CATHODEDOM_API FDomNodeId GetFocusedNode() const; // exported — the key-dispatch seam's primary keydown target (document.activeElement)

	// The document is an EventTarget at the TOP of the bubble path. A keydown dispatched at a node bubbles up
	// the node chain to the ROOT and then to the DOCUMENT (focused element -> ... -> root -> document), so
	// `document.addEventListener('keydown', ...)` fires. Mirrors the FDomNode EventTarget machinery; the document fires only
	// when it HAS listeners (a no-listener document is a zero-iteration no-op append -> no change to node-only dispatch).
	void AddEventListener(const FString& EventType, FJsValue Listener, bool bCapture = false);
	void RemoveEventListener(const FString& EventType, JSValueConst ListenerFn, bool bCapture = false);
	// document.dispatchEvent: dispatch AT the document (no ancestors). The invoker passes the author's event object
	// (DispatchJsEventAtDocument supplies it) — no self-marshaling overload (a fresh wrapper would lose identity + type).
	void DispatchEvent(FDomEvent& Event, const FDomListenerInvoker& Invoker);
	// Fire THIS document's matching listeners for one phase (capture/at-target/bubble); returns false to HALT (stopPropagation).
	// Called by FDomNode::DispatchEvent (the bubble splice — document is the outermost EventTarget) + the document-as-target path.
	bool FireListeners(FDomEvent& Event, EDomEventPhase Phase, bool bCapturePhase, const FDomListenerInvoker& Invoker);
	// The document's OWN wrapper (the one installed as globalThis.document) for `e.currentTarget === document` identity.
	FJsValue GetDocumentWrapper() const;

	// Expose THIS document as the JS `document` global on its own context: wrap it via the document JS class (CathodeBinding's
	// CreateWrappedObject — NON-OWNING: the document is C++/TUniquePtr-owned, the wrapper never deletes it) and set
	// globalThis.document. The PRODUCTION install method; the per-surface owner (CathodeComposition)
	// fires it — document is per-surface, not per-runtime.
	CATHODEDOM_API void InstallAsGlobal(); // exported — CathodeComposition installs it cross-module

	// NB: the document JS class has NO gc_mark (intentionally absent) + a NULL finalizer — it is C++/TUniquePtr-
	// owned. Reclaim is Dispose() (drops RootWrapper) + the node gc_mark (intra-tree cycles); RootWrapper's C++
	// refcount governs survival. The document's own listeners are NOT gc-marked: the null-finalizer document
	// outlives its wrapper, so collecting a document<->listener cycle would dangle its listener refs; production
	// keeps globalThis.document rooted, so that cycle is never collected. See the lifetime note in
	// CathodeDom/COVERAGE.md.

	// --- Selectors: the matcher service (CathodeLayout-implemented, litehtml-grounded). Held by an
	//     ABSTRACT pointer (no litehtml type enters CathodeDom). Installed at construction (CreateDomDocument's
	//     optional arg) or via the setter; node/document querySelector routes here. Null until installed -> the
	//     querySelector thunk throws (a wiring invariant — production + the proof test always install it).
	void SetSelectorService(const ISelectorService* InService);
	const ISelectorService* GetSelectorService() const;

	// Drive CathodeLayout: relayout at the available width + cache the result so the observation reads can compose
	// its geometry/style. The DOM serializes + drives; it does NOT lay out.
	void Relayout(int32 AvailableWidth);

	// getComputedStyle LAZY flush: relayout-if-dirty using the last Relayout's width, so a
	// getComputedStyle read reflects pending element.style writes. Writes only dirty-mark; the read flushes the
	// ACCUMULATED dirty into ONE relayout (browser flush-on-read; batches writes; not eager-per-write).
	void FlushLayoutIfDirty();

	// --- CathodeLayout's ILayoutDocumentSource: structure reads + markup serialization ---
	virtual int32 GetRootNode() const override;
	virtual int32 GetParentNode(int32 NodeId) const override;
	virtual void GetChildNodes(int32 NodeId, TArray<int32>& OutChildren) const override;
	virtual bool IsLayoutBoundary(int32 NodeId) const override;
	CATHODEDOM_API virtual FString GetDocumentHtml() const override; // serialize the whole tree to markup (exported — read cross-module)
	virtual FString GetSubtreeHtml(int32 NodeId) const override; // serialize a subtree (the efficient re-feed path)
	virtual FString GetDocumentCss() const override;

private:
	friend class FDomNode;
	void UnregisterNode(FDomNodeId NodeId); // called by ~FDomNode — de-register from the NON-OWNING index
	void MarkDirty(FDomNodeId NodeId, EDirtyKind Kind); // mutation -> CathodeLayout's ILayoutEngine::MarkNodeDirty
	void SerializeNode(const FDomNode* Node, FString& Out) const; // recursive DOM -> HTML markup

	ICathodeJsRuntime& Runtime;
	INativeClassRegistry& Registry;
	ILayoutEngine& Layout;
	const ISelectorService* SelectorService = nullptr; // selectors: CathodeLayout's matcher (abstract; no litehtml here)
	JSContext* Context = nullptr;          // CathodeRuntime's live context (wrapping/marshaling), via GetNativeContextHandle
	JSClassID NodeClassId = 0;             // the registered DOM-node class id
	int32 NextNodeId = 0;
	TMap<FDomNodeId, FDomNode*> NodeIndex; // NON-OWNING index (Model A): the wrapper graph owns; this only looks up
	FJsValue RootWrapper;                  // the document's OWNING ref to the root node's wrapper (the GC root)
	FDomNodeId RootNodeId = INDEX_NONE;
	FDomNodeId FocusedNodeId = INDEX_NONE; // focus state (document.activeElement); runtime, not serialized
	TArray<FDomEventListener> Listeners;   // the document's EventTarget listeners (held JS refs; NOT gc_mark-marked — see the NB above)
	JSValue DocumentWrapperBorrow = JS_UNDEFINED; // non-owning borrow of the installed `document` wrapper (=== identity)
	FLayoutResult LastLayout;              // the last Relayout result; the observation reads compose its geometry/style
	bool bLayoutDirty = false;             // a layout-affecting write is pending since the last Relayout
	int32 LastLayoutWidth = 0;             // the width the flush re-relayouts with (the last Relayout's); 0 = never laid out
};

// Bridge for document.dispatchEvent: recover the base FDomEvent from a JS event arg + dispatch AT the document.
// Defined in CathodeDomNode.cpp (where the file-local event-recovery helper lives); the document thunk (CathodeDomDocument.cpp)
// routes through it. Internal helper (no CATHODEDOM_API — within CathodeDom). bOutValidEvent=false if the arg isn't a Cathode Event.
bool DispatchJsEventAtDocument(JSValueConst EventArg, FDomDocument& Doc, bool& bOutValidEvent);

// --- Registration + construction (composing the siblings) ---

// Register the single DOM node class via CathodeBinding — wires the gc_mark (held-ref marking) + the finalizer
// (FinalizeNativeObject<FDomNode>), and INSTALLS the bound members onto the class proto
// from the single binding-definition table (needs the live JSContext). Returns the class id used to wrap nodes.
CATHODEDOM_API JSClassID RegisterDomNodeClass(INativeClassRegistry& Registry, JSContext* Context);

// The DOM node's declared-member DESCRIPTOR, emitted from the SAME binding-definition
// table the members install from — CathodeVerify/SCHEMA.md section-2 shape. The CathodeVerify faculty consumes this as
// __CATHODE_DECLARED__ (the declared<->installed lockstep). Projection/test seam; no production faculty coupling.
CATHODEDOM_API FString GetDomNodeDescriptorJson();

// Register the FDomDocument JS class via CathodeBinding — the single binding-definition mechanism
// extended to a SECOND class: a NULL non-owning finalizer + NO gc_mark (the document is C++/TUniquePtr-owned;
// reclaim is Dispose() + the node gc_mark, and RootWrapper's C++ refcount governs survival — see the
// CathodeDom/COVERAGE.md lifetime note) + INSTALLS the document members (getElementById/createElement/
// createTextNode/createElementNS) from GDomDocumentMembers. CreateDomDocument calls this.
CATHODEDOM_API JSClassID RegisterDomDocumentClass(INativeClassRegistry& Registry, JSContext* Context);

// The FDomDocument declared-member DESCRIPTOR, emitted from GDomDocumentMembers via the SAME
// EnumerateBoundMembersJson (the declared<->installed lockstep, PER CLASS — reconciled vs the live document proto).
CATHODEDOM_API FString GetDomDocumentDescriptorJson();

// Register the Event + KeyboardEvent JS classes — NEW bound classes via the SAME mechanism
// (GEventMembers / GKeyboardEventMembers through InstallBoundMembers). The dispatch wraps the live shared
// FDomEvent in an Event object passed to listeners. CreateDomDocument calls this; the optional out-params hand
// back the class ids (the lockstep test builds the Event/KeyboardEvent proto probes from them).
CATHODEDOM_API void RegisterDomEventClasses(INativeClassRegistry& Registry, JSContext* Context,
	JSClassID* OutEventClassId = nullptr, JSClassID* OutKeyboardEventClassId = nullptr);

// The Event + KeyboardEvent declared-member DESCRIPTORS, from their tables via EnumerateBoundMembersJson (the
// lockstep, per class — reconciled vs the live Event / KeyboardEvent protos).
CATHODEDOM_API FString GetDomEventDescriptorJson();
CATHODEDOM_API FString GetDomKeyboardEventDescriptorJson();

// Internal (shared across the DOM TUs): marshal a selector-match list (live FDomNodes, as
// IMatchableNode*) to a STATIC NodeList of their EXISTING wrappers (=== identity), reusing the childNodes NodeList
// recipe. querySelectorAll on BOTH Element (CathodeDomNode.cpp) and Document (CathodeDomDocument.cpp) calls this —
// one marshaling, no duplication. Defined in CathodeDomNode.cpp. Not exported (same-module use only).
JSValue MarshalMatchListToNodeList(JSContext* Ctx, FDomDocument* Doc, const TArray<const IMatchableNode*>& Matches);

// Construct a DOM document over CathodeBinding's runtime/registry (for wrapping + lifetime) and CathodeLayout's
// layout engine (for the handshake). Composes the siblings; stands up no second runtime, builds no layout.
// The optional Selector arg installs the matcher service so node/document querySelector works; defaulted null
// so call sites that don't exercise selectors need not pass it. Production + the selector proof
// pass CreateSelectorService().
CATHODEDOM_API TUniquePtr<FDomDocument> CreateDomDocument(
	ICathodeJsRuntime& Runtime, INativeClassRegistry& Registry, ILayoutEngine& Layout,
	const ISelectorService* Selector = nullptr);

#if WITH_DEV_AUTOMATION_TESTS
// The WORKER-BUILD marker (test-only): the thread CreateDomDocument (the JS DOM-build entry) last ran on.
// The worker-build test asserts build-thread == raster-thread (== the same auto-started worker) AND != game — proving the
// FDomDocument BUILD and the COMPOSE are CO-LOCATED on the worker. 0 = no build yet. Mirrors GetLastRasterThreadId.
CATHODEDOM_API uint32 GetLastDomBuildThreadId();
#endif

// Marshal a JS value to a DOM attribute/property STRING via CathodeBinding's bounded vocabulary: String as-is, Number
// stringified, Bool -> "true"/"false"; REJECTS exotic + non-scalar (Null/Undefined/Array/Record/NativeObject/
// Callable/exotic) returning false. This is the value-crossing the generated JS thunks use; the C++ attribute
// API itself is string-typed.
CATHODEDOM_API bool MarshalAttributeValue(JSContext* Context, JSValueConst Value, FString& OutString);

} // namespace Cathode
