#include "CathodeBinding.h"

#include "Containers/StringConv.h"

// quickjs-ng — confined to this TU. Include path wired at the execution-approval sprint; no build this sprint.
THIRD_PARTY_INCLUDES_START
#include "quickjs.h"
THIRD_PARTY_INCLUDES_END

namespace Cathode
{

EJsValueKind ClassifyJsValue(JSContext* Context, JSValueConst Value, const INativeClassRegistry* Registry)
{
	// Primitives.
	if (JS_IsNull(Value))      { return EJsValueKind::Null; }
	if (JS_IsUndefined(Value)) { return EJsValueKind::Undefined; }
	if (JS_IsNumber(Value))    { return EJsValueKind::Number; }
	if (JS_IsBool(Value))      { return EJsValueKind::Bool; }
	if (JS_IsString(Value))    { return EJsValueKind::String; }

	// Explicitly-rejected primitives.
	if (JS_IsSymbol(Value))    { return EJsValueKind::Rejected; }
	if (JS_IsBigInt(Value))    { return EJsValueKind::Rejected; }

	// Objects. Reject proxies FIRST — this check is load-bearing: in -ng JS_IsArray does NOT punch through
	// proxies (quickjs.h:937-940), so an un-caught plain Proxy is not array/function/date/etc. and would fall
	// to JS_IsObject -> Record (an ACCEPTED kind), silently letting proxies in. Proxy-first makes it Reject.
	// (Do NOT drop this on the assumption JS_IsArray screens proxies — it no longer does in -ng.)
	// TODO-PREBUILD(signature): JS_IsProxy (quickjs.h:943).
	if (JS_IsProxy(Value))     { return EJsValueKind::Rejected; }

	if (JS_IsFunction(Context, Value)) { return EJsValueKind::Callable; }
	if (JS_IsArray(Value))             { return EJsValueKind::Array; }

	// Exotic object forms with public predicates -> Rejected.
	if (JS_IsDate(Value))              { return EJsValueKind::Rejected; }
	if (JS_IsArrayBuffer(Value))       { return EJsValueKind::Rejected; }
	// TODO-PREBUILD(signature): JS_GetTypedArrayType (quickjs.h:1089) returns -1 when not a typed array.
	if (JS_GetTypedArrayType(Value) >= 0) { return EJsValueKind::Rejected; }
	if (JS_IsError(Value))             { return EJsValueKind::Rejected; }

	if (JS_IsObject(Value))
	{
		// NativeObject = a wrapped instance of one of OUR registered classes; a plain object is a Record.
		// TODO-PREBUILD(signature): JS_GetClassID (quickjs.h:690) + registry membership identifies our wrapped
		// objects precisely. NOTE: other builtin object kinds (RegExp / Map / Set / WeakMap / WeakSet /
		// DataView — several of which DO have -ng predicates, quickjs.h:925-931) also fall through to Record
		// here. They are bounded out by the DATA MODEL, not by classification: the DOM / game.* boundary never
		// carries them, so an explicit per-kind reject is unnecessary. Revisit (add precise rejects via those
		// predicates) only if that boundary ever changes.
		if (Registry != nullptr && Registry->IsRegistered(JS_GetClassID(Value)))
		{
			return EJsValueKind::NativeObject;
		}
		return EJsValueKind::Record;
	}

	return EJsValueKind::Rejected;
}

// --- Typed converters (the element converters; arrays/records compose these at the JSValue level) ---

FJsValue MarshalNumber(JSContext* Context, double Number)
{
	return FJsValue(Context, JS_NewFloat64(Context, Number));
}

FJsValue MarshalString(JSContext* Context, const FString& String)
{
	const FTCHARToUTF8 Utf8(*String);
	return FJsValue(Context, JS_NewString(Context, Utf8.Get()));
}

FJsValue MarshalBool(JSContext* Context, bool bValue)
{
	return FJsValue(Context, JS_NewBool(Context, bValue));
}

bool UnmarshalNumber(JSContext* Context, JSValueConst Value, double& OutNumber)
{
	if (!JS_IsNumber(Value))
	{
		return false; // reject: not a number
	}
	return JS_ToFloat64(Context, &OutNumber, Value) == 0;
}

bool UnmarshalString(JSContext* Context, JSValueConst Value, FString& OutString)
{
	if (!JS_IsString(Value))
	{
		return false; // reject: not a string
	}
	size_t Length = 0;
	const char* Utf8 = JS_ToCStringLen(Context, &Length, Value);
	if (Utf8 == nullptr)
	{
		return false;
	}
	OutString = FString(UTF8_TO_TCHAR(Utf8));
	JS_FreeCString(Context, Utf8);
	return true;
}

bool UnmarshalBool(JSContext* Context, JSValueConst Value, bool& OutBool)
{
	if (!JS_IsBool(Value))
	{
		return false; // reject: not a bool
	}
	const int Result = JS_ToBool(Context, Value); // -1 on exception
	if (Result < 0)
	{
		return false;
	}
	OutBool = (Result != 0);
	return true;
}

} // namespace Cathode
