#pragma once

#include "CoreMinimal.h"

#include "CathodeResolver.h" // IResourceResolver (005 -> CathodeResolver dep — the blessed evolution)

namespace Cathode
{

// 005 document_container -> Resource Resolver routing, litehtml-FREE (the container converts litehtml::string
// <-> FString at the thin vendor edge, then calls these — exactly the CathodePaintRouting pattern, so the
// routing is testable without litehtml). A NULL resolver yields the not-found result, == 005's pre-hook stub
// behavior (no regression). The resolver provides BYTES only (RQ1: no decode) — get_image_size's dimensions
// are a 003-decode at integration, so RouteImageSize returns 0x0 this sprint.

bool RouteImageBytes(const IResourceResolver* Resolver, const FString& Src, const FString& BaseUrl, TArray<uint8>& OutBytes);
bool RouteLinkedCss(const IResourceResolver* Resolver, const FString& Url, const FString& BaseUrl, FString& OutCss);
void RouteImageSize(const IResourceResolver* Resolver, const FString& Src, const FString& BaseUrl, int32& OutWidth, int32& OutHeight);

} // namespace Cathode
