import { createFileRoute } from '@tanstack/react-router';
import { AmsExtFilamentEditPage } from '../../features/device-page/ams-filament-hotend/ams-ext-filament-edit/AmsExtFilamentEditPage';

export const Route = createFileRoute('/device_page/ams_ext_filament_edit')({
  component: AmsExtFilamentEditPage,
});
