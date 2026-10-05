import { useEffect } from 'react';
import useStore from '../store/AppStore';

type ThemeName = 'dark' | 'light';

// Modules that report runtime theme changes.
const THEME_SOURCE_MODULES = new Set<string>([
  'device_page_ams_filament_hotend',
  'device_page_connected_slots',
  'device_page_ams_control_web',
]);

function isThemeName(value: unknown): value is ThemeName {
  return value === 'dark' || value === 'light';
}

// Mirrors host theme reports into app state and <html data-theme>.
export function useDeviceWebTheme(): void {
  const theme = useStore((s) => s.filament.theme);
  const setTheme = useStore((s) => s.filament.setTheme);

  useEffect(() => {
    document.documentElement.dataset.theme = theme;
  }, [theme]);

  useEffect(() => {
    const handler = (event: Event) => {
      const detail = (event as CustomEvent).detail;
      const body = detail?.body;
      if (!body || typeof body !== 'object') return;
      if (detail.head?.type !== 'report') return;
      const { module: mod, submod, action, payload } = body as {
        module?: string; submod?: string; action?: string; payload?: unknown;
      };
      if (!mod || !THEME_SOURCE_MODULES.has(mod)) return;
      if (submod !== 'init' || action !== 'theme_changed') return;
      const next = (payload as { theme?: unknown } | undefined)?.theme;
      if (isThemeName(next)) setTheme(next);
    };
    document.addEventListener('cpp:device', handler);
    return () => document.removeEventListener('cpp:device', handler);
  }, [setTheme]);
}
