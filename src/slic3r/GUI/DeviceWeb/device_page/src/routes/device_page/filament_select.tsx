import { createFileRoute } from '@tanstack/react-router';
import { FilamentSelectPage } from '../../features/device-page/filament-select/FilamentSelectPage';

export const Route = createFileRoute('/device_page/filament_select')({
  component: FilamentSelectPage,
});
