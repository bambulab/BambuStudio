import { createFileRoute } from '@tanstack/react-router';
import { HotendDetailPage } from '../../features/device-page/ams-filament-hotend/hotend-detail/HotendDetailPage';

export const Route = createFileRoute('/device_page/hotend_detail')({
  component: HotendDetailPage,
});
