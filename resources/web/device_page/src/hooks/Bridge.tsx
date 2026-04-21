import { useRef, useCallback, useEffect } from 'react';
import useStore from '../store/AppStore';

const DEVICE_WEB_SDK_VERSION = '1.0';

type Ok<T> = { ok: true; value: T };
type Err<E> = { ok: false; error: E };
export type Result<T, E = string> = Ok<T> | Err<E>;

export interface Header {
    version: string;
    type: string; // "request" | "response" | "report"
    seq: number;
    ts: number; // ms
}

interface Packet<T = any> { head: Header; body: T; }

function rawSend(pkt: unknown) {
    // Edge-WebView2
    if (typeof (window as any).chrome?.webview?.postMessage === 'function') {
        (window as any).chrome.webview.postMessage(
            typeof pkt === 'string' ? pkt : JSON.stringify(pkt)
        );
        return;
    }

    // Other browsers: app:// URL fallback
    const url = 'app://' + encodeURIComponent(
        typeof pkt === 'string' ? pkt : JSON.stringify(pkt)
    );
    fetch(url).catch(() => {});
}

function rawSendCallback<T>(head: Header, body?: T) {
    rawSend({ head, body });
}

export function useSendToCpp() {
    return useCallback(rawSendCallback, []);
}

function validateHeader(pkt: Packet) {
    return pkt.head.version === DEVICE_WEB_SDK_VERSION;
}

export function useDeviceBridge() {
    const sequence = useRef(0);
    const pending  = useRef(new Map<number, {
        resolve: (r: any) => void;
        timer: ReturnType<typeof setTimeout>;
    }>());

    const send = useSendToCpp();

    const request = useCallback(
        <P = any, R = any>(params?: P, timeoutMs = 5000): Promise<Result<R>> => {
            const seq = sequence.current++;
            const debugState = useStore.getState().filament;

            const head: Header = {
                version: DEVICE_WEB_SDK_VERSION,
                type: 'request',
                seq,
                ts: Date.now(),
            };
            const reqBody = (params ?? {}) as Record<string, unknown>;
            if (debugState.debugEnabled && reqBody.module === 'filament') {
                debugState.appendDebugLog({
                    ts: Date.now(),
                    category: 'bridge',
                    level: 'info',
                    title: 'Web request sent',
                    summary: `${String(reqBody.submod ?? 'unknown')}/${String(reqBody.action ?? 'unknown')}`,
                    detail: reqBody,
                });
            }
            send(head, params ?? {});

            return new Promise<Result<R>>(resolve => {
                const timer = setTimeout(() => {
                    pending.current.delete(seq);
                    resolve({ ok: false, error: 'Request Timeout' });
                }, timeoutMs);

                pending.current.set(seq, { resolve, timer });
            });
        },
        [send]
    );

    useEffect(() => {
        const handler = (e: Event) => {
            const pkt = (e as CustomEvent<Packet>).detail;
            if (!validateHeader(pkt)) return;

            if (pkt.head.type === 'response') {
                const entry = pending.current.get(pkt.head.seq);
                if (!entry) return;
                clearTimeout(entry.timer);
                pending.current.delete(pkt.head.seq);
                const body = pkt.body as Record<string, unknown>;
                const debugState = useStore.getState().filament;
                if (debugState.debugEnabled && body?.module === 'filament') {
                    debugState.appendDebugLog({
                        ts: Date.now(),
                        category: 'bridge',
                        level: Number(body.error_code ?? 0) === 0 ? 'info' : 'error',
                        title: 'C++ response received',
                        summary: `${String(body.submod ?? 'unknown')}/${String(body.action ?? 'unknown')}`,
                        detail: body,
                    });
                }
                entry.resolve({ ok: true, value: pkt.body });
            }
            // 'report' events are handled by individual feature hooks
            // via their own document.addEventListener('cpp:device', ...) subscriptions
        };

        document.addEventListener('cpp:device', handler);
        return () => {
            document.removeEventListener('cpp:device', handler);
            pending.current.forEach(entry => clearTimeout(entry.timer));
            pending.current.clear();
        };
    }, []);

    return request;
}
