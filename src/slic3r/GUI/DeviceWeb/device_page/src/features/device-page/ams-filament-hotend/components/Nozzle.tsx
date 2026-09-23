import type { AmsFilamentHotendNozzleState, HotendToolKind } from '../types';
import { visibleFilamentColor } from '../visibleFilamentColor';
import nozzleAlertErrorUrl from '../assets/nozzle_alert_error.svg';
import nozzleAlertLostUrl from '../assets/nozzle_alert_lost.svg';
import nozzleAlertFeedSuspectUrl from '../assets/nozzle_alert_feed_suspect.svg';
import nozzleAlertUnknownUrl from '../assets/nozzle_alert_unknown.svg';

// Alert badges share one 20x20 slot centered on the 24x48 nozzle (per Figma).
const ALERT_BADGE_URL: Partial<Record<AmsFilamentHotendNozzleState, string>> = {
  Error: nozzleAlertErrorUrl,
  Lost: nozzleAlertLostUrl,
  FeedSuspect: nozzleAlertFeedSuspectUrl,
  Unknown: nozzleAlertUnknownUrl,
};

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

// "No hotend" silhouette (inline from hotend_null.svg, native viewBox 24x48).
// A dashed rounded-rect frame plus a grey "prohibited" circle, shown when the
// slot has no nozzle/tool inserted (nozzleState === 'None').
const NULL_PROHIBIT_PATH =
  'M12.0005 17.3323C13.7684 17.3324 15.4642 18.0344 16.7144 19.2844C17.9646 20.5347 18.6665 22.2312 18.6665 23.9993C18.6664 25.7673 17.9645 27.463 16.7144 28.7131C15.4642 29.9633 13.7685 30.6652 12.0005 30.6653C10.2324 30.6653 8.53589 29.9634 7.28564 28.7131C6.03558 27.463 5.33358 25.7672 5.3335 23.9993C5.3335 22.2312 6.0354 20.5347 7.28564 19.2844C8.53589 18.0342 10.2324 17.3323 12.0005 17.3323ZM8.67139 28.2698C9.61789 29.0075 10.7878 29.4153 12.0005 29.4153C13.437 29.4152 14.8148 28.8451 15.8306 27.8293C16.8463 26.8136 17.4164 25.4357 17.4165 23.9993C17.4165 22.7866 17.0088 21.6167 16.271 20.6702L8.67139 28.2698ZM12.0005 18.5823C10.5639 18.5823 9.18623 19.1534 8.17041 20.1692C7.15459 21.185 6.5835 22.5627 6.5835 23.9993C6.58357 25.2115 6.99074 26.3811 7.72803 27.3274L15.3286 19.7268C14.3823 18.9895 13.2127 18.5823 12.0005 18.5823Z';

// Alert silhouette (inline from hotend_error.svg, native viewBox 24x48).
// Reuses the standard BODY_PATH outline (lighter grey) with a metal nozzle
// body; the per-state badge comes from ALERT_BADGE_URL.
const ERROR_FILAMENT_PATH =
  'M13 114H11V111.964H13V114ZM13 107.893H11V103.821H13V107.893ZM13 99.75H11V95.6787H13V99.75ZM13 91.6074H11V87.5361H13V91.6074ZM13 83.4639H11V79.3926H13V83.4639ZM13 75.3213H11V71.25H13V75.3213ZM13 67.1787H11V63.1074H13V67.1787ZM13 59.0361H11V54.9639H13V59.0361ZM13 50.8926H11V46.8213H13V50.8926ZM13 42.75H11V38.6787H13V42.75ZM13 34.6074H11V30.5361H13V34.6074ZM13 26.4639H11V22.3926H13V26.4639ZM13 18.3213H11V14.25H13V18.3213ZM13 10.1787H11V6.10742H13V10.1787ZM13 2.03613H11V0H13V2.03613Z';
const ERROR_NOZZLE_BODY_PATH =
  'M12.2568 47.5554H11.7744L11.3887 46.3337H10.874L8.94434 45.1111L9.58789 42.6667H9.55566V32.8894H5.77734V26.1667H18V32.8894H14.4443V42.6667H14.4121L15.0557 45.1111L13.2861 46.3337H12.6113L12.2568 47.5554Z';
