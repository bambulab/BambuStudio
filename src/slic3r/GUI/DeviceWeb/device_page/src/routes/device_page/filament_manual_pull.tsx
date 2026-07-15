import { createFileRoute } from '@tanstack/react-router';
import { FilamentManualPullPage } from '../../features/device-page/ams-filament-hotend/manual-pull/FilamentManualPullPage';

export const Route = createFileRoute('/device_page/filament_manual_pull')({
  component: FilamentManualPullPage,
});
