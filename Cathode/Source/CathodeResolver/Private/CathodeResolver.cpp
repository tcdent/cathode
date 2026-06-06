#include "CathodeResolver.h"

#include "Containers/StringConv.h"

namespace Cathode
{

namespace
{
// The directory portion of a base URL: everything up to and including the last '/'. "" if none.
FString BaseDir(const FString& BaseUrl)
{
	int32 Slash = INDEX_NONE;
	if (BaseUrl.FindLastChar(TEXT('/'), Slash))
	{
		return BaseUrl.Left(Slash + 1);
	}
	return FString();
}

// Collapse "." / ".." segments of a '/'-separated path into a clean root-relative path (no leading '/'). A
// ".." that would escape the root is clamped at the root (trusted-only — no traversal escape).
FString CollapseSegments(const FString& Path)
{
	TArray<FString> In;
	Path.ParseIntoArray(In, TEXT("/"), /*CullEmpty=*/true);
	TArray<FString> Out;
	for (const FString& Segment : In)
	{
		if (Segment == TEXT("."))
		{
			continue;
		}
		if (Segment == TEXT(".."))
		{
			if (Out.Num() > 0)
			{
				Out.Pop();
			}
			continue; // clamp at root if it would escape
		}
		Out.Add(Segment);
	}
	return FString::Join(Out, TEXT("/"));
}
} // namespace

FString ResolveUrlPath(const FString& Url, const FString& BaseUrl)
{
	// '/'-rooted -> the virtual root (ignore the base); relative -> against the base's directory. Then collapse
	// ./ and ../ and strip any leading '/'. No custom scheme — ordinary web URLs.
	FString Joined;
	if (Url.StartsWith(TEXT("/")))
	{
		Joined = Url; // absolute against the virtual root
	}
	else
	{
		Joined = BaseDir(BaseUrl) + Url; // relative against the base's directory
	}
	return CollapseSegments(Joined); // root-relative, no leading '/'
}

namespace
{
// The resolver: bespoke URL normalization over a neutral IBundleSource. Private to this TU; only the factory
// is exposed. Couples-to-none (no vendor, no sibling).
class FResourceResolver final : public IResourceResolver
{
public:
	explicit FResourceResolver(IBundleSource& InSource)
		: Source(InSource)
	{
	}

	virtual bool ResolveBytes(const FString& Url, const FString& BaseUrl, TArray<uint8>& OutBytes) const override
	{
		const FString BundlePath = ResolveUrlPath(Url, BaseUrl);
		return Source.Read(BundlePath, OutBytes); // false on a clean miss
	}

	virtual bool ResolveText(const FString& Url, const FString& BaseUrl, FString& OutText) const override
	{
		TArray<uint8> Bytes;
		if (!ResolveBytes(Url, BaseUrl, Bytes))
		{
			return false;
		}
		// Decode bundle bytes as UTF-8 (NUL-terminate the view for the converter).
		Bytes.Add(0);
		OutText = FString(UTF8_TO_TCHAR(reinterpret_cast<const char*>(Bytes.GetData())));
		return true;
	}

private:
	IBundleSource& Source;
};
} // namespace

TUniquePtr<IResourceResolver> CreateResourceResolver(IBundleSource& Source)
{
	return MakeUnique<FResourceResolver>(Source);
}

} // namespace Cathode
