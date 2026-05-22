import type { AmsFilamentHotendNozzleState } from '../types';

// Inline nozzle from the Figma export (viewBox 24x48). Rendered as SVG so
// the dashed filament path through the assembly can take the spool color
// at runtime instead of relying on a baked-in PNG.

// Outer extruder + heater block silhouette (the static grey body).
const BODY_PATH =
  'M14.4414 4C14.7497 4 15 4.25034 15 4.55859V9H17.8838C18.5002 9.00004 19 9.49976 19 10.1162V35.8838C19 36.5002 18.5002 37 17.8838 37H6.11621C5.49976 37 5.00004 36.5002 5 35.8838V10.1162C5.00004 9.49976 5.49976 9.00004 6.11621 9H9V4.55859C9 4.25034 9.25034 4 9.55859 4H14.4414Z';

// "Extruder gripping the nozzle" silhouette, used on the Filament tab when
// `filament_nozzle.on_extruder === true`. Authored in a native 22x32 viewBox
// (per Figma export); we translate it into the standard 24x48 nozzle slot so
// the dashed filament overlay coordinates and column-level spacing match the
// regular Nozzle render branch.
const EXTRUDER_BODY_PATH =
  'M2.23242 0.279297H19.7676C20.8464 0.27937 21.7206 1.15365 21.7207 2.23242V29.0088H13.5947V22.9658C13.5949 22.5679 13.9175 22.2453 14.3154 22.2451H15.6523C16.1146 22.2451 16.4891 21.8705 16.4893 21.4082V15.4736C16.4893 15.0113 16.1147 14.6357 15.6523 14.6357H6.34766C5.88528 14.6357 5.51074 15.0113 5.51074 15.4736V21.4082C5.51087 21.8705 5.88536 22.2451 6.34766 22.2451H7.68457C8.08247 22.2453 8.40512 22.5679 8.40527 22.9658V29.0088H0.279297V2.23242C0.279371 1.15365 1.15365 0.279371 2.23242 0.279297Z';
const EXTRUDER_FOOT_LEFT_PATH =
  'M1.11621 31.7207H7.56836C8.03052 31.7205 8.40524 31.346 8.40527 30.8838V27.6924C8.40518 27.2302 8.03048 26.8557 7.56836 26.8555H1.11621C0.65392 26.8555 0.279387 27.2301 0.279297 27.6924V30.8838C0.279335 31.3461 0.653888 31.7207 1.11621 31.7207Z';
const EXTRUDER_FOOT_RIGHT_PATH =
  'M14.4307 31.7207H20.8828C21.345 31.7205 21.7197 31.346 21.7197 30.8838V27.6924C21.7196 27.2302 21.3449 26.8557 20.8828 26.8555H14.4307C13.9684 26.8555 13.5938 27.2301 13.5938 27.6924V30.8838C13.5938 31.3461 13.9683 31.7207 14.4307 31.7207Z';

// Dashed filament path running through the nozzle (upper + lower halves of
// the Figma clip groups). Painted with the spool color when the nozzle is
// loaded; otherwise grey.
const FILAMENT_UPPER_PATH =
  'M13.25 114H11.25V111.964H13.25V114ZM13.25 107.893H11.25V103.821H13.25V107.893ZM13.25 99.75H11.25V95.6787H13.25V99.75ZM13.25 91.6074H11.25V87.5361H13.25V91.6074ZM13.25 83.4639H11.25V79.3926H13.25V83.4639ZM13.25 75.3213H11.25V71.25H13.25V75.3213ZM13.25 67.1787H11.25V63.1074H13.25V67.1787ZM13.25 59.0361H11.25V54.9639H13.25V59.0361ZM13.25 50.8926H11.25V46.8213H13.25V50.8926ZM13.25 42.75H11.25V38.6787H13.25V42.75ZM13.25 34.6074H11.25V30.5361H13.25V34.6074ZM13.25 26.4639H11.25V22.3926H13.25V26.4639ZM13.25 18.3213H11.25V14.25H13.25V18.3213ZM13.25 10.1787H11.25V6.10742H13.25V10.1787ZM13.25 2.03613H11.25V0H13.25V2.03613Z';

