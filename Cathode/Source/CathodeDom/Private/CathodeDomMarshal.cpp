#include "CathodeDom.h"

THIRD_PARTY_INCLUDES_START
#include "quickjs.h"
THIRD_PARTY_INCLUDES_END

namespace Cathode
{

bool MarshalAttributeValue(JSContext* Context, JSValueConst Value, FString& OutString)
{
	// Compose 007's bounded marshaling: classify, then convert the scalar vocabulary; reject everything else.
	switch (ClassifyJsValue(Context, Value))
	{
	case EJsValueKind::String:
		return UnmarshalString(Context, Value, OutString);

	case EJsValueKind::Number:
	{
		double Number = 0.0;
		if (!UnmarshalNumber(Context, Value, Number))
		{
			return false;
		}
		// Approximate; exact JS Number->String formatting is a validation-pass refinement.
		OutString = FString::SanitizeFloat(Number);
		return true;
	}

	case EJsValueKind::Bool:
	{
		bool bValue = false;
		if (!UnmarshalBool(Context, Value, bValue))
		{
			return false;
		}
		OutString = bValue ? TEXT("true") : TEXT("false");
		return true;
	}

	default:
		// REJECT exotic + non-scalar: Null/Undefined/Array/Record/NativeObject/Callable/Rejected. Attribute
		// values are scalar strings; trusted-only, so a clear reject (no JS exception path yet).
		return false;
	}
}

} // namespace Cathode