const ERROR_NOZZLE_DETAIL_PATH =
  'M18.1113 32.8889H5.88867V32.2776H18.1113V32.8889ZM18.1113 31.6663H5.88867V31.0559H18.1113V31.6663ZM18.1113 30.4446H5.88867V29.8333H18.1113V30.4446ZM10.1826 28.4827H8.86133V27.3245H10.1826V28.4827ZM15.1377 28.4827H13.8164V27.3245H15.1377V28.4827Z';
// Cutter / pen tool silhouettes (inline from hotend_blade.svg /
// hotend_filament_pen.svg, viewBox 24x48). All share the BODY_PATH outline
// (#DBDBDB) plus the dashed filament; only the tool body below differs.
const BLADE_BODY_TOP_PATH =
  'M15.6665 31.9585H8.3335V31.0425H15.6665V31.9585ZM15.6665 30.5835H8.3335V29.8198H15.6665V30.5835ZM15.6665 29.3618H8.3335V26.4585H15.6665V29.3618ZM9.82959 27.3755C9.50976 27.3755 9.25049 27.6348 9.25049 27.9546C9.25066 28.2743 9.50987 28.5337 9.82959 28.5337H9.95557C10.2751 28.5335 10.5345 28.2742 10.5347 27.9546C10.5347 27.6349 10.2752 27.3757 9.95557 27.3755H9.82959ZM13.9546 27.3755C13.6348 27.3755 13.3755 27.6348 13.3755 27.9546C13.3757 28.2743 13.6349 28.5337 13.9546 28.5337H14.0806C14.4001 28.5335 14.6595 28.2742 14.6597 27.9546C14.6597 27.6349 14.4002 27.3757 14.0806 27.3755H13.9546Z';
const BLADE_BODY_BOTTOM_PATH =
  'M13.3745 37.9165H13.3755V39.2642H14.3911C14.7387 39.2645 15.0155 39.5528 14.9946 39.8999C14.9577 40.5141 14.7949 40.9727 14.4175 41.8726C14.0398 42.7728 12.6203 45.696 11.6177 46.9292C11.2658 47.3611 10.5981 47.1096 10.5981 46.5522V39.2642H10.6255V31.9585H13.3745V37.9165Z';

const PEN_TIP_PATH =
  'M13.8325 42.8816C13.8325 43.1441 13.7905 43.405 13.7075 43.6541L12.5796 47.0388C12.3938 47.5958 11.6052 47.5959 11.4194 47.0388L10.2915 43.6541C10.2085 43.405 10.1665 43.1441 10.1665 42.8816V31.6667H13.8325V42.8816Z';
const PEN_BARREL_BG_PATH = 'M10.1665 31.6667H13.8332V42.6667H10.1665V31.6667Z';
const PEN_BARREL_FILL_PATH =
  'M10.7773 31.6667H13.2218V41.4445C13.2218 41.782 12.9482 42.0556 12.6107 42.0556H11.3885C11.0509 42.0556 10.7773 41.782 10.7773 41.4445V31.6667Z';
const PEN_BODY_TOP_PATH =
  'M15.5562 29.5286H8.3335V29.9866H15.5562V30.7502H8.3335V31.2092H15.5562V32.1252H8.22217V26.6252H15.5562V29.5286ZM9.82959 27.5422C9.50976 27.5422 9.25049 27.8015 9.25049 28.1213C9.25066 28.441 9.50987 28.7004 9.82959 28.7004H9.95557C10.2752 28.7004 10.5345 28.441 10.5347 28.1213C10.5347 27.8015 10.2753 27.5423 9.95557 27.5422H9.82959ZM13.9546 27.5422C13.6348 27.5422 13.3755 27.8015 13.3755 28.1213C13.3757 28.441 13.6349 28.7004 13.9546 28.7004H14.0806C14.4002 28.7004 14.6595 28.441 14.6597 28.1213C14.6597 27.8015 14.4003 27.5423 14.0806 27.5422H13.9546Z';

