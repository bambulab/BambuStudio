import type { ConnectedSlotsSchematic, ConnectedSlotsSchematicItem } from '../types';
import { AmsMiniSilhouette } from './AmsMiniSilhouette';

const EMPTY_CHIP = '#c2c2c2';

/**
 * `color` holds one comma separated entry per slot and keeps empty slots as
 * blanks, so the split must stay positional.
 */
function slotColors(item: ConnectedSlotsSchematicItem) {
  const count = Math.max(item.slot_count ?? 0, 1);
  const parsed = (item.color ?? '').split(',').map((c) => c.trim());
  return Array.from({ length: count }, (_, i) => parsed[i] || EMPTY_CHIP);
}

/** Ext chip: red (or tray) bar + Ext label. */
export function ExtSourceItem({ item, onSelect }: { item: ConnectedSlotsSchematicItem; onSelect?: () => void }) {
  return (
    <button
      type="button"
      className={`cs-ext-source-item${item.selected ? ' cs-ext-source-item--selected' : ''}${item.empty ? ' cs-ext-source-item--empty' : ''}`}
      data-testid={`ext-source-item-${item.ext_index}`}
      aria-pressed={item.selected ?? false}
      onClick={onSelect}
    >
      <i
        className="cs-ext-source-item__bar"
        style={{ background: item.empty ? EMPTY_CHIP : (item.color || '#ff8181') }}
      />
      <span className="cs-ext-source-item__label">{item.label || 'Ext'}</span>
    </button>
  );
}

/** Mini AMS chip: the unit silhouette with one filament cube per slot. */
export function AmsSourceItem({ item, onSelect }: { item: ConnectedSlotsSchematicItem; onSelect?: () => void }) {
  return (
    <button
      type="button"
      className={`cs-ams-source-item${item.selected ? ' cs-ams-source-item--selected' : ''}`}
      data-testid={`ams-source-item-${item.ext_index}`}
      title={item.label}
      aria-pressed={item.selected ?? false}
      onClick={onSelect}
    >
      <AmsMiniSilhouette colors={slotColors(item)} />
    </button>
  );
}

/** An item is the Ext source when the backend left `ams_id` empty. */
function isExtSource(item: ConnectedSlotsSchematicItem) {
  return !item.ams_id;
}

/** Connected Ext / AMS sources in the right schematic bar. */
export function ConnectedSourceSchematic({
  schematic,
  onSelectSource,
}: {
  schematic: ConnectedSlotsSchematic;
  onSelectSource?: (extIndex: number) => void;
}) {
  const items = schematic.items ?? [];
  return (
    <div
      className="cs-connected-source-schematic"
      data-testid="connected-source-schematic"
    >
      {items.map((item) => {
        const onSelect = onSelectSource ? () => onSelectSource(item.ext_index) : undefined;
        return isExtSource(item)
          ? <ExtSourceItem key={item.ext_index} item={item} onSelect={onSelect} />
          : <AmsSourceItem key={item.ext_index} item={item} onSelect={onSelect} />;
      })}
    </div>
  );
}
