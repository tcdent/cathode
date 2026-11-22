#if WITH_DEV_AUTOMATION_TESTS

#include "CathodeDom.h"
#include "CathodeJsRuntime.h"
#include "CathodeRuntimeTesting.h"

#include "Misc/AutomationTest.h"

THIRD_PARTY_INCLUDES_START
#include "quickjs.h"
THIRD_PARTY_INCLUDES_END

// Forms — value/checked LIVE state (the dirty-value model). value/checked are the LIVE input state,
// DISTINCT from the value/checked ATTRIBUTES (= defaultValue/defaultChecked). The proof is the FOUR-STEP
// discriminating sequence (a naive round-trip is insufficient; an always-live field fails steps 1-2, an
// always-attribute reflection fails steps 3-4) + the serialization leak test (the live value must NOT
// reach the layout-feed markup — SerializeNode emits the attribute defaults).

namespace
{
using namespace Cathode;

class FStubLayoutEngine final : public ILayoutEngine
{
public:
	virtual void SetDocument(const ILayoutDocumentSource&) override {}
	virtual void MarkNodeDirty(int32, EDirtyKind) override {}
	virtual FLayoutResult Relayout(int32) override { return FLayoutResult(); }
};

JSValue Eval(JSContext* Context, const char* Source)
{
	return JS_Eval(Context, Source, FCStringAnsi::Strlen(Source), "<cathode-forms-test>", JS_EVAL_TYPE_GLOBAL);
}
} // namespace

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FCathodeDomFormsTest,
	"Cathode.Dom.Forms.ValueChecked",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCathodeDomFormsTest::RunTest(const FString& Parameters)
{
	using namespace Cathode;

	Cathode::FScopedTestRuntime RuntimePtr;
	if (!RuntimePtr.IsValid()) { AddError(TEXT("Cathode test runtime failed to initialize")); return false; }
	TUniquePtr<INativeClassRegistry> Registry = CreateNativeClassRegistry(*RuntimePtr);
	FStubLayoutEngine Layout;
	TUniquePtr<FDomDocument> Doc = CreateDomDocument(*RuntimePtr, *Registry, Layout);
	JSContext* Context = static_cast<JSContext*>(RuntimePtr->GetNativeContextHandle());

	FJsValue InputW = Doc->CreateElement(TEXT("input"));
	Doc->FindNode(Doc->GetRootNode())->AppendChild(InputW); // in the tree (so GetDocumentHtml serializes it)

	JSValue Global = JS_GetGlobalObject(Context);
	JS_SetPropertyStr(Context, Global, "__input", JS_DupValue(Context, InputW.GetValue()));
	JS_FreeValue(Context, Global);

	auto EvalBool = [&](const char* Expr) -> bool
	{
		JSValue R = Eval(Context, Expr);
		if (JS_IsException(R)) { JS_FreeValue(Context, R); AddError(FString::Printf(TEXT("JS threw: %s"), ANSI_TO_TCHAR(Expr))); return false; }
		bool bValue = false; const bool bOk = UnmarshalBool(Context, R, bValue);
		JS_FreeValue(Context, R);
		return bOk && bValue;
	};

	// ===== VALUE — the FOUR-STEP discriminating sequence (state accumulates on __input) =====
	// (1) INITIAL reflects-default: no 'value' attribute -> el.value === '' (reflects the absent content attribute).
	TestTrue(TEXT("value (1) initial reflects-default: el.value === '' (no value attr)"),
		EvalBool("__input.value === '' && __input.getAttribute('value') === null"));
	// (2) setAttribute BEFORE dirty reflects: el.value tracks the content attribute while not-dirty.
	TestTrue(TEXT("value (2) setAttribute before dirty -> el.value reflects it ('d')"),
		EvalBool("(function(){ __input.setAttribute('value','d'); return __input.value === 'd'; })()"));
	// (3) SET diverges: el.value='x' -> the live value and the attribute are now SEPARATE.
	TestTrue(TEXT("value (3) set diverges: el.value='x' -> value==='x' AND getAttribute stays 'd'"),
		EvalBool("(function(){ __input.value='x'; return __input.value==='x' && __input.getAttribute('value')==='d'; })()"));
	// (4) setAttribute AFTER dirty does NOT clobber: the attribute updates independently; the live value stays.
	TestTrue(TEXT("value (4) setAttribute after dirty does NOT clobber: value stays 'x', getAttribute -> 'd2'"),
		EvalBool("(function(){ __input.setAttribute('value','d2'); return __input.value==='x' && __input.getAttribute('value')==='d2'; })()"));

	// ===== CHECKED — the FOUR-STEP sequence with PRESENCE semantics =====
	// (1) INITIAL reflects-defaultChecked: no 'checked' attribute -> el.checked === false.
	TestTrue(TEXT("checked (1) initial reflects-defaultChecked: el.checked === false (no checked attr)"),
		EvalBool("__input.checked === false && __input.getAttribute('checked') === null"));
	// (2) setAttribute('checked') BEFORE dirty reflects: presence -> el.checked === true.
	TestTrue(TEXT("checked (2) setAttribute('checked') before dirty -> el.checked === true"),
		EvalBool("(function(){ __input.setAttribute('checked',''); return __input.checked === true; })()"));
	// (3) SET diverges: el.checked=false (live false) while the attribute (default true) is STILL present.
	TestTrue(TEXT("checked (3) set diverges: el.checked=false -> checked===false AND the checked attribute still present"),
		EvalBool("(function(){ __input.checked=false; return __input.checked===false && __input.getAttribute('checked')!==null; })()"));
	// (4) removeAttribute AFTER dirty does NOT clobber: flip live to true, remove the attr (default->false) -> stays true.
	TestTrue(TEXT("checked (4) attr change after dirty does NOT clobber: el.checked stays true though the attribute is removed"),
		EvalBool("(function(){ __input.checked=true; __input.removeAttribute('checked'); return __input.checked===true && __input.getAttribute('checked')===null; })()"));

	// ===== SERIALIZATION LEAK TEST: the LIVE value must NOT leak into the layout-feed markup =====
	// State now: LiveValue='x' (dirty) but the value ATTRIBUTE='d2'; LiveChecked=true (dirty) but NO checked attribute.
	// SerializeNode emits the ATTRIBUTES (the defaults), never the live state.
	const FString Html = Doc->GetDocumentHtml();
	TestTrue(TEXT("serialization: markup carries the value ATTRIBUTE 'd2', NOT the live value 'x'"),
		Html.Contains(TEXT("value=\"d2\"")) && !Html.Contains(TEXT("value=\"x\"")));
	TestTrue(TEXT("serialization: the dirty live checked (true) did NOT inject a 'checked' attribute into the markup"),
		!Html.Contains(TEXT("checked")));

	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