// "Racked" tool-changer grip (inline from hotend_racked_blade.svg /
// hotend_racked_filament_pen.svg). When on_extruder is true the tool is held
// by the changer: a continuous filament bar, the BODY_PATH redrawn, plus the
// grip body and its two feet.
const RACKED_FILAMENT_BAR_PATH = 'M11 0L13 -8.74229e-08L13 64L11 64L11 0Z';
const RACKED_GRIP_BODY_PATH =
  'M3.23242 8.2793H20.7676C21.8464 8.27937 22.7206 9.15365 22.7207 10.2324V37.0088H13.2793V32.2451H14.9414C15.4037 32.2451 15.7792 31.8705 15.7793 31.4082V27.0586C15.7793 26.5962 15.4038 26.2207 14.9414 26.2207H8.85059C8.38824 26.2207 8.01367 26.5962 8.01367 27.0586V31.415C8.01374 31.8732 8.38179 32.246 8.83984 32.252L10.7207 32.2754V37.042L1.2793 37.0088V10.2324C1.27937 9.15365 2.15365 8.27937 3.23242 8.2793Z';
const RACKED_GRIP_FOOT_LEFT_PATH =
  'M2.11621 39.7207H8.56836C9.03052 39.7205 9.40524 39.346 9.40527 38.8838V35.6924C9.40518 35.2302 9.03048 34.8557 8.56836 34.8555H2.11621C1.65392 34.8555 1.27939 35.2301 1.2793 35.6924V38.8838C1.27933 39.3461 1.65389 39.7207 2.11621 39.7207Z';
const RACKED_GRIP_FOOT_RIGHT_PATH =
  'M15.4307 39.7207H21.8828C22.345 39.7205 22.7197 39.346 22.7197 38.8838V35.6924C22.7196 35.2302 22.3449 34.8557 21.8828 34.8555H15.4307C14.9684 34.8555 14.5938 35.2301 14.5938 35.6924V38.8838C14.5938 39.3461 14.9683 39.7207 15.4307 39.7207Z';

const STANDBY_COLOR = '#DBDBDB';
const FALLBACK_SPOOL_COLOR = '#D9D9D9';

