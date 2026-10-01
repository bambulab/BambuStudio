import { createFileRoute } from '@tanstack/react-router';
import { FilamentManagerPage } from '../features/filament-manager/FilamentManagerPage';

export const Route = createFileRoute('/filament_manager')({
  component: FilamentManagerPage,
});
