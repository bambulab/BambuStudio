import { createFileRoute } from '@tanstack/react-router';
import { ConnectedSlotsPage } from '../../features/device-page/connected-slots/ConnectedSlotsPage';

export const Route = createFileRoute('/device_page/connected_slots')({
  component: ConnectedSlotsPage,
});
