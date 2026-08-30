# Text tool: "only fonts supporting current text" filter

Date: 2026-07-09 · Status: approved by user (option 1)

## Problem

The text-emboss font combo lists every installed face (~470 after the
macOS weight-name additions). Most cannot render 한글; picking one silently
falls back to the bundled backup font (Nanum Gothic), which reads as
"the font does not change".

## Design

A checkbox under the font combo: **입력 문구 지원 폰트만** (only fonts
supporting the current text). When enabled, the combo hides faces that
cannot render the characters of the current text. It composes with the
existing name-search filter. Empty text disables the effect. The state
persists in app config (`text_only_supported_fonts`). macOS only.

## Coverage check

`CTFontCopyCharacterSet` per face — the font's cmap coverage, the same
table the emboss engine (stb_truetype) reads glyphs from. No file IO;
~470 faces in ≈0.1 s. Recomputed only when the set of distinct
characters in the text changes (sampled, max 16 distinct chars,
whitespace ignored). A face whose name resolution falls back to a
default font simply reports that font's coverage and is filtered out
for Korean text — safe failure direction.

## Components

- `CurFacenames` (GLGizmoText.cpp): add `supports_text` (per-face flag,
  parallel to `faces`) and `support_key` (chars of last computation).
  Not serialized to the fonts.cereal cache.
- `update_text_support(CurFacenames&, const std::string&)` — anonymous
  namespace, `__APPLE__` only; fills `supports_text`.
- `GLGizmoText::m_only_supported_fonts` — loaded from app config in the
  constructor, written on toggle.
- `draw_font_list()` — renders the checkbox, calls
  `update_text_support` while the combo popup is open, and skips
  non-supporting faces in the item loop (both plain and search-filtered
  paths).

## Not in scope

Windows/Linux coverage check, favorites, language presets.
