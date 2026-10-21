// Native check that every pattern in src/HookSites.h is well-formed.

#include "HookSites.h"

#include <cctype>
#include <cstdio>

static bool PatternOk(std::string_view a_p)
{
	if (a_p.empty())
		return true;
	std::size_t i = 0;
	while (i < a_p.size()) {
		if (i + 2 > a_p.size())
			return false;
		const char a = a_p[i], b = a_p[i + 1];
		const bool wild = a == '?' && b == '?';
		const bool hex = std::isxdigit(static_cast<unsigned char>(a)) && std::isxdigit(static_cast<unsigned char>(b));
		if (!wild && !hex)
			return false;
		i += 2;
		if (i < a_p.size()) {
			if (a_p[i] != ' ')
				return false;
			++i;
		}
	}
	return true;
}

int main()
{
	int failures = 0;
	for (const auto* site : BM::HookSites::kAllSites) {
		for (auto p : { site->patternSE, site->patternAE }) {
			if (!PatternOk(p)) {
				std::printf("FAIL %.*s: bad pattern \"%.*s\"\n",
					static_cast<int>(site->name.size()), site->name.data(),
					static_cast<int>(p.size()), p.data());
				++failures;
			}
		}
	}
	if (failures == 0)
		std::printf("All hook-site patterns well-formed (%zu sites).\n", BM::HookSites::kAllSites.size());
	return failures == 0 ? 0 : 1;
}
