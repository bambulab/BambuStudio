import { createFileRoute } from '@tanstack/react-router';
import { FilamentPage } from '../features/filament/FilamentPage';

export const Route = createFileRoute('/filament')({
  component: FilamentPage,
});
