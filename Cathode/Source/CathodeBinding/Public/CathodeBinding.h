#pragma once

#include "CoreMinimal.h"
#include "Templates/Function.h"
#include "Templates/UniquePtr.h"

#include "CathodeJsRuntime.h" // ICathodeJsRuntime — the binding composes 002's ONE confined context

// Cathode JS Binding — the generic machinery that makes C++ objects/functions appear to JS as real values
// and keeps both heaps' lifetimes correct. Unlike the other Cathode components this one is VENDOR-CONFINED
// rather than couples-to-none: it includes quickjs.h and calls quickjs-ng binding primitives, and it needs
// the live JSContext that 002 (Runtime Core) owns. The SWAP BOUNDARY here is the MODULE boundary itself —
// consumers (DOM, Host Bridge, Runtime Core) go through this API, never raw quickjs.
//
// RQ2 DECISION (settled by the human's locked ruling, not re-litigated): this PUBLIC header INCLUDES
// quickjs.h and EXPOSES quickjs-ng's own conventions (JSValue / JSContext / JSClassID) directly — it does
// NOT wrap them in an own opaque type. Rationale: the human ruled "we are exposing the spec the JS runtime
// provides" + "do not invent a type system." Binding (class registration + opaque + finalizer + value
// marshaling) is inherently engine-specific; an opaque wrapper would be either a ceremony-only pass-through
// or the very type-system we were told not to build, and would give FALSE isolation (swapping engines
// rewrites the binding + its consumers regardless). The isolation that DOES hold is the module boundary.
//
// The vendored quickjs-ng header is wired by the execution-approval sprint's build (vendor/lib/cathode/
// quickjs-ng/quickjs.h); no build runs this sprint. Slice 1 = surface/shapes only — no vendor call bodies.
THIRD_PARTY_INCLUDES_START
#include "quickjs.h"
THIRD_PARTY_INCLUDES_END

namespace Cathode
{

// === Value RAII (doc: "RAII over JS refcounts") ===
//
// Owns one reference to a JSValue (paired with its JSContext) and enforces JS_DupValue / JS_FreeValue on
// copy / move / destruction — manual refcounting is the classic embedding leak; RAII makes it safe. It
// EXPOSES the held JSValue (GetValue) — RAII over the refcount, not an abstraction that hides the value.
// (Special members + accessors that touch the vendor are declared here; bodies land in Slice 2.)
class CATHODEBINDING_API FJsValue
{
public:
	FJsValue() = default;                              // an empty holder (no owned ref)
	FJsValue(JSContext* InContext, JSValue InValue);   // takes ownership of ONE existing reference
	~FJsValue();

	FJsValue(const FJsValue& Other);                   // JS_DupValue (a new owned reference)
	FJsValue& operator=(const FJsValue& Other);
	FJsValue(FJsValue&& Other) noexcept;               // transfer ownership (no dup)
	FJsValue& operator=(FJsValue&& Other) noexcept;

	bool IsValid() const { return Context != nullptr; }
	JSContext* GetContext() const { return Context; }
	JSValue GetValue() const { return Value; }         // exposed — consumers use -ng's value directly

	JSValue Release();                                 // relinquish ownership WITHOUT freeing (hand off the ref)

	// Mark this held reference for the collector during a class's gc_mark, so -ng can traverse it and reclaim
	// cross-heap cycles (Model A: lifetime is JS-refcount/GC-governed). No-op when empty. (Slice 4.)
	void Mark(JSRuntime* Runtime, JS_MarkFunc* MarkFunc) const;

