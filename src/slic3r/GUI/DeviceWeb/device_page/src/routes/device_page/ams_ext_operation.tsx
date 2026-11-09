import { createFileRoute } from '@tanstack/react-router';
import { AmsExtOperationPage } from '../../features/device-page/ams-filament-hotend/ams-ext-operation/AmsExtOperationPage';

export const Route = createFileRoute('/device_page/ams_ext_operation')({
  component: AmsExtOperationPage,
});
