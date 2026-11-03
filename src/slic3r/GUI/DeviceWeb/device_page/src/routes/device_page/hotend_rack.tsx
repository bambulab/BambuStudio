import { createFileRoute } from '@tanstack/react-router';
import { HotendRackPage } from '../../features/device-page/hotend-rack/HotendRackPage';

export const Route = createFileRoute('/device_page/hotend_rack')({
  component: HotendRackPage,
});
