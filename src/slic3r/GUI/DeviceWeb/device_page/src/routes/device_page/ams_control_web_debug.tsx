import { createFileRoute } from '@tanstack/react-router';
import { AmsControlWebDebugPage } from '../../features/device-page/ams-control-web/bridge-debug/AmsControlWebDebugPage';

export const Route = createFileRoute('/device_page/ams_control_web_debug')({
  component: AmsControlWebDebugPage,
});
