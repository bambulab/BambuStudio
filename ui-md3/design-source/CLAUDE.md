# Bambu Studio — Material Design 3 Redesign (design source)

This folder holds the ORIGINAL Claude Design "Design Component" source that we are
implementing as a standalone web application. Do not edit these files — they are the
source of truth for fidelity. The real implementation lives in ui-md3/ (index.html etc).

- `Bambu Studio.dc.html` — the full desktop-app shell design (all 9 screens + dialogs).
- `SearchField.dc.html`  — a reusable regex-capable search field component.
- `support.js`           — the proprietary React-based DC runtime the design was authored against.
                           We DO NOT ship this; we reimplement a tiny vanilla runtime instead.
