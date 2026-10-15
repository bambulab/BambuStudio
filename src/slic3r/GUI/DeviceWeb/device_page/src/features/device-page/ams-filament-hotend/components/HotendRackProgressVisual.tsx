import type { HotendRackStatus } from '../types';

import rackHotendJobPreviewUrl from '../assets/rack_hotend_job_preview.png';

export function HotendRackProgressVisual({ operation }: { operation?: HotendRackStatus['operation'] }) {
  const shouldShowPreview = operation === 'take'
    || operation === 'place'
    || operation === 'exchange'
    || operation === 'read_all'
    || operation === 'read_single';
  if (!shouldShowPreview) return null;

  return (
    <div className="hotend-rack-progress-visual-card" aria-hidden="true">
      <img className="rack-visual-hotend-job-preview" src={rackHotendJobPreviewUrl} alt="" />
    </div>
  );
}