export function Nozzle({
  nozzleState,
  displayIndex,
  color,
  hasFilamentInNozzle,
  lineColor,
  hasFilamentInLine,
  onExtruder,
  toolKind,
}: {
  nozzleState: AmsFilamentHotendNozzleState;
  displayIndex: number;
  color?: string;
  hasFilamentInNozzle?: boolean;
  // Feed-line side of the filament bar. The bar is split at the extruder
  // midpoint: above it continues the tube coming down from the card, below it
  // shows what is actually sitting in the nozzle. Defaults to the nozzle
  // values so callers that only know about the nozzle keep a single-tone bar.
  lineColor?: string;
  hasFilamentInLine?: boolean;
  // True when this rack nozzle is currently mounted on the extruder. Swaps
  // the visual to the "extruder gripping the nozzle" silhouette.
  onExtruder?: boolean;
  // Tool type in this slot. cutter/pen draw their own silhouette (and a
  // "racked" variant when onExtruder); everything else falls back to nozzle.
  toolKind?: HotendToolKind;
}) {
  // Prefer the C++ nozzle signal; legacy payloads still drive it via
  // nozzleState === 'Active'. Only paint the dashed filament with the spool
  // color when C++ has reported a real color (`#D9D9D9` is the unknown
  // sentinel and renders as the standby grey).
  const hasFilament = hasFilamentInNozzle ?? nozzleState === 'Active';
  const hasRealColor = !!color && color.toLowerCase() !== FALLBACK_SPOOL_COLOR.toLowerCase();
  // Tint near-white colours so the in-nozzle filament stays visible against
  // the light background, matching the FeedLine treatment above.
  const filamentColor = hasFilament && hasRealColor && color ? visibleFilamentColor(color) : STANDBY_COLOR;

  const lineHasFilament = hasFilamentInLine ?? hasFilament;
  const lineSource = lineColor ?? color;
  const lineHasRealColor = !!lineSource && lineSource.toLowerCase() !== FALLBACK_SPOOL_COLOR.toLowerCase();
  const feedColor = lineHasFilament && lineHasRealColor && lineSource
    ? visibleFilamentColor(lineSource)
    : STANDBY_COLOR;

  // Shared filament overlay, parameterized on the upper/lower clip bounds so
  // each render branch can reuse it with its own body geometry. The bar is
  // continuous and centered on the column axis (x = 12) so it lines up with
  // the FeedLine above; the two halves can differ in colour when the tube and
  // the nozzle hold different filament.
  const filamentOverlay = (
    <>
      <g clipPath="url(#nozzle-filament-upper-clip)">
        <rect x={11} y={0} width={2} height={22} fill={feedColor} />
      </g>
      <g clipPath="url(#nozzle-filament-lower-clip)">
        <rect x={11} y={22} width={2} height={24} fill={filamentColor} />
      </g>
    </>
  );

  if (nozzleState === 'None') {
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
        <rect x={2.5} y={2.5} width={19} height={43} rx={1.5} stroke="#C2C2C2" strokeDasharray="4 4" />
        <path d={NULL_PROHIBIT_PATH} fill="#C2C2C2" />
      </svg>
    );
  }

  const alertBadgeUrl = ALERT_BADGE_URL[nozzleState];
  if (alertBadgeUrl) {
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
          <clipPath id="nozzle-error-filament-clip">
            <rect x={2} y={0} width={20} height={26} />
          </clipPath>
        </defs>

        <path d={BODY_PATH} fill="#DBDBDB" />
        <g clipPath="url(#nozzle-error-filament-clip)">
          <path d={ERROR_FILAMENT_PATH} fill="#C2C2C2" />
        </g>
        <path d={ERROR_NOZZLE_BODY_PATH} fill="#A3A3A3" />
        <path d={ERROR_NOZZLE_DETAIL_PATH} fill="black" />
        <image href={alertBadgeUrl} x={2} y={14} width={20} height={20} />
      </svg>
    );
  }

  if (toolKind === 'cutter' || toolKind === 'pen') {
    const penBarrelFill = onExtruder ? '#00FF33' : '#B6F34F';
    return (
      <svg
        className={`nozzle nozzle-${nozzleState.toLowerCase()} nozzle-${toolKind}${onExtruder ? ' nozzle-racked' : ''}`}
        width={24}
        height={48}
        viewBox="0 0 24 48"
        fill="none"
        aria-label={`nozzle-${displayIndex}-${toolKind}`}
        data-nozzle-state={nozzleState}
        data-tool-kind={toolKind}
        data-on-extruder={onExtruder ? 'true' : undefined}
      >
        <defs>
          <clipPath id="nozzle-tool-filament-clip">
            <rect x={2} y={0} width={20} height={26} />
          </clipPath>
        </defs>

        <path d={BODY_PATH} fill="#DBDBDB" />
        <g clipPath="url(#nozzle-tool-filament-clip)">
          {onExtruder
            ? <path d={RACKED_FILAMENT_BAR_PATH} fill="#C2C2C2" />
            : <path d={ERROR_FILAMENT_PATH} fill="#C2C2C2" />}
        </g>

        {onExtruder && (
          <>
            <path d={RACKED_GRIP_BODY_PATH}       fill="#DBDBDB" stroke="white" strokeWidth={0.55814} />
            <path d={RACKED_GRIP_FOOT_LEFT_PATH}  fill={toolKind === 'cutter' ? '#A3A3A3' : '#C2C2C2'} stroke="white" strokeWidth={0.55814} />
            <path d={RACKED_GRIP_FOOT_RIGHT_PATH} fill={toolKind === 'cutter' ? '#A3A3A3' : '#C2C2C2'} stroke="white" strokeWidth={0.55814} />
          </>
        )}

        {toolKind === 'cutter' ? (
          <>
            <path d={BLADE_BODY_TOP_PATH}    fill="#A3A3A3" />
            <path d={BLADE_BODY_BOTTOM_PATH} fill="#A3A3A3" />
          </>
        ) : (
          <>
            <path d={PEN_TIP_PATH}        fill="#A3A3A3" />
            <path d={PEN_BARREL_BG_PATH}  fill="#DBDBDB" />
            <path d={PEN_BARREL_FILL_PATH} fill={penBarrelFill} />
            <path d={PEN_BODY_TOP_PATH}   fill="#A3A3A3" />
          </>
        )}
      </svg>
    );
  }

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

        {/* Top-align with the regular nozzle body (y=4). */}
        <g transform="translate(1, 4)">
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
