#if WITH_DEV_AUTOMATION_TESTS

#include "CathodeDom.h"
#include "CathodeJsRuntime.h"
#include "CathodeRuntimeTesting.h"

#include "Misc/AutomationTest.h"

THIRD_PARTY_INCLUDES_START
#include "quickjs.h"
THIRD_PARTY_INCLUDES_END

// Snapshot-returns — the OBJECT-RETURN DOM surface, JS-AUTHORED-proto types minted in C++ (the NodeList
// pattern; NOT table-bound, so NOT lockstep cases — verified by instanceof + spec surface + NO-superset):
//   - Element.attributes -> NamedNodeMap SNAPSHOT of MINIMAL { name, value } items (no Attr class).
//   - Element.getBoundingClientRect() -> DOMRect VALUE from the REAL layout box (proven against the real captured box).

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

// For the DOMRect proof: the REAL litehtml layout engine needs a text measurer (a stub measurer is fine — the box
// geometry comes from the inline width/height styles, not text).
class FStubMeasurer final : public ITextMeasurer
{
public:
	virtual FLayoutFontRef ResolveFont(const FString&, int32, int32, bool) override { return FLayoutFontRef(); }
	virtual float MeasureWidth(const FString&, FLayoutFontRef) override { return 0.0f; }
	virtual void GetFontMetrics(FLayoutFontRef, float& OutAscent, float& OutDescent, float& OutHeight) override
	{
		OutAscent = 12.0f; OutDescent = 4.0f; OutHeight = 16.0f;
	}
};

JSValue Eval(JSContext* Context, const char* Source)
{
	return JS_Eval(Context, Source, FCStringAnsi::Strlen(Source), "<cathode-objreturns-test>", JS_EVAL_TYPE_GLOBAL);
}
} // namespace

// ===== NamedNodeMap (Element.attributes) — minimal { name, value } items, snapshot, no-superset, Preact-faithful =====
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FCathodeDomNamedNodeMapTest,
	"Cathode.Dom.ObjectReturns.NamedNodeMap",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCathodeDomNamedNodeMapTest::RunTest(const FString& Parameters)
{
	using namespace Cathode;

	Cathode::FScopedTestRuntime RuntimePtr;
	if (!RuntimePtr.IsValid()) { AddError(TEXT("Cathode test runtime failed to initialize")); return false; }
	TUniquePtr<INativeClassRegistry> Registry = CreateNativeClassRegistry(*RuntimePtr);
	FStubLayoutEngine Layout;
	TUniquePtr<FDomDocument> Doc = CreateDomDocument(*RuntimePtr, *Registry, Layout);
	JSContext* Context = static_cast<JSContext*>(RuntimePtr->GetNativeContextHandle());

	FJsValue ElW = Doc->CreateElement(TEXT("div"));
	FDomNode* El = Doc->NodeFromWrapper(ElW);
	El->SetAttribute(TEXT("id"), TEXT("hero"));
	El->SetAttribute(TEXT("class"), TEXT("card big"));
	El->SetAttribute(TEXT("data-x"), TEXT("42"));

	JSValue Global = JS_GetGlobalObject(Context);
	JS_SetPropertyStr(Context, Global, "__el", JS_DupValue(Context, ElW.GetValue()));
	JS_FreeValue(Context, Global);

	auto EvalBool = [&](const char* Expr) -> bool
	{
		JSValue R = Eval(Context, Expr);
		if (JS_IsException(R)) { JS_FreeValue(Context, R); AddError(FString::Printf(TEXT("JS threw: %s"), ANSI_TO_TCHAR(Expr))); return false; }
		bool bValue = false; const bool bOk = UnmarshalBool(Context, R, bValue);
		JS_FreeValue(Context, R);
		return bOk && bValue;
	};

	// instanceof + the spec SURFACE (the JS-proto type, not table-bound).
	TestTrue(TEXT("attributes instanceof NamedNodeMap"), EvalBool("__el.attributes instanceof NamedNodeMap"));
	TestTrue(TEXT("length === 3 (the node's real attributes)"), EvalBool("__el.attributes.length === 3"));

	// items in deterministic (sorted-key) order: class, data-x, id — each a MINIMAL { name, value }.
	TestTrue(TEXT("[i].name/.value are the node's real attributes, sorted-key order"),
		EvalBool("(function(){ var a=__el.attributes; return a[0].name==='class' && a[0].value==='card big' && a[1].name==='data-x' && a[1].value==='42' && a[2].name==='id' && a[2].value==='hero'; })()"));

	// MINIMAL-ITEM exact subset: name + value ONLY — NO localName/namespaceURI/ownerElement, NO Attr methods.
	TestTrue(TEXT("item is the EXACT subset { name, value } — no Attr-class members"),
		EvalBool("(function(){ var a=__el.attributes[0]; return a.name!==undefined && a.value!==undefined && a.localName===undefined && a.namespaceURI===undefined && a.ownerElement===undefined && a.appendChild===undefined; })()"));

	// NO array/object SUPERSET (the NodeList no-superset discipline — map/filter/push must be undefined).
	TestTrue(TEXT("NamedNodeMap has NO array superset (map/filter/push undefined)"),
		EvalBool("__el.attributes.map===undefined && __el.attributes.filter===undefined && __el.attributes.push===undefined"));

	// item(i) / getNamedItem(name). NB: each `.attributes` access mints a FRESH snapshot (correct snapshot
	// semantics, like childNodes), so item(i) === [i] must compare on the SAME captured map, not across two mints.
	TestTrue(TEXT("item(1) === [1] (same captured map); item(99) -> null"),
		EvalBool("(function(){ var a=__el.attributes; return a.item(1) === a[1] && a.item(99) === null; })()"));
	TestTrue(TEXT("getNamedItem('id').value === 'hero'; getNamedItem('nope') === null"),
		EvalBool("__el.attributes.getNamedItem('id').value === 'hero' && __el.attributes.getNamedItem('nope') === null"));

	// iterator.
	TestTrue(TEXT("iterable: [...attributes] yields the items in order"),
		EvalBool("(function(){ var names=[]; for (var a of __el.attributes) { names.push(a.name); } return names.length===3 && names[0]==='class' && names[2]==='id'; })()"));

	// CONSUMER ORACLE: Preact's EXACT use — build name->value off length + [i].name/.value.
	TestTrue(TEXT("Preact-exact: for(i<length) x[attributes[i].name]=attributes[i].value"),
		EvalBool("(function(){ var x={}; for(var i=0;i<__el.attributes.length;i++){ x[__el.attributes[i].name]=__el.attributes[i].value; } return x.id==='hero' && x['data-x']==='42' && x['class']==='card big'; })()"));

	// SNAPSHOT: a CAPTURED NamedNodeMap is unchanged by a later setAttribute; a re-read reflects it.
	TestTrue(TEXT("SNAPSHOT: captured NamedNodeMap unchanged after a later setAttribute; re-read grows"),
		EvalBool("(function(){ var cap=__el.attributes; var before=cap.length; __el.setAttribute('data-y','9'); var stillCap=cap.length; var reread=__el.attributes.length; return before===3 && stillCap===3 && reread===4; })()"));

	return true;
}

