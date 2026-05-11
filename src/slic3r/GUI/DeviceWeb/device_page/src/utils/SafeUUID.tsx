export function safeRandomUUID(): string {
  const g: any = globalThis.crypto || (globalThis as any).msCrypto;

  if (g?.randomUUID) return g.randomUUID();

  if (g?.getRandomValues) {
    const b = new Uint8Array(16);
    g.getRandomValues(b);
    b[6] = (b[6] & 0x0f) | 0x40; // version 4
    b[8] = (b[8] & 0x3f) | 0x80; // variant
    const h = (n: number) => n.toString(16).padStart(2, '0');
    const s = Array.from(b, h).join('');
    return `${s.slice(0,8)}-${s.slice(8,12)}-${s.slice(12,16)}-${s.slice(16,20)}-${s.slice(20)}`;
  }

  // 最末级，非加密强度
  return 'xxxxxxxx-xxxx-4xxx-yxxx-xxxxxxxxxxxx'.replace(/[xy]/g, c => {
    const r = (Math.random() * 16) | 0;
    const v = c === 'x' ? r : (r & 0x3) | 0x8;
    return v.toString(16);
  });
}
