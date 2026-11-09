import { createFileRoute } from '@tanstack/react-router';
import { AutoRefillRefillInfoPage } from '../../features/device-page/auto-refill-refill-info/AutoRefillRefillInfoPage';

export const Route = createFileRoute('/device_page/auto_refill_refill_info')({
  component: AutoRefillRefillInfoPage,
});
