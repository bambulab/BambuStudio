// Shared by index.html and editor.html: both pages must read the 3mf
// description the same way, otherwise a save round trip corrupts it.
//
// The 3mf round trip is asymmetric. Slic3r::xml_escape() encodes " and ',
// but Slic3r::xml_unescape() only reverses &lt; &gt; &amp;, so quotes arrive
// here still encoded and no longer delimit attributes, e.g.
// <img src=&#34;https://...&#34;> parses into src='"https://..."'.
//
// MakerWorld descriptions use the numeric form &#34;, hand-written markup uses
// &quot;, so both spellings have to be covered.
//
// Restore quotes only. A generic HTML entity decode would also strip one level
// of escaping from the description body, so a literal "a < b" would turn into
// markup and the unclosed tag would swallow the rest of it.
function DecodeDescriptionFrom3MF(text) {
  if (!text) return '';
  return text
    .replace(/&(?:quot|#0*34|#[xX]0*22);/g, '"')
    .replace(/&(?:apos|#0*39|#[xX]0*27);/g, "'");
}
