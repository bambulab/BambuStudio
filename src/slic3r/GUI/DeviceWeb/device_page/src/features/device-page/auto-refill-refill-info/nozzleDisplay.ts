const TPU_285_FLOW = 'TPU 285';

export function formatAutoRefillNozzle(
  diameter: number | string | undefined,
  flowType?: string,
): string {
  // Accept the old short code as well so a refreshed WebView also renders
  // correctly before the host executable containing the new schema is rebuilt.
  const flow = flowType === 'TF' ? TPU_285_FLOW : flowType;
  if (diameter === undefined || diameter === '') return flow ?? '';

  const diameterText = typeof diameter === 'number' ? diameter.toFixed(1) : diameter;
  return flow ? `${diameterText} ${flow}` : diameterText;
}