	// Release this reference via the RUNTIME (JS_FreeValueRT), for use INSIDE a class finalizer — which runs
	// with rt but NO ctx, so the ctx-based destructor must never run there. Empties the holder so its later
	// destructor is a no-op. Use ONLY on the finalizer path; normal scope uses the destructor. (Slice 4.)
	void FreeRT(JSRuntime* Runtime);

private:
	JSContext* Context = nullptr;
	JSValue Value = JS_UNDEFINED; // valid only while Context != nullptr
};

// === Appendable native-class registration (doc: "provide the binding mechanism") ===
//
// A native class a consumer APPENDS — generic, names no consumer-specific type. The finalizer is the
// native-side cleanup BACKSTOP (it runs when a wrapped instance is GC'd; quickjs-ng has NO native
// FinalizationRegistry, so the class finalizer IS the backstop). The optional gc_mark is quickjs-ng's
// cycle-traversal hook: marking an instance's held JS refs lets the collector see through some cross-heap
// cycles (relevant to the teardown policy — Slice 4 decides its role alongside explicit teardown).
struct FNativeClassDef
{
	const char* Name = nullptr;          // class name (the consumer's, e.g. its element/host-object class)
	JSClassFinalizer* Finalizer = nullptr;
	JSClassGCMark* GcMark = nullptr;     // optional: mark held refs so the GC can traverse the cycle
};

// One generic, APPENDABLE registry consumers add their native classes to (DOM appends its node class, Host
// Bridge appends its game-object class — to the SAME registry). Backed by JS_NewClassID + JS_NewClass +
// JS_SetOpaque + JS_NewObjectClass (bodies Slice 2). Created over 002's context (factory below).
class INativeClassRegistry
{
public:
	virtual ~INativeClassRegistry() = default;

	// Append a native class -> its JSClassID. The layer names no consumer type; the consumer supplies it.
	virtual JSClassID RegisterClass(const FNativeClassDef& Def) = 0;

	// Create a wrapped JS object of a registered class with an opaque native pointer attached (JS_SetOpaque).
	virtual FJsValue CreateWrappedObject(JSClassID ClassId, void* Opaque) = 0;

	virtual bool IsRegistered(JSClassID ClassId) const = 0;
};

// === The two generic handle mechanisms quickjs-ng already provides ===

// A wrapped-native-object handle: a JS object of a registered class carrying an opaque native pointer. A
// thin typed view over an FJsValue; exposes the opaque (JS_GetOpaque) — generic, no node/game-object.
class FNativeObjectHandle
{
public:
	FNativeObjectHandle() = default;
	explicit FNativeObjectHandle(FJsValue InObject);

	bool IsValid() const;
	const FJsValue& Object() const { return WrappedObject; }
	void* GetOpaque(JSClassID ExpectedClass) const; // JS_GetOpaque (Slice 2 body)

private:
	FJsValue WrappedObject;
};

// A held-callable handle: owns a JS function value native-side (e.g. a listener held by a native object)
// and invokes it; RAII-manages the function's refcount via FJsValue. Generic.
class CATHODEBINDING_API FCallableHandle
{
public:
	FCallableHandle() = default;
	explicit FCallableHandle(FJsValue InFunction);