// ===== DOMRect (Element.getBoundingClientRect()) — VALUE from the REAL layout box (no stub, no hand-typed pixel) =====
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FCathodeDomBoundingClientRectTest,
	"Cathode.Dom.ObjectReturns.BoundingClientRect",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCathodeDomBoundingClientRectTest::RunTest(const FString& Parameters)
{
	using namespace Cathode;

	Cathode::FScopedTestRuntime RuntimePtr;
	if (!RuntimePtr.IsValid()) { AddError(TEXT("Cathode test runtime failed to initialize")); return false; }
	TUniquePtr<INativeClassRegistry> Registry = CreateNativeClassRegistry(*RuntimePtr);

	// The REAL litehtml layout engine — NOT the stub (a stub yields zero boxes; the rect must wrap a REAL computed box).
	FStubMeasurer Measurer;
	TUniquePtr<ILayoutEngine> RealEngine = CreateLiteHtmlLayoutEngine(Measurer);
	TUniquePtr<FDomDocument> Doc = CreateDomDocument(*RuntimePtr, *Registry, *RealEngine);
	JSContext* Context = static_cast<JSContext*>(RuntimePtr->GetNativeContextHandle());

	// A real sized tree: container(200px) > a(50x30) — inline styles drive litehtml's computed boxes.
	FJsValue ContainerW = Doc->CreateElement(TEXT("div"));
	FDomNode* Container = Doc->NodeFromWrapper(ContainerW);
	Container->SetAttribute(TEXT("style"), TEXT("width:200px"));
	Doc->FindNode(Doc->GetRootNode())->AppendChild(ContainerW);
	FJsValue AW = Doc->CreateElement(TEXT("div"));
	FDomNode* A = Doc->NodeFromWrapper(AW);
	A->SetAttribute(TEXT("style"), TEXT("width:50px;height:30px"));
	Container->AppendChild(AW);
	FJsValue OrphanW = Doc->CreateElement(TEXT("div")); // created, NEVER appended -> no laid-out geometry

	Doc->Relayout(800); // drive REAL litehtml layout + cache (the geometry source for getBoundingClientRect)

	// The REAL captured box (the oracle — NOT a hand-typed pixel). Computed the SAME way MarshalDOMRect does.
	FBox2f BoxA(ForceInit);
	const bool bHasA = A->GetBoundingClientRect(BoxA);
	TestTrue(TEXT("A has real laid-out geometry after the real Relayout"), bHasA);
	const double RealX = BoxA.Min.X;
	const double RealY = BoxA.Min.Y;
	const double RealW = static_cast<double>(BoxA.Max.X) - BoxA.Min.X;
	const double RealH = static_cast<double>(BoxA.Max.Y) - BoxA.Min.Y;

	JSValue Global = JS_GetGlobalObject(Context);
	JS_SetPropertyStr(Context, Global, "__a", JS_DupValue(Context, AW.GetValue()));
	JS_SetPropertyStr(Context, Global, "__container", JS_DupValue(Context, ContainerW.GetValue()));
	JS_SetPropertyStr(Context, Global, "__orphan", JS_DupValue(Context, OrphanW.GetValue()));
	// Inject the REAL box values as the comparison oracle (real layout output, not hand-typed).
	JS_SetPropertyStr(Context, Global, "__realX", JS_NewFloat64(Context, RealX));
	JS_SetPropertyStr(Context, Global, "__realY", JS_NewFloat64(Context, RealY));
	JS_SetPropertyStr(Context, Global, "__realW", JS_NewFloat64(Context, RealW));
	JS_SetPropertyStr(Context, Global, "__realH", JS_NewFloat64(Context, RealH));
	JS_FreeValue(Context, Global);

	auto EvalBool = [&](const char* Expr) -> bool
	{
		JSValue R = Eval(Context, Expr);
		if (JS_IsException(R)) { JS_FreeValue(Context, R); AddError(FString::Printf(TEXT("JS threw: %s"), ANSI_TO_TCHAR(Expr))); return false; }
		bool bValue = false; const bool bOk = UnmarshalBool(Context, R, bValue);
		JS_FreeValue(Context, R);
		return bOk && bValue;
	};

	TestTrue(TEXT("getBoundingClientRect() instanceof DOMRect"), EvalBool("__a.getBoundingClientRect() instanceof DOMRect"));

	// REAL geometry (relational, non-zero — a stub box would be all-zeros).
	TestTrue(TEXT("real geometry: width>0 && height>0 (real layout, not a stub zero)"),
		EvalBool("(function(){ var r=__a.getBoundingClientRect(); return r.width>0 && r.height>0; })()"));

	// CAPTURED-REFERENCE: the JS DOMRect wraps the REAL C++ box (NOT a hand-typed pixel), within fp epsilon.
	TestTrue(TEXT("JS DOMRect === the real C++ layout box (x/y/width/height)"),
		EvalBool("(function(){ var r=__a.getBoundingClientRect(); var eps=0.01;"
			" return Math.abs(r.x-__realX)<eps && Math.abs(r.y-__realY)<eps && Math.abs(r.width-__realW)<eps && Math.abs(r.height-__realH)<eps; })()"));

	// THE 8-PROP IDENTITY: top/left/right/bottom DERIVE from x/y/width/height (a mint filling them independently would diverge).
	TestTrue(TEXT("DOMRect identity: right===x+width, bottom===y+height, top===y, left===x"),
		EvalBool("(function(){ var r=__a.getBoundingClientRect(); return r.right===r.x+r.width && r.bottom===r.y+r.height && r.top===r.y && r.left===r.x; })()"));

	// RELATIONAL real-layout invariant: a nests within the container's x-extent (real composed geometry).
	TestTrue(TEXT("relational: a's rect nests within the container's x-extent"),
		EvalBool("(function(){ var a=__a.getBoundingClientRect(), c=__container.getBoundingClientRect(); return a.left>=c.left && a.right<=c.right && c.width>=a.width; })()"));

	// UNRENDERED / no laid-out geometry -> ALL-ZEROS DOMRect (spec), never null.
	TestTrue(TEXT("unrendered element -> all-zeros DOMRect"),
		EvalBool("(function(){ var r=__orphan.getBoundingClientRect(); return (r instanceof DOMRect) && r.x===0 && r.y===0 && r.width===0 && r.height===0 && r.right===0 && r.bottom===0; })()"));

	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
