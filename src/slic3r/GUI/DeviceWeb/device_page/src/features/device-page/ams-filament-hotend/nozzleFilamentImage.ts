import { useEffect, useState } from 'react';
import hotendNozzleUrl from './assets/hotend_nozzle.png';

// hotend_nozzle.png ships with the filament bar painted in this colour.
const BAKED_FILAMENT_RGB: [number, number, number] = [0xB6, 0xF3, 0x4F];
// The bar is flat-filled in the asset, so an exact match is enough; keep a
// small tolerance in case the asset is re-exported with slight rounding.
const MATCH_TOLERANCE = 8;

const cache = new Map<string, string>();
let basePixels: Promise<ImageData | null> | null = null;

function loadBasePixels(): Promise<ImageData | null> {
  if (basePixels) return basePixels;
  basePixels = new Promise<ImageData | null>((resolve) => {
    const img = new Image();
    img.onload = () => {
      const canvas = document.createElement('canvas');
      canvas.width = img.naturalWidth;
      canvas.height = img.naturalHeight;
      const ctx = canvas.getContext('2d', { willReadFrequently: true });
      if (!ctx) {
        resolve(null);
        return;
      }
      ctx.drawImage(img, 0, 0);
      try {
        resolve(ctx.getImageData(0, 0, canvas.width, canvas.height));
      } catch {
        resolve(null);
      }
    };
    img.onerror = () => resolve(null);
    img.src = hotendNozzleUrl;
  });
  return basePixels;
}

function parseRgb(color: string): [number, number, number] | null {
  const match = /^#?([0-9a-fA-F]{6})/.exec(color.trim());
  if (!match) return null;
  const value = parseInt(match[1], 16);
  return [(value >> 16) & 0xFF, (value >> 8) & 0xFF, value & 0xFF];
}

function isBakedFilament(r: number, g: number, b: number): boolean {
  return Math.abs(r - BAKED_FILAMENT_RGB[0]) <= MATCH_TOLERANCE
    && Math.abs(g - BAKED_FILAMENT_RGB[1]) <= MATCH_TOLERANCE
    && Math.abs(b - BAKED_FILAMENT_RGB[2]) <= MATCH_TOLERANCE;
}

async function buildRecolored(color: string): Promise<string | null> {
  const rgb = parseRgb(color);
  if (!rgb) return null;

  const base = await loadBasePixels();
  if (!base) return null;

  const canvas = document.createElement('canvas');
  canvas.width = base.width;
  canvas.height = base.height;
  const ctx = canvas.getContext('2d');
  if (!ctx) return null;

  const pixels = new ImageData(new Uint8ClampedArray(base.data), base.width, base.height);
  const { data } = pixels;
  for (let i = 0; i < data.length; i += 4) {
    if (data[i + 3] === 0) continue;
    if (!isBakedFilament(data[i], data[i + 1], data[i + 2])) continue;
    [data[i], data[i + 1], data[i + 2]] = rgb;
  }
  ctx.putImageData(pixels, 0, 0);
  return canvas.toDataURL('image/png');
}

/**
 * Nozzle icon whose filament bar is repainted with `color`. Recolouring the
 * source bitmap (instead of overlaying a swatch) keeps the bar clean once the
 * WebView scales the 36x42 asset up for the current display scaling.
 * Falls back to the original asset until the recoloured copy is ready.
 */
export function useNozzleImage(color: string): string {
  const [src, setSrc] = useState(() => cache.get(color) ?? hotendNozzleUrl);

  useEffect(() => {
    const cached = cache.get(color);
    if (cached) {
      setSrc(cached);
      return;
    }

    let active = true;
    setSrc(hotendNozzleUrl);
    void buildRecolored(color).then((url) => {
      if (!url) return;
      cache.set(color, url);
      if (active) setSrc(url);
    });
    return () => { active = false; };
  }, [color]);

  return src;
}
