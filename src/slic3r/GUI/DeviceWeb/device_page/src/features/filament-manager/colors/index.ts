// STUDIO-17977: shared colour-management module for the filament feature.
// All filament surfaces (list, detail dialog, add/edit dialog, SVG chip)
// import from this single entry point so hex normalisation, swatch
// rendering, and candidate reverse-lookup logic stay consistent.
//
// Module layout:
//   - hex.ts        primitive string-level normalisation + multiset keys
//   - render.ts     CSS background + text label expressions
//   - candidate.ts  official judgment + reverse-lookup chains

export {
  canonicalizeHex,
  canonicalizeHexList,
  hexEqual,
  hexMultisetKey,
} from './hex';

export {
  cssBackgroundFor,
  hexLabelFor,
  colorNameWithHexLabel,
} from './render';
export type { ColorRenderInput } from './render';

export {
  isOfficialCandidate,
  matchHexMultiset,
  resolveCandidateForSpool,
  candidateMatchesFormState,
} from './candidate';