	bool IsValid() const;
	// Invoke with already-marshaled JS args; returns the result (the engine's pending exception, if any, is
	// retrievable through 002's TakePendingError). JS_Call (Slice 3 body).
	FJsValue Call(JSValueConst ThisVal, TArrayView<const JSValue> Args);

private:
	FJsValue Function;
};

// === Bounded marshaling (doc: "marshal values within a small, fixed vocabulary; reject exotic") ===
//
// A CLASSIFICATION of quickjs-ng values into the accepted vocabulary — NOT an invented value type. The
// converters below move the vocabulary directly between C++ and quickjs-ng (no intermediate tagged-union;
// that would be the forbidden type system). Arrays/records compose the element converters.
enum class EJsValueKind : uint8
{
	Number,
	String,
	Bool,
	Null,
	Undefined,
	Array,         // a plain JS array of vocabulary values
	Record,        // a plain string-keyed object of vocabulary values
	NativeObject,  // a wrapped-native-object (registered class + opaque)
	Callable,      // a JS function held native-side
	Rejected,      // exotic: Symbol / BigInt / typed-array / ArrayBuffer / Date / RegExp / Proxy / cyclic
};

// The accept/reject boundary: classify a JSValue into the vocabulary (Rejected for anything exotic). The
// classifier is the gate; consumers never marshal a Rejected value. Pass the registry to precisely
// distinguish a wrapped NativeObject (a registered class) from a plain Record; without it, a custom-class
// object cannot be confirmed as ours and reads as Record (best-effort). (Body Slice 3 — uses JS_IsNumber/
// String/Bool/Null/Undefined/Array/Function + JS_IsSymbol/BigInt/Date/Proxy/GetTypedArrayType for rejection.)
CATHODEBINDING_API EJsValueKind ClassifyJsValue(JSContext* Context, JSValueConst Value, const INativeClassRegistry* Registry = nullptr);

// Typed converters (vocabulary expressed in C++ <-> -ng terms). Reading returns false on a kind mismatch
// (the reject path). Bodies Slice 3.
CATHODEBINDING_API FJsValue MarshalNumber(JSContext* Context, double Number);
CATHODEBINDING_API FJsValue MarshalString(JSContext* Context, const FString& String);
CATHODEBINDING_API FJsValue MarshalBool(JSContext* Context, bool bValue);
CATHODEBINDING_API bool UnmarshalNumber(JSContext* Context, JSValueConst Value, double& OutNumber);
CATHODEBINDING_API bool UnmarshalString(JSContext* Context, JSValueConst Value, FString& OutString);
CATHODEBINDING_API bool UnmarshalBool(JSContext* Context, JSValueConst Value, bool& OutBool);

// === Teardown policy (the heart — Model A: JS-refcount-governed lifetime) ===
//
// HUMAN DECISION (sprint 007): a wrapped native object's lifetime is governed SOLELY by its JS wrapper's
// refcount — there is NO independent C++ root and NO app-side held-ref registry (that would be the parallel
// bookkeeping we were told not to build). The cross-heap cycle (a native object holding a JS callable that
// closes over the native's wrapper) is closed by the ENGINE's own mechanisms:
//   1. gc_mark — a registered class's gc_mark marks the instance's held JS refs (FJsValue::Mark ->
//      JS_MarkValue), making the cycle VISIBLE so -ng's collector can reclaim it. (Cycle visibility.)
//   2. the class FINALIZER — frees the native C++ side when an instance is collected. It runs with rt but NO
//      ctx, so any held FJsValue must be released via FJsValue::FreeRT (JS_FreeValueRT), never the ctx-based
//      destructor. NOTE: the per-class JSClassFinalizer fires PER-INSTANCE as each is collected (refcount
//      reaches zero, a GC cycle reclaim, or the JS_FreeRuntime graph teardown). It does NOT carry the
//      "LIFO at the very end" guarantee — that is the separate JSRuntimeFinalizer mechanism, not this.
//   3. JS_FreeRuntime — the blanket dispose at shutdown (002 owns this in its ordered teardown).
// Reclamation is GC-TIMED (non-deterministic); that is the accepted cost. Consumers (the future DOM) are
// thereby committed to JS-refcount-governed lifetime (source of truth = the JS wrapper graph).

// Generic class finalizer: recover the native object via its opaque and delete it (the native's destructor
// frees its own held FJsValue refs via FJsValue::FreeRT, having stored the JSRuntime* at creation — a
// finalizer has rt but no ctx). Assign as JSClassDef.finalizer (matches JSClassFinalizer). Safe when the
// opaque is null (already torn down / a different class).
template <typename TNative>
void FinalizeNativeObject(JSRuntime* /*Runtime*/, JSValueConst Value)
{
	// TODO-PREBUILD(signature): JS_GetAnyOpaque(obj, &class_id) -> void* (quickjs.h:1042); JSValueConst arg in
	// -ng + writes class_id out. Shares the fork surface of JS_GetOpaque (tagged in Slice 2); production
	// finalizer path.
	JSClassID ClassId;
	if (TNative* Native = static_cast<TNative*>(JS_GetAnyOpaque(Value, &ClassId)))
	{
		delete Native;
	}
}

// === Construction (over 002's ONE confined context) ===
//
// The factories obtain the live JSContext from 002 via the new opaque-context accessor (see below) and
// cast it back internally — never standing up a second runtime. Provisional in Slice 1; bodies + module in
// Slice 2.
CATHODEBINDING_API TUniquePtr<INativeClassRegistry> CreateNativeClassRegistry(ICathodeJsRuntime& Runtime);

// === 002's ONE new accessor (signature specified here; APPLIED to ICathodeJsRuntime in Slice 2) ===
//
// ICathodeJsRuntime gains exactly one method:
//
//     virtual void* GetNativeContextHandle() const = 0;
//
// It returns the live JSContext as an opaque void* so 002's public header stays JS_*-free; this binding
// layer casts it back via static_cast<JSContext*>(Runtime.GetNativeContextHandle()) inside its impl. The
// JSRuntime is derived from it (JS_GetRuntime), so ONE accessor suffices. Worker-thread-only (002's thread
// affinity invariant). The rest of accepted 002 stays byte-identical.

} // namespace Cathode
