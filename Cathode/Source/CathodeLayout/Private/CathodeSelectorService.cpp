// The CSS-selector matcher service (sprint 012 selectors) — litehtml-backed PARSE + a live-tree structural MATCH.
//
// litehtml owns parsing ONLY (css_selector::parse — so the supported selector SET is litehtml's, never fabricated);
// the match is reimplemented over IMatchableNode (the LIVE retained DOM tree), reproducing html_tag::select's
// per-condition loop. NO litehtml document/container is built — the matcher is container-free + stateless. litehtml
// types appear ONLY in this .cpp (the swap-boundary discipline); the public CathodeSelector.h is neutral.

#include "CathodeSelector.h"

THIRD_PARTY_INCLUDES_START
// UE's check()/verify() macros (CoreMinimal) are function-like and collide with litehtml METHOD names
// (e.g. media_query::check) — push + undef across the litehtml include, then restore (mirrors CathodeLayoutEngine.cpp).
#pragma push_macro("check")
#pragma push_macro("verify")
#undef check
#undef verify
#include "litehtml.h"
#pragma pop_macro("verify")
#pragma pop_macro("check")
THIRD_PARTY_INCLUDES_END

namespace Cathode
{
namespace
{

// FString -> std::string (UTF-8). litehtml's parser lowercases names itself, so the raw selector is passed as-is.
litehtml::string ToUtf8(const FString& In)
{
	return litehtml::string(TCHAR_TO_UTF8(*In));
}

// Lowercased UTF-8 for interning a node's own tag/id/class tokens to match litehtml's parse-time lowercasing.
litehtml::string_id LowerId(const FString& In)
{
	return litehtml::_id(litehtml::string(TCHAR_TO_UTF8(*In.ToLower())));
}

// The predefined ':not' name id — interning "not" is idempotent with litehtml's own predefined id.
litehtml::string_id NotId()
{
	static const litehtml::string_id Id = litehtml::_id("not");
	return Id;
}

// --- Supported-set + well-formedness validation (run ONCE per parsed selector, before any matching) --------------
// A selector that PARSES but uses an UNSUPPORTED feature, OR is MALFORMED, is rejected (InvalidSelector -> a DOM
// SyntaxError), never silently mis-matched. Supported: type/`*`, `.class`, `#id`, `[attr]` (all operators),
// descendant ' ' + child '>', :not(simple).
//
// Well-formedness matters because litehtml's parser is LENIENT: ">>>" does NOT fail to parse — it absorbs the
// combinator soup into a bogus TAG (m_tag = _id(">"), m_attrs empty), which would otherwise just never-match
// (a silent wrong-accept). So every selector NAME (tag / class / id / attribute name) must be a valid identifier.

bool IsIdentName(const litehtml::string& Name)
{
	if (Name.empty())
	{
		return false;
	}
	for (const char Ch : Name)
	{
		const bool bIdent = (Ch >= 'a' && Ch <= 'z') || (Ch >= 'A' && Ch <= 'Z')
			|| (Ch >= '0' && Ch <= '9') || Ch == '-' || Ch == '_';
		if (!bIdent)
		{
			return false; // a selector-syntax / garbage char in a NAME -> malformed (e.g. the ">" tag from ">>>")
		}
	}
	return true;
}

bool IsSupportedSimple(const litehtml::css_element_selector& Sel)
{
	// The type selector: `*` (star) is the no-tag wildcard; any other tag must be a valid identifier (rejects ">").
	if (Sel.m_tag != litehtml::star_id && !IsIdentName(litehtml::_s(Sel.m_tag)))
	{
		return false;
	}
	for (const litehtml::css_attribute_selector& Attr : Sel.m_attrs)
	{
		switch (Attr.type)
		{
		case litehtml::select_class:
		case litehtml::select_id:
		case litehtml::select_exists:
		case litehtml::select_equal:
		case litehtml::select_contain_str:
		case litehtml::select_start_str:
		case litehtml::select_end_str:
			// structural class/id/attribute condition — supported; its NAME must be a valid identifier.
			if (!IsIdentName(litehtml::_s(Attr.name)))
			{
				return false;
			}
			break;
		case litehtml::select_pseudo_class:
			// ONLY :not(simple) is supported among pseudo-classes; validate its nested simple selector too.
			if (Attr.name != NotId() || !Attr.sel || !IsSupportedSimple(*Attr.sel))
			{
				return false;
			}
			break;
		default: // select_pseudo_element + anything else
			return false;
		}
	}
	return true;
}

bool IsSupportedChain(const litehtml::css_selector& Sel)
{
	if (!IsSupportedSimple(Sel.m_right))
	{
		return false;
	}
	if (Sel.m_left)
	{
		// Only descendant + child combinators are supported (no adjacent/general sibling).
		if (Sel.m_combinator != litehtml::combinator_descendant && Sel.m_combinator != litehtml::combinator_child)
		{
			return false;
		}
		return IsSupportedChain(*Sel.m_left);
	}
	return true;
}

// --- The match primitive (assumes the selector is validated-supported) -------------------------------------------

bool ClassListContains(const FString& ClassAttr, litehtml::string_id WantedClassId)
{
	TArray<FString> Tokens;
	ClassAttr.ParseIntoArrayWS(Tokens); // whitespace-split the class attribute
	for (const FString& Token : Tokens)
	{
		if (LowerId(Token) == WantedClassId)
		{
			return true;
		}
	}
	return false;
}

bool AttributeConditionMatches(const litehtml::css_attribute_selector& Attr, bool bHasValue, const FString& Value)
{
	if (!bHasValue)
	{
		return false; // every attribute condition requires the attribute to be present
	}
	const FString Operand = FString(UTF8_TO_TCHAR(Attr.val.c_str())); // litehtml stores the operand value as-authored
	switch (Attr.type)
	{
	case litehtml::select_exists:      return true;
	case litehtml::select_equal:       return Value.Equals(Operand, ESearchCase::CaseSensitive);
	case litehtml::select_contain_str: return !Operand.IsEmpty() && Value.Contains(Operand, ESearchCase::CaseSensitive); // ~= / *= (litehtml: substring)
	case litehtml::select_start_str:   return Value.StartsWith(Operand, ESearchCase::CaseSensitive);                     // |= / ^= (litehtml: prefix)
	case litehtml::select_end_str:     return Value.EndsWith(Operand, ESearchCase::CaseSensitive);                       // $=
	default:                           return false;
	}
}

// Does Node match a single simple selector (reproduces html_tag::select's per-condition loop over our accessors).
bool MatchSimple(const IMatchableNode& Node, const litehtml::css_element_selector& Sel)
{
	// tag: `*` (star_id) matches any; else the element's lowercased tag id must equal the selector's.
	if (Sel.m_tag != litehtml::star_id)
	{
		const FString Tag = Node.GetMatchTagName();
		if (Tag.IsEmpty() || LowerId(Tag) != Sel.m_tag)
		{
			return false;
		}
	}
	for (const litehtml::css_attribute_selector& Attr : Sel.m_attrs)
	{
		switch (Attr.type)
		{
		case litehtml::select_class:
		{
			FString ClassAttr;
			if (!Node.GetMatchAttribute(TEXT("class"), ClassAttr) || !ClassListContains(ClassAttr, Attr.name))
			{
				return false;
			}
			break;
		}
		case litehtml::select_id:
		{
			FString IdAttr;
			if (!Node.GetMatchAttribute(TEXT("id"), IdAttr) || LowerId(IdAttr) != Attr.name)
			{
				return false;
			}
			break;
		}
		case litehtml::select_pseudo_class: // validated to be ONLY :not(simple)
		{
			if (MatchSimple(Node, *Attr.sel))
			{
				return false; // :not matched -> this simple selector fails
			}
			break;
		}
		default: // an attribute condition (exists/equal/contain/start/end)
		{
			const FString AttrName = FString(UTF8_TO_TCHAR(litehtml::_s(Attr.name).c_str()));
			FString Value;
			const bool bHas = Node.GetMatchAttribute(AttrName, Value);
			if (!AttributeConditionMatches(Attr, bHas, Value))
			{
				return false;
			}
			break;
		}
		}
	}
	return true;
}

// Does Node match the full selector chain — match m_right against Node, then walk the combinator to m_left.
bool MatchChain(const IMatchableNode& Node, const litehtml::css_selector& Sel)
{
	if (!MatchSimple(Node, Sel.m_right))
	{
		return false;
	}
	if (!Sel.m_left)
	{
		return true;
	}
	if (Sel.m_combinator == litehtml::combinator_child)
	{
		const IMatchableNode* Parent = Node.GetMatchParent();
		return Parent != nullptr && MatchChain(*Parent, *Sel.m_left);
	}
	// descendant: some ancestor matches m_left (ancestors may sit ABOVE the query scope — correct per spec).
	for (const IMatchableNode* Ancestor = Node.GetMatchParent(); Ancestor != nullptr; Ancestor = Ancestor->GetMatchParent())
	{
		if (MatchChain(*Ancestor, *Sel.m_left))
		{
			return true;
		}
	}
	return false;
}

// Pre-order traversal collecting matches. bFirstOnly stops at the first. Root tested iff bIncludeRoot.
void Collect(const IMatchableNode& Node, const litehtml::css_selector& Sel, bool bFirstOnly,
	TArray<const IMatchableNode*>& Out)
{
	if (MatchChain(Node, Sel))
	{
		Out.Add(&Node);
		if (bFirstOnly)
		{
			return;
		}
	}
	TArray<const IMatchableNode*> Children;
	Node.GetMatchChildren(Children);
	for (const IMatchableNode* Child : Children)
	{
		if (Child == nullptr)
		{
			continue;
		}
		Collect(*Child, Sel, bFirstOnly, Out);
		if (bFirstOnly && Out.Num() > 0)
		{
			return;
		}
	}
}

// The production matcher: parse (litehtml) + validate-supported + live-tree walk.
class FSelectorService final : public ISelectorService
{
public:
	virtual ESelectorStatus QueryFirst(const IMatchableNode& Root, bool bIncludeRoot, const FString& Selector,
		const IMatchableNode*& OutMatch) const override
	{
		OutMatch = nullptr;
		litehtml::css_selector Parsed;
		if (!ParseAndValidate(Selector, Parsed))
		{
			return ESelectorStatus::InvalidSelector;
		}
		TArray<const IMatchableNode*> Matches;
		WalkScope(Root, bIncludeRoot, Parsed, /*bFirstOnly*/ true, Matches);
		if (Matches.Num() > 0)
		{
			OutMatch = Matches[0];
			return ESelectorStatus::Matched;
		}
		return ESelectorStatus::NoMatch;
	}

