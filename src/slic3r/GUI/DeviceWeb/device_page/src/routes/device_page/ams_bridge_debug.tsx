import { createFileRoute } from '@tanstack/react-router';
import { AmsBridgeDebugPage } from '../../features/device-page/ams-filament-hotend/bridge-debug/AmsBridgeDebugPage';

export const Route = createFileRoute('/device_page/ams_bridge_debug')({
  component: AmsBridgeDebugPage,
});
