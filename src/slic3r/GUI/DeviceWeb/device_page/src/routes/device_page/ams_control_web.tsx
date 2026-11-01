import { createFileRoute } from '@tanstack/react-router';
import { AmsControlWebPage } from '../../features/device-page/ams-control-web/AmsControlWebPage';

export const Route = createFileRoute('/device_page/ams_control_web')({
  component: AmsControlWebPage,
});
