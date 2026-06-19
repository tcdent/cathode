#if WITH_DEV_AUTOMATION_TESTS

#include "CathodeDom.h"
#include "CathodeJsRuntime.h"
#include "CathodeRuntimeTesting.h"

#include "Misc/AutomationTest.h"

THIRD_PARTY_INCLUDES_START
#include "quickjs.h"
THIRD_PARTY_INCLUDES_END

// 018 slice 1 — CREATE + APPEND -> the C++ RETAINED TREE (C++-direct). Real JS (016 Evaluate) mints two UNIQUE-tagged
// elements via the bound document.createElement + appends them to a parent via parent.appendChild (017's binding); then
// C++ reads the RETAINED FDomNode tree DIRECTLY (NodeFromWrapper -> GetTagName / GetChildNodes) -- NOT querySelector
// (selector-service-mediated = INDIRECT, the PRIMARY reject; in Model A FDomNode.Children ARE the wrapper refs, no shadow,
// but querySelector routes through the selector service = a confound). The genuine gap: the JS-MINTED node's C++ identity
// + the EXACT-order parenting (appendChild's bare C++-verify exists on a C++-minted node, EventTests:436; this is on
// JS-MINTED nodes + asserts ORDER, stronger than Contains).
// DISCRIMINATORS: UNIQUE tags (lowercase to sidestep HTML tag-case-normalization; a no-op/echo -> wrong tag FAILS); EXACT
// order [a,b] (mis-order FAILS); a no-op create -> NodeFromWrapper null/wrong tag; a no-op append -> the parent lacks a/b.

namespace
{
using namespace Cathode;

class FStubMeasurer final : public ITextMeasurer
{
public:
	virtual FLayoutFontRef ResolveFont(const FString&, int32, int32, bool) override { return FLayoutFontRef(); }
	virtual float MeasureWidth(const FString&, FLayoutFontRef) override { return 0.0f; }
	virtual void GetFontMetrics(FLayoutFontRef, float& A, float& D, float& H) override { A = 12.0f; D = 4.0f; H = 16.0f; }
};
} // namespace

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FCathodeDomJsCreateAppendTest,
	"Cathode.Dom.JsDriven.CreateAppendRetainedTree",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCathodeDomJsCreateAppendTest::RunTest(const FString& Parameters)
{
	using namespace Cathode;

	FScopedTestRuntime Runtime;
	if (!Runtime.IsValid()) { AddError(TEXT("runtime failed to start")); return false; }
	TUniquePtr<INativeClassRegistry> Registry = CreateNativeClassRegistry(*Runtime);
	FStubMeasurer Measurer;
	TUniquePtr<ILayoutEngine> Engine = CreateLiteHtmlLayoutEngine(Measurer);
	TUniquePtr<FDomDocument> Doc = CreateDomDocument(*Runtime, *Registry, *Engine);
	JSContext* Context = static_cast<JSContext*>(Runtime->GetNativeContextHandle());
	Doc->InstallAsGlobal(); // globalThis.document

	// A parent __root (an EMPTY div under <html>) exposed as a global; real JS appends to it -> its child list is exactly the appends.
	FDomNode* Html = Doc->FindNode(Doc->GetRootNodeId());
	FJsValue RootW = Doc->CreateElement(TEXT("div"));
	Doc->NodeFromWrapper(RootW)->SetAttribute(TEXT("id"), TEXT("root"));
	Html->AppendChild(RootW);
	FDomNode* Root = Doc->NodeFromWrapper(RootW);

	JSValue Global = JS_GetGlobalObject(Context);
	JS_SetPropertyStr(Context, Global, "__root", JS_DupValue(Context, RootW.GetValue()));
	JS_FreeValue(Context, Global);

	// REAL JS mints two UNIQUE-tagged elements + appends them to __root IN ORDER.
	const FEvalResult R = Runtime->Evaluate(
		TEXT("globalThis.__a = document.createElement('cathode-probe-a');")
		TEXT("globalThis.__b = document.createElement('cathode-probe-b');")
		TEXT("__root.appendChild(globalThis.__a); __root.appendChild(globalThis.__b);"),
		TEXT("<create-append>"), EEvalKind::Global);
	TestFalse(TEXT("the create+append eval did not throw"), R.bThrew);

	// C++ recovers the JS-MINTED wrappers via the global channel (017's transit) -> NodeFromWrapper -> the C++ FDomNode.
	Global = JS_GetGlobalObject(Context);
	const FJsValue AW(Context, JS_GetPropertyStr(Context, Global, "__a"));
	const FJsValue BW(Context, JS_GetPropertyStr(Context, Global, "__b"));
	JS_FreeValue(Context, Global);
	FDomNode* const A = Doc->NodeFromWrapper(AW);
	FDomNode* const B = Doc->NodeFromWrapper(BW);

	// (1) THE JS-MINTED node's C++ IDENTITY — C++-direct GetTagName on the FDomNode (NOT a JS read-back). A no-op create -> null/wrong tag.
	if (A == nullptr || B == nullptr) { AddError(TEXT("a JS-minted node did not resolve to a C++ FDomNode")); return false; }
	TestEqual(TEXT("(create) the JS-minted __a's C++ FDomNode tag == 'cathode-probe-a' (unique; not echoable/pre-existing)"), A->GetTagName(), FString(TEXT("cathode-probe-a")));
	TestEqual(TEXT("(create) the JS-minted __b's C++ FDomNode tag == 'cathode-probe-b'"), B->GetTagName(), FString(TEXT("cathode-probe-b")));

	// (2) THE EXACT-ORDER PARENTING — C++-direct GetChildNodes on the parent FDomNode (the source of truth, NOT querySelector).
	// __root was created EMPTY -> after the two real-JS appends, its child-id list is EXACTLY [__a, __b] in order.
	TArray<FDomNodeId> RootChildren;
	Root->GetChildNodes(RootChildren);
	TestEqual(TEXT("(append) __root's C++ child count == 2 (the two JS-appended nodes)"), RootChildren.Num(), 2);
	if (RootChildren.Num() == 2)
	{
		TestEqual(TEXT("(append) __root's 1st C++ child == __a's NodeId (EXACT order, not [b,a])"), RootChildren[0], A->GetNodeId());
		TestEqual(TEXT("(append) __root's 2nd C++ child == __b's NodeId"), RootChildren[1], B->GetNodeId());
	}

	return true;
}

