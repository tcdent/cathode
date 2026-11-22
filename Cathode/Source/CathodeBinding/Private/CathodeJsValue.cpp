#include "CathodeBinding.h"

#include "CathodeBindingTestHooks.h"

// quickjs-ng — confined to this TU (and the other CathodeBinding impls).
THIRD_PARTY_INCLUDES_START
#include "quickjs.h"
THIRD_PARTY_INCLUDES_END

namespace Cathode
{

#if WITH_DEV_AUTOMATION_TESTS
TFunction<void()> GJsValueOnDup;
TFunction<void()> GJsValueOnFree;
TFunction<void()> GJsValueOnFreeRT;
TFunction<void()> GJsValueOnMark;
#endif

namespace
{
// The real refcount ops run; the taps only OBSERVE our calls (no production fork — see CathodeBindingTestHooks.h).
FORCEINLINE void TapDup()
{
#if WITH_DEV_AUTOMATION_TESTS
	if (GJsValueOnDup) { GJsValueOnDup(); }
#endif
}
FORCEINLINE void TapFree()
{
#if WITH_DEV_AUTOMATION_TESTS
	if (GJsValueOnFree) { GJsValueOnFree(); }
#endif
}
FORCEINLINE void TapFreeRT()
{
#if WITH_DEV_AUTOMATION_TESTS
	if (GJsValueOnFreeRT) { GJsValueOnFreeRT(); }
#endif
}
FORCEINLINE void TapMark()
{
#if WITH_DEV_AUTOMATION_TESTS
	if (GJsValueOnMark) { GJsValueOnMark(); }
#endif
}
} // namespace

FJsValue::FJsValue(JSContext* InContext, JSValue InValue)
	: Context(InContext)
	, Value(InValue)
{
	// Takes ownership of ONE existing reference — no dup here (the caller hands us a ref to own).
}

FJsValue::~FJsValue()
{
	if (Context != nullptr)
	{
		JS_FreeValue(Context, Value);
		TapFree();
	}
}

FJsValue::FJsValue(const FJsValue& Other)
	: Context(Other.Context)
	, Value(Other.Value)
{
	// Copy => a NEW owned reference (the classic place a missing dup turns into a use-after-free).
	if (Context != nullptr)
	{
		Value = JS_DupValue(Context, Other.Value);
		TapDup();
	}
}

FJsValue& FJsValue::operator=(const FJsValue& Other)
{
	if (this != &Other)
	{
		if (Context != nullptr)
		{
			JS_FreeValue(Context, Value);
			TapFree();
		}
		Context = Other.Context;
		Value = Other.Value;
		if (Context != nullptr)
		{
			Value = JS_DupValue(Context, Other.Value);
			TapDup();
		}
	}
	return *this;
}

FJsValue::FJsValue(FJsValue&& Other) noexcept
	: Context(Other.Context)
	, Value(Other.Value)
{
	// Move => transfer the existing ref, no dup; the source is emptied so it frees nothing.
	Other.Context = nullptr;
	Other.Value = JS_UNDEFINED;
}

FJsValue& FJsValue::operator=(FJsValue&& Other) noexcept
{
	if (this != &Other)
	{
		if (Context != nullptr)
		{
			JS_FreeValue(Context, Value);
			TapFree();
		}
		Context = Other.Context;
		Value = Other.Value;
		Other.Context = nullptr;
		Other.Value = JS_UNDEFINED;
	}
	return *this;
}

JSValue FJsValue::Release()
{
	// Relinquish ownership WITHOUT freeing — the caller takes over the ref (e.g. handing it back to JS).
	JSValue Out = Value;
	Context = nullptr;
	Value = JS_UNDEFINED;
	return Out;
}

void FJsValue::Mark(JSRuntime* Runtime, JS_MarkFunc* MarkFunc) const
{
	// gc_mark visibility: let the collector traverse this held ref so a cross-heap cycle can be reclaimed
	// (JS-refcount-governed lifetime). Marking does not transfer or release ownership.
	// TODO-PREBUILD(signature): JS_MarkValue(rt, val, mark_func) (quickjs.h:511); JS_MarkFunc (quickjs.h:510).
	if (Context != nullptr)
	{
		JS_MarkValue(Runtime, Value, MarkFunc);
		TapMark();
	}
}

void FJsValue::FreeRT(JSRuntime* Runtime)
{
	// Finalizer-safe release: a class finalizer runs with rt but NO ctx, so free via the runtime, never the
	// ctx-based destructor. Empties the holder so its later destructor is a no-op (no double free).
	// TODO-PREBUILD(signature): JS_FreeValueRT(rt, v) (quickjs.h:845).
	if (Context != nullptr)
	{
		JS_FreeValueRT(Runtime, Value);
		TapFreeRT();
		Context = nullptr;
		Value = JS_UNDEFINED;
	}
}

} // namespace Cathode