	// querySelectorAll / matches / closest fold in here over the SAME ParseAndValidate + MatchChain primitive when
	// the contract confirms the trio (compose-not-duplicate) — held out now to avoid unbound surface.

private:
	static bool ParseAndValidate(const FString& Selector, litehtml::css_selector& OutParsed)
	{
		if (!OutParsed.parse(ToUtf8(Selector)))
		{
			return false; // structurally unparseable
		}
		return IsSupportedChain(OutParsed); // parseable but must be within the declared supported set
	}

	static void WalkScope(const IMatchableNode& Root, bool bIncludeRoot, const litehtml::css_selector& Sel,
		bool bFirstOnly, TArray<const IMatchableNode*>& Out)
	{
		if (bIncludeRoot)
		{
			Collect(Root, Sel, bFirstOnly, Out);
			return;
		}
		// EXCLUDE-SELF: descendants only (element.querySelector never returns the element itself).
		TArray<const IMatchableNode*> Children;
		Root.GetMatchChildren(Children);
		for (const IMatchableNode* Child : Children)
		{
			if (Child == nullptr)
			{
				continue;
			}
			Collect(*Child, Sel, bFirstOnly, Out);
			if (bFirstOnly && Out.Num() > 0)
			{
				return;
			}
		}
	}
};

} // namespace

TUniquePtr<ISelectorService> CreateSelectorService()
{
	return MakeUnique<FSelectorService>();
}

} // namespace Cathode
