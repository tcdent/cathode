#pragma once

#include "CoreMinimal.h"

namespace Cathode
{
struct FBoundMember;

// 025 S1 — codegen accessors: expose the PRODUCTION FBoundMember binding tables (static + file-local in their owning
// TUs) so GenerateAuthoringDts projects the SAME tables InstallBoundMembers registers (Model A — the .d.ts == the
// registered surface by construction). Defined alongside each table (CathodeDomNode.cpp / CathodeDomDocument.cpp).
void GetDomNodeBoundMembersForCodegen(const FBoundMember*& OutMembers, int32& OutCount);
void GetDomDocumentBoundMembersForCodegen(const FBoundMember*& OutMembers, int32& OutCount);
void GetDomEventBoundMembersForCodegen(const FBoundMember*& OutMembers, int32& OutCount);
void GetDomKeyboardEventBoundMembersForCodegen(const FBoundMember*& OutMembers, int32& OutCount);

} // namespace Cathode
