import { createFileRoute } from '@tanstack/react-router';
import { ControlPanelDebugPage } from '../../features/device-page/control-panel/bridge-debug/ControlPanelDebugPage';

export const Route = createFileRoute('/device_page/control_panel_debug')({
  component: ControlPanelDebugPage,
});
