import type { PreviewItem } from '../types';
import { cn, slotFill } from './style';

export function AmsPreviewBar({
  items,
  onSelect,
}: {
  items: PreviewItem[];
  onSelect: (item: PreviewItem) => void;
}) {
  if (items.length === 0) return null;
  return (
    <div className="flex items-center gap-1">
      {items.map((item) => (
        <button
          key={item.ams_id}
          type="button"
          onClick={() => onSelect(item)}
          className={cn(
            'rounded border px-1.5 py-1',
            item.active ? 'border-[#00AE42]' : 'border-transparent hover:border-[#C2C2C2]',
          )}
          aria-label={`${item.ams_type_name} ${item.ams_id}`}
        >
          <div className="flex h-6 items-end gap-[3px] rounded-sm bg-[#EEEEEE] px-1.5 py-1">
            {item.cubes.map((cube, index) => (
              <span
                key={`${item.ams_id}-${index}`}
                className="h-3.5 w-1.5 rounded-sm"
                style={{ background: cube.is_exists ? slotFill(cube) : '#D0D0D0' }}
              />
            ))}
          </div>
        </button>
      ))}
    </div>
  );
}
