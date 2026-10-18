// data::sprite URI parsing - see SpriteUri.h.
#include "SpriteUri.h"

#include <cstdio>

namespace sprites {

bool splitCropUri(const std::string& uri, std::size_t bodyStart, std::string& atlasFile, int rect[4]) {
	atlasFile.clear();
	std::size_t slash = uri.rfind('/');
	while (slash != std::string::npos && slash >= bodyStart) {
		int left = 0, top = 0, right = 0, bottom = 0;
		if (sscanf_s(uri.c_str() + slash + 1, "%d,%d,%d,%d", &left, &top, &right, &bottom) == 4) {
			atlasFile = uri.substr(bodyStart, slash - bodyStart);
			if (atlasFile.empty())
				return false;
			rect[0] = left;
			rect[1] = top;
			rect[2] = right;
			rect[3] = bottom;
			return true;
		}
		if (slash == 0)
			break;
		slash = uri.rfind('/', slash - 1);
	}
	return false;
}

}
