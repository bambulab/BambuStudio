import { createFileRoute } from '@tanstack/react-router';
import { ControlPanelPage } from '../../features/device-page/control-panel/ControlPanelPage';

export const Route = createFileRoute('/device_page/control_panel')({
  component: ControlPanelPage,
});
