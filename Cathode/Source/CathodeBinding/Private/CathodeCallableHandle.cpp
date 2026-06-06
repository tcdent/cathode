#include "CathodeBinding.h"

// quickjs-ng — confined to this TU. Include path wired at the execution-approval sprint; no build this sprint.
THIRD_PARTY_INCLUDES_START
#include "quickjs.h"
THIRD_PARTY_INCLUDES_END

namespace Cathode
{

FCallableHandle::FCallableHandle(FJsValue InFunction)
	: Function(MoveTemp(InFunction))
{
	// Owns the JS function value via FJsValue RAII — the held-listener/callback shape; the ref is freed when
	// this handle (and its FJsValue) dies.
}

bool FCallableHandle::IsValid() const
{
	return Function.IsValid();
}

FJsValue FCallableHandle::Call(JSValueConst ThisVal, TArrayView<const JSValue> Args)
{
	if (!Function.IsValid())
	{
		return FJsValue();
	}
	JSContext* Context = Function.GetContext();

	// TODO-PREBUILD(signature): JS_Call(ctx, func_obj, this_obj, argc, argv) (quickjs.h:995). argv is
	// JSValueConst* — whether JSValueConst aliases JSValue (and thus the const_cast below is exact) depends
	// on the -ng build config (the const-checker variant). A thrown call returns JS_EXCEPTION, retrievable
	// via 002's TakePendingError; the returned value is a new reference FJsValue takes ownership of.
	JSValue Result = JS_Call(Context, Function.GetValue(), ThisVal,
		Args.Num(), const_cast<JSValue*>(Args.GetData()));

	return FJsValue(Context, Result);
}

} // namespace Cathode
