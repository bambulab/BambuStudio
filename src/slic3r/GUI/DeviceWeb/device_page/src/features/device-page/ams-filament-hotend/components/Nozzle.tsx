import type { AmsFilamentHotendNozzleState } from '../types';
import nozzleNormalUrl from '../assets/nozzle_normal.png';
import nozzleActiveUrl from '../assets/nozzle_active.png';

export function Nozzle({
  nozzleState,
  displayIndex,
}: {
  nozzleState: AmsFilamentHotendNozzleState;
  displayIndex: number;
}) {
  const isActive = nozzleState === 'Active';
  const src = isActive ? nozzleActiveUrl : nozzleNormalUrl;
  return (
    <img
      className={`nozzle nozzle-${nozzleState.toLowerCase()}`}
      src={src}
      width={24}
      height={48}
      alt={`nozzle-${displayIndex}`}
      data-nozzle-state={nozzleState}
    />
  );
}
