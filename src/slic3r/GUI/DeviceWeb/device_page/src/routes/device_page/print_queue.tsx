import { createFileRoute } from '@tanstack/react-router';
import { PrintQueuePage } from '../../features/device-page/print-queue/PrintQueuePage';

export const Route = createFileRoute('/device_page/print_queue')({
  component: PrintQueuePage,
});
