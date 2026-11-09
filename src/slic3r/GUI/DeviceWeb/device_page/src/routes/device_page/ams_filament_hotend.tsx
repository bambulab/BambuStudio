import { createFileRoute } from '@tanstack/react-router';
import { AmsFilamentHotendPage } from '../../features/device-page/ams-filament-hotend/AmsFilamentHotendPage';

export const Route = createFileRoute('/device_page/ams_filament_hotend')({
  component: AmsFilamentHotendPage,
});