const FILAMENT_LOWER_PATH =
  'M13.25 136H11.25V133.964H13.25V136ZM13.25 129.893H11.25V125.821H13.25V129.893ZM13.25 121.75H11.25V117.679H13.25V121.75ZM13.25 113.607H11.25V109.536H13.25V113.607ZM13.25 105.464H11.25V101.393H13.25V105.464ZM13.25 97.3213H11.25V93.25H13.25V97.3213ZM13.25 89.1787H11.25V85.1074H13.25V89.1787ZM13.25 81.0361H11.25V76.9639H13.25V81.0361ZM13.25 72.8926H11.25V68.8213H13.25V72.8926ZM13.25 64.75H11.25V60.6787H13.25V64.75ZM13.25 56.6074H11.25V52.5361H13.25V56.6074ZM13.25 48.4639H11.25V44.3926H13.25V48.4639ZM13.25 40.3213H11.25V36.25H13.25V40.3213ZM13.25 32.1787H11.25V28.1074H13.25V32.1787ZM13.25 24.0361H11.25V22H13.25V24.0361Z';

const STANDBY_COLOR = '#DBDBDB';
const FALLBACK_SPOOL_COLOR = '#D9D9D9';

export function Nozzle({
  nozzleState,
  displayIndex,
  color,
  hasFilamentInNozzle,
  onExtruder,
  solid,
}: {
  nozzleState: AmsFilamentHotendNozzleState;
  displayIndex: number;
  color?: string;
  hasFilamentInNozzle?: boolean;
  // True when this rack nozzle is currently mounted on the extruder. Swaps
  // the visual to the "extruder gripping the nozzle" silhouette.
  onExtruder?: boolean;
  // True when the filament is fully loaded into this slot. Renders the
  // in-nozzle filament as a solid bar instead of the dashed pattern, matching
  // Figma 1477:113586. Loading/unloading keeps the dashed pattern to suggest
  // motion.
  solid?: boolean;
}) {
  // Prefer the C++ nozzle signal; legacy payloads still drive it via
  // nozzleState === 'Active'. Only paint the dashed filament with the spool
  // color when C++ has reported a real color (`#D9D9D9` is the unknown
  // sentinel and renders as the standby grey).
  const hasFilament = hasFilamentInNozzle ?? nozzleState === 'Active';
  const hasRealColor = !!color && color.toLowerCase() !== FALLBACK_SPOOL_COLOR.toLowerCase();
  const filamentColor = hasFilament && hasRealColor ? color : STANDBY_COLOR;

  // Shared filament overlay, parameterized on the upper/lower clip bounds so
  // each render branch can reuse it with its own body geometry. Switches
  // between the original dashed segments (motion / standby) and a single
  // continuous bar (loaded) based on `solid`.
  // Center the in-nozzle filament strictly on the column axis (x = 12) so
  // it lines up with the FeedLine above. The dashed Figma paths sit at
  // x=11.25..13.25 (~0.25px right of center); pull them back via a translate.
  const filamentOverlay = solid ? (
    <>
      <g clipPath="url(#nozzle-filament-upper-clip)">
        <rect x={11} y={0} width={2} height={22} fill={filamentColor} />
      </g>
      <g clipPath="url(#nozzle-filament-lower-clip)">
        <rect x={11} y={22} width={2} height={24} fill={filamentColor} />
      </g>
    </>
  ) : (
    <g transform="translate(-0.25, 0)">
      <g clipPath="url(#nozzle-filament-upper-clip)">
        <path d={FILAMENT_UPPER_PATH} fill={filamentColor} />
      </g>
      <g clipPath="url(#nozzle-filament-lower-clip)">
        <path d={FILAMENT_LOWER_PATH} fill={filamentColor} />
      </g>
    </g>
  );

  if (onExtruder) {
    // The Figma asset is 22x32; translate it into the standard 24x48 nozzle
    // slot so the slot column stays the same width as a regular nozzle and
    // the dashed filament clip rects line up.
    return (
      <svg
        className={`nozzle nozzle-${nozzleState.toLowerCase()} nozzle-on-extruder`}
        width={24}
        height={48}
        viewBox="0 0 24 48"
        fill="none"
        aria-label={`nozzle-${displayIndex}-on-extruder`}
        data-nozzle-state={nozzleState}
        data-on-extruder="true"
      >
        <defs>
          <clipPath id="nozzle-filament-upper-clip">
            <rect x={2.25} y={0} width={20} height={22} />
          </clipPath>
          <clipPath id="nozzle-filament-lower-clip">
            <rect x={2.25} y={22} width={20} height={24} />
          </clipPath>
        </defs>

        <g transform="translate(1, 8)">
          <path d={EXTRUDER_BODY_PATH}       fill="#C2C2C2" stroke="#858585" strokeWidth={0.55814} />
          <path d={EXTRUDER_FOOT_LEFT_PATH}  fill="#EBEBEB" stroke="#858585" strokeWidth={0.55814} />
          <path d={EXTRUDER_FOOT_RIGHT_PATH} fill="#EBEBEB" stroke="#858585" strokeWidth={0.55814} />
        </g>

        {filamentOverlay}
      </svg>
    );
  }

  return (
    <svg
      className={`nozzle nozzle-${nozzleState.toLowerCase()}`}
      width={24}
      height={48}
      viewBox="0 0 24 48"
      fill="none"
      aria-label={`nozzle-${displayIndex}`}
      data-nozzle-state={nozzleState}
    >
      <defs>
        {/* The dashed filament paths are authored in a tall coordinate space;
            clip them to the nozzle viewBox so only the visible segments show. */}
        <clipPath id="nozzle-filament-upper-clip">
          <rect x={2.25} y={0} width={20} height={22} />
        </clipPath>
        <clipPath id="nozzle-filament-lower-clip">
          <rect x={2.25} y={22} width={20} height={24} />
        </clipPath>
      </defs>

      {/* Outer body */}
      <path d={BODY_PATH} fill="#C2C2C2" />

      {/* Heater block window with cooling fins */}
      <rect x={6.32422} y={24.3159} width={11.3514} height={7.4211} fill="#EBEBEB" />
      <rect x={6.32422} y={27}      width={11.3514} height={0.315789} fill="#C2C2C2" />
      <rect x={6.32422} y={28.1055} width={11.3514} height={0.315789} fill="#C2C2C2" />
      <rect x={6.32422} y={29.2104} width={11.3514} height={0.315789} fill="#C2C2C2" />
      <rect x={6.32422} y={30.3159} width={11.3514} height={0.315789} fill="#C2C2C2" />
      <rect x={6.32422} y={31.4214} width={11.3514} height={0.315789} fill="#C2C2C2" />

      {/* Nozzle throat + connector dots */}
      <rect x={9.56738} y={31.7368} width={4.86487} height={10.5789} fill="#EBEBEB" />
      <rect x={8.91895} y={25.2632} width={1.2973} height={1.26316} fill="#858585" />
      <rect x={13.7842} y={25.2632} width={1.2973} height={1.26316} fill="#858585" />

      {/* Tip */}
      <path d="M11.3516 46.2632H12.6489L12.2733 47.5263L11.7612 47.5263L11.3516 46.2632Z" fill="#C2C2C2" />
      <path d="M8.91895 44.5264H15.0811L14.4325 42.3158H9.56759L8.91895 44.5264Z" fill="#EBEBEB" />
      <path d="M8.91895 44.5264H15.0811L13.2973 46.2632L10.8649 46.2632L8.91895 44.5264Z" fill="#EBEBEB" />

      {filamentOverlay}
    </svg>
  );
}
