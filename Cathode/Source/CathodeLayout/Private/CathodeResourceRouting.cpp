#include "CathodeResourceRouting.h"

namespace Cathode
{

bool RouteImageBytes(const IResourceResolver* Resolver, const FString& Src, const FString& BaseUrl, TArray<uint8>& OutBytes)
{
	// load_image: resolve the image bytes (available for the 003 draw at integration). Null resolver -> miss.
	return Resolver != nullptr && Resolver->ResolveBytes(Src, BaseUrl, OutBytes);
}

bool RouteLinkedCss(const IResourceResolver* Resolver, const FString& Url, const FString& BaseUrl, FString& OutCss)
{
	// import_css: resolve the linked stylesheet text (the container hands it to litehtml to parse). Null -> miss.
	return Resolver != nullptr && Resolver->ResolveText(Url, BaseUrl, OutCss);
}

void RouteImageSize(const IResourceResolver* Resolver, const FString& Src, const FString& BaseUrl, int32& OutWidth, int32& OutHeight)
{
	// get_image_size: the hook is wired through here (it ROUTES to the resolver), but the DIMENSIONS need
	// decoding the bytes (003) -> integration (RQ1: the resolver has no size). So the size stays 0x0 this sprint
	// (== 005's pre-hook stub), now produced via the routing layer rather than hardcoded. NOTE: this resolves
	// then DISCARDS the bytes and does NOT gate on hit/miss — a forward-wiring placeholder; the integration
	// tidy is to fold this into the 003 decode-for-size path (decode the resolved bytes -> real dimensions).
	TArray<uint8> Bytes;
	(void)(Resolver != nullptr && Resolver->ResolveBytes(Src, BaseUrl, Bytes));
	OutWidth = 0;
	OutHeight = 0;
}

} // namespace Cathode