// 018 slice 2 — el.STYLE WRITE -> the C++ RETAINED NODE (C++-direct). Real JS (016 Evaluate) writes el.style via the bound
// CSSStyleDeclaration write-through setter (014; the WRITE is IN the Evaluate source -- NOT a C++ SetAttribute("style",...),
// which would be the masquerade [the other direction]); then C++ reads the node's STORED inline 'style' attribute DIRECTLY
// (NodeFromWrapper -> GetAttribute("style"), the canonical FDomNode attr the write-through stores on) -- NOT the JS
// getAttribute('style')/el.style read-back (the PRIMARY reject, indirect). The gap: the C++-stored inline style after a
// real-JS write (the setter + the real-JS drive exist [StyleTests:86]; only the C++-direct verify is unproven).
// DISCRIMINATOR: DISTINCT written values 'red' + '42px' (not default/empty) -> GetAttribute("style") CONTAINS them C++-side.
// A no-op/stub setter (a JS-side style object not writing through to the FDomNode attr) -> no 'style' attr / lacks the
// values -> FAILS. Contains on the distinct value substrings (robust to CSS serialization: style.color='red' stores 'red'
// as-is per StyleTests:86, NOT normalized to rgb; '42px' is a length, stored as-is).

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FCathodeDomJsStyleWriteTest,
	"Cathode.Dom.JsDriven.StyleWriteRetainedNode",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCathodeDomJsStyleWriteTest::RunTest(const FString& Parameters)
{
	using namespace Cathode;

	FScopedTestRuntime Runtime;
	if (!Runtime.IsValid()) { AddError(TEXT("runtime failed to start")); return false; }
	TUniquePtr<INativeClassRegistry> Registry = CreateNativeClassRegistry(*Runtime);
	FStubMeasurer Measurer;
	TUniquePtr<ILayoutEngine> Engine = CreateLiteHtmlLayoutEngine(Measurer);
	TUniquePtr<FDomDocument> Doc = CreateDomDocument(*Runtime, *Registry, *Engine);
	JSContext* Context = static_cast<JSContext*>(Runtime->GetNativeContextHandle());
	Doc->InstallAsGlobal(); // globalThis.document

	// REAL JS mints a node + writes el.style (the bound CSSStyleDeclaration write-through setter) — the WRITE is in the eval source.
	const FEvalResult R = Runtime->Evaluate(
		TEXT("globalThis.__el = document.createElement('cathode-styled');")
		TEXT("globalThis.__el.style.color = 'red'; globalThis.__el.style.width = '42px';"),
		TEXT("<style-write>"), EEvalKind::Global);
	TestFalse(TEXT("the style-write eval did not throw"), R.bThrew);

	// C++ recovers the JS-MINTED wrapper via the global channel (017's transit) -> NodeFromWrapper -> the C++ FDomNode.
	JSValue Global = JS_GetGlobalObject(Context);
	const FJsValue ElW(Context, JS_GetPropertyStr(Context, Global, "__el"));
	JS_FreeValue(Context, Global);
	FDomNode* const El = Doc->NodeFromWrapper(ElW);
	if (El == nullptr) { AddError(TEXT("the JS-minted styled node did not resolve to a C++ FDomNode")); return false; }

	// C++-DIRECT: the FDomNode's STORED inline 'style' attribute (the source of truth the 014 write-through stores on) — NOT the JS getAttribute.
	// A no-op/stub setter -> no 'style' attr (bHasStyle false) or lacks the values -> FAILS.
	FString StyleAttr;
	const bool bHasStyle = El->GetAttribute(TEXT("style"), StyleAttr);
	TestTrue(TEXT("(style write) the real-JS el.style write created a C++ 'style' attribute on the FDomNode (a no-op setter -> none)"), bHasStyle);
	TestTrue(FString::Printf(TEXT("(style write) the C++-stored style CONTAINS the distinct 'red' (the real-JS write reached the node; attr='%s')"), *StyleAttr), StyleAttr.Contains(TEXT("red")));
	TestTrue(FString::Printf(TEXT("(style write) the C++-stored style CONTAINS the distinct '42px' (attr='%s')"), *StyleAttr), StyleAttr.Contains(TEXT("42px")));

	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
