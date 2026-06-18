#if WITH_DEV_AUTOMATION_TESTS

#include "CathodeJsRuntime.h"
#include "CathodeRuntimeTesting.h"

#include "Misc/AutomationTest.h"

// 016 slice 1 — real Evaluate (JS_Eval, Global) against the REAL engine (CreateJsRuntime, NOT the recording mock).
// FEvalResult carries NO value, so every proof is THROW-ON-SUCCESS: bThrew==TRUE carrying a read-back value the engine
// COMPUTED (a bThrew==false assertion is VACUOUS — the stub returns bThrew=false for everything, including the check eval).
// The COMPUTED-throw "42" (String(6*7); 42 is NOT a substring of the source) is the key proof + the fresh-binary marker:
// a stale STUB binary returns bThrew=false -> the assert FAILS (RED); green => the real engine evaluated it.
//
// MESSAGE FORM (grounded): TakePendingError reads JS_ToCString(exception) = the Error's toString ("Error: <message>"),
// NOT the bare .message — so the proofs assert Message.Contains(<computed value>), which still bites the stub (empty) AND
// an echo (carries the SOURCE, e.g. "String(6*7)", never "42"). Flagged to the lead; TakePendingError is reused as-is.

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FCathodeRuntimeEvalTest,
	"Cathode.RuntimeCore.Eval.RealEvaluate",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCathodeRuntimeEvalTest::RunTest(const FString& Parameters)
{
	using namespace Cathode;

	// FScopedTestRuntime (the established real-engine idiom, as ContractGlobals/Dom/Binding use): on ctor releases the
	// module's auto-started singleton + creates a test-owned runtime; on dtor tears it down (releases the single-instance
	// claim). Self-isolating — leaves no runtime alive for a same-filter neighbor (a raw CreateJsRuntime did, colliding
	// with ContractGlobals' CreateJsRuntime under a narrow filter).
	FScopedTestRuntime Runtime;
	if (!Runtime.IsValid() || !Runtime->IsValid()) { AddError(TEXT("runtime failed to start")); return false; }

	auto Eval = [&](const TCHAR* Source) -> FEvalResult
	{
		return Runtime->Evaluate(Source, TEXT("<eval-test>"), EEvalKind::Global);
	};

	// (1) plain throw -> bThrew==true + the message carries 'boom'. (A stub returns bThrew=false -> this bites it.)
	{
		const FEvalResult R = Eval(TEXT("throw new Error('boom')"));
		TestTrue(TEXT("(1) plain throw -> bThrew==true"), R.bThrew);
		TestTrue(TEXT("(1) the error message carries 'boom'"), R.Error.Message.Contains(TEXT("boom")));
	}

	// (2) COMPUTED throw -> bThrew==true + Message carries "42". 42 is NOT a substring of the source ("String(6*7)"), so
	// it bites the STUB (bThrew=false) AND an ECHO (would carry the source, not "42"). THE coherence / fresh-binary marker.
	{
		const FEvalResult R = Eval(TEXT("throw new Error(String(6*7))"));
		TestTrue(TEXT("(2) computed throw -> bThrew==true"), R.bThrew);
		TestTrue(TEXT("(2) the engine computed 6*7 -> message carries '42' (not an echo of the source)"), R.Error.Message.Contains(TEXT("42")));
	}

	// (3) syntax error -> bThrew==true (the PARSE path, distinct from a runtime throw).
	{
		const FEvalResult R = Eval(TEXT("}{"));
		TestTrue(TEXT("(3) syntax error -> bThrew==true (parse path)"), R.bThrew);
	}

	// (4) INVERTED cross-eval side effect: eval1 sets a global (setup; its bThrew=false is NOT the discriminator); eval2
	// throws IFF the side effect persisted -> bThrew==true + the read-back value. Proves eval1 EXECUTED + state persisted
	// across evals on the SHARED context. (A stub: eval2 -> bThrew=false -> the bThrew==true assert FAILS.)
	{
		Eval(TEXT("globalThis.__x = 7")); // setup
		const FEvalResult R = Eval(TEXT("if (globalThis.__x === 7) throw new Error('took:' + globalThis.__x)"));
		TestTrue(TEXT("(4) side effect persisted across evals -> bThrew==true"), R.bThrew);
		TestTrue(TEXT("(4) the read-back value -> message carries 'took:7'"), R.Error.Message.Contains(TEXT("took:7")));
	}

	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
