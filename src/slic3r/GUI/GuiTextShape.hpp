#ifndef slic3r_Text_Shape_hpp_
#define slic3r_Text_Shape_hpp_

#include "libslic3r/TriangleMesh.hpp"
#include <libslic3r/Emboss.hpp>
#include <libslic3r/EmbossShape.hpp>
struct stbtt_fontinfo;
using fontinfo_opt = std::optional<stbtt_fontinfo>;
namespace Slic3r {
	namespace GUI {
	class GuiTextShape
	{
	public:
        static ExPolygons letter2shapes(wchar_t letter, Point &cursor, FontFileWithCache &font_with_cache, const FontProp &font_prop, fontinfo_opt &font_info_cache);

	private:
	};

}

}; // namespace Slic3r

#endif // slic3r_Text_Shape_hpp_