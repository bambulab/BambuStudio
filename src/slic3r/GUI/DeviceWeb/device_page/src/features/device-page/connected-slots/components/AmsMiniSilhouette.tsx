/**
 * Mini AMS preview, ported from the wx AMSPreview widget: the filament cubes are
 * painted first and the unit silhouette is drawn on top so the cubes only show
 * through its slot cut-outs.
 *
 * Paths are `resources/images/{single,four}_slot_ams_item.svg`; a single-slot AMS
 * (AMS HT) must not be drawn with the four-slot body.
 */

// Both silhouettes cut out the same 6x12 pill per slot; only the origin differs.
const CUBE_WIDTH = 6;
const CUBE_HEIGHT = 12;
const CUBE_RADIUS = 3;

const FOUR_SLOT = {
  viewBox: '0 0 52 32',
  width: 36,
  height: 22,
  cubeY: 9.2617,
  cubeX: (index: number) => 9.5 + 9 * index,
  shell: 'M6 9.18382C6 6.32088 8.32088 4 11.1838 4H40.8162C43.6791 4 46 6.32088 46 9.18382V16H42.5V12.2617C42.5 10.6049 41.1569 9.26172 39.5 9.26172C37.8431 9.26172 36.5 10.6049 36.5 12.2617V16H33.5V12.2617C33.5 10.6049 32.1569 9.26172 30.5 9.26172C28.8431 9.26172 27.5 10.6049 27.5 12.2617V16H24.5V12.2617C24.5 10.6049 23.1569 9.26172 21.5 9.26172C19.8431 9.26172 18.5 10.6049 18.5 12.2617V16H15.5V12.2617C15.5 10.6049 14.1569 9.26172 12.5 9.26172C10.8432 9.26172 9.5 10.6049 9.5 12.2617V16H6V9.18382Z',
  base: 'M36.5 16H33.5V18.2617C33.5 19.9186 32.1569 21.2617 30.5 21.2617C28.8431 21.2617 27.5 19.9186 27.5 18.2617V16H24.5V18.2617C24.5 19.9186 23.1569 21.2617 21.5 21.2617C19.8431 21.2617 18.5 19.9186 18.5 18.2617V16H15.5V18.2617C15.5 19.9186 14.1569 21.2617 12.5 21.2617C10.8432 21.2617 9.5 19.9186 9.5 18.2617V16H4V28H48V16H42.5V18.2617C42.5 19.9186 41.1569 21.2617 39.5 21.2617C37.8431 21.2617 36.5 19.9186 36.5 18.2617V16Z',
};

const SINGLE_SLOT = {
  viewBox: '0 0 29 33',
  width: 20,
  height: 23,
  cubeY: 9.9966,
  cubeX: () => 11.7788,
  shell: 'M11.9626 4.73486C9.09969 4.73486 6.77881 7.05574 6.77881 9.91868V16.7349H11.7788V12.9966C11.7788 11.3397 13.122 9.99658 14.7788 9.99658C16.4357 9.99658 17.7788 11.3397 17.7788 12.9966V16.7349H22.7788V9.91868C22.7788 7.05574 20.4579 4.73486 17.595 4.73486H11.9626Z',
  base: 'M11.7788 16.7349H4.77881V28.7349H24.7788V16.7349H17.7788V18.9966C17.7788 20.6535 16.4357 21.9966 14.7788 21.9966C13.122 21.9966 11.7788 20.6535 11.7788 18.9966V16.7349Z',
};

/** `colors` carries one entry per slot, so its length picks the silhouette. */
export function AmsMiniSilhouette({ colors }: { colors: string[] }) {
  const shape = colors.length > 1 ? FOUR_SLOT : SINGLE_SLOT;
  const cubes = colors.slice(0, shape === FOUR_SLOT ? 4 : 1);
  return (
    <svg
      className="cs-ams-source-item__mini"
      width={shape.width}
      height={shape.height}
      viewBox={shape.viewBox}
      fill="none"
      aria-hidden="true"
    >
      {cubes.map((color, index) => (
        <rect
          key={`cube-${index}`}
          x={shape.cubeX(index)}
          y={shape.cubeY}
          width={CUBE_WIDTH}
          height={CUBE_HEIGHT}
          rx={CUBE_RADIUS}
          fill={color}
        />
      ))}
      <path fillRule="evenodd" clipRule="evenodd" d={shape.shell} fill="var(--cs-ams-shell)" />
      <path fillRule="evenodd" clipRule="evenodd" d={shape.base} fill="var(--cs-ams-base)" />
    </svg>
  );
}
