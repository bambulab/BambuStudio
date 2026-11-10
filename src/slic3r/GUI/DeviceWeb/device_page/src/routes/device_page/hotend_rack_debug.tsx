import { createFileRoute } from '@tanstack/react-router';
import { BridgeSnapshotDebugPage } from '../../features/device-page/ams-control-web/bridge-debug/AmsControlWebDebugPage';

function HotendRackDebugPage() {
  return <BridgeSnapshotDebugPage module="device_page_hotend_rack" title="Hotend Rack Web Bridge Debug" />;
}

export const Route = createFileRoute('/device_page/hotend_rack_debug')({
  component: HotendRackDebugPage,
});
