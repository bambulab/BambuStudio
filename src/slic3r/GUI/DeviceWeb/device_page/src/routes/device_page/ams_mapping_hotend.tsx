import { createFileRoute } from '@tanstack/react-router';
import { AmsMappingHotendPage } from '../../features/device-page/ams-mapping-hotend/AmsMappingHotendPage';

export const Route = createFileRoute('/device_page/ams_mapping_hotend')({
  component: AmsMappingHotendPage,
});
