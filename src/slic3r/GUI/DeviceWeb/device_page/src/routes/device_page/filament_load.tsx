import { createFileRoute } from '@tanstack/react-router';
import { FilamentLoadPage } from '../../features/device-page/filament-load/FilamentLoadPage';

export const Route = createFileRoute('/device_page/filament_load')({
  component: FilamentLoadPage,
});
