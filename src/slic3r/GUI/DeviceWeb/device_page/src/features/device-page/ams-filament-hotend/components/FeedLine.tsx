import type { AmsFilamentHotendLineState } from '../types';
import lineAtNozzleUrl from '../assets/line_at_nozzle.svg';
import lineStandbyUrl from '../assets/line_standby.svg';
import lineTransportingUrl from '../assets/line_transporting.svg';

export function FeedLine({ height, state }: { height: number; state: AmsFilamentHotendLineState }) {
  if (state === 'no_hotend') return <div className="feed-line feed-line-empty" style={{ height }} />;

  const src =
    state === 'loaded' ? lineAtNozzleUrl :
    state === 'loading' || state === 'unloading' ? lineTransportingUrl :
    lineStandbyUrl;

  return (
    <img className="feed-line" src={src} width={20} height={height} alt="" aria-hidden="true" />
  );
}
