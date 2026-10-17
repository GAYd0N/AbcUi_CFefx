// framework::Localization implementation - see Localization.h.
#include "Localization.h"
#include "ResourceFile.h"
#include "TextUtil.h"

#include <abc/Console.h>

#include "../rmlui/VGUI2ExtensionImport.h"

#include <cstring>
#include <unordered_map>
#include <vector>

namespace framework {

namespace {
std::unordered_map<std::string, std::string> Table;

// "key = value" lines; '#' starts a comment. Values keep interior spaces.
void parseTableFile(const std::string& path) {
	std::vector<unsigned char> bytes;
	if (!ResourceFile::read(path, bytes, 1024 * 1024))
		return;

	std::string line;
	auto parseLine = [&]() { std::string key, value; if (TextUtil::parseTranslationLine(line,key,value)) Table[key]=value; line.clear(); };
	for (const unsigned char ch : bytes) {
		if (ch == '\n')
			parseLine();
		else if (line.size() < 65536)
			line.push_back(static_cast<char>(ch));
	}
	parseLine();
}
} // namespace

void Localization::load() {
	Table.clear();

	const char* language = vgui2ext::currentLanguage();
	if (!language || !*language)
		language = "english";

	// Sven Co-op reports "schinese"; keep a translation-free alias chain so a
	// missing language file degrades to english instead of an empty UI.
	std::string path = "abc/abcui/lang/";
	path += language;
	path += ".txt";
	parseTableFile("abc/abcui/lang/english.txt");
	if (strcmp(language, "english") != 0) parseTableFile(path);
	if (Table.empty() && strcmp(language, "english") != 0) {
		abc::ConsoleWarn("[AbcUi] no localization for \"%s\", falling back to english\n", language);
		parseTableFile("abc/abcui/lang/english.txt");
	}
}

bool Localization::translate(std::string& out, const std::string& token) {
	const auto itor = Table.find(token);
	if (itor == Table.end())
		return false;
	out = itor->second;
	return true;
}

}
