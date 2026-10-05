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

type TraceDirection = 'web_request' | 'cpp_response' | 'cpp_report';

interface DeviceWebTraceEntry {
    direction: TraceDirection;
    head: Header;
    body: unknown;
    recorded_ts: number;
}

type DeviceWebHealth = Readonly<{
    pageInstanceId: string;
    getConsoleEntries: () => unknown[];
}>;

type DeviceWebDebugWindow = Window & {
    __deviceWebTrace?: DeviceWebTraceEntry[];
    __bambuWebViewHealth?: DeviceWebHealth;
};

let reportedHealthPageInstanceId: string | undefined;
let healthSequence = 0;

function appendDeviceWebTrace(direction: TraceDirection, head: Header, body: unknown) {
    const debugWindow = window as DeviceWebDebugWindow;
    const trace = debugWindow.__deviceWebTrace ?? [];
    trace.push({
        direction,
        head: { ...head },
        body: cloneForTrace(body),
        recorded_ts: Date.now(),
    });
    if (trace.length > 120) {
        trace.splice(0, trace.length - 120);
    }
    debugWindow.__deviceWebTrace = trace;
}

function cloneForTrace(value: unknown) {
    try {
        return JSON.parse(JSON.stringify(value));
    } catch (e) {
        return {
            unserializable: true,
            error: String(e),
        };
    }
}

function rawSend(pkt: unknown) {
    let str: string;
    try {
        str = typeof pkt === 'string' ? pkt : JSON.stringify(pkt);
    } catch (e) {
        console.error('[DeviceWebBridge] Failed to serialize packet', e, pkt);
        throw e;
    }

    // Edge-WebView2 (Windows)
    if (typeof (window as any).chrome?.webview?.postMessage === 'function') {
        (window as any).chrome.webview.postMessage(str);
        return;
    }

    // WKWebView (macOS) — handler registered as "wx" via AddScriptMessageHandler.
    if (typeof (window as any).webkit?.messageHandlers?.wx?.postMessage === 'function') {
        (window as any).webkit.messageHandlers.wx.postMessage(str);
        return;
    }

    // Fallback: trigger wxEVT_WEBVIEW_NAVIGATING via iframe navigation.
    // NOTE: fetch('app://...') does NOT trigger navigation events in WKWebView,
    // so we must use an iframe with src= to trigger decidePolicyForNavigationAction.
    const frame = document.createElement('iframe');
    frame.style.display = 'none';
    frame.src = 'app://' + encodeURIComponent(str);
    document.body.appendChild(frame);
    setTimeout(() => { try { document.body.removeChild(frame); } catch {} }, 200);
}

function rawSendCallback<T>(head: Header, body?: T) {
    rawSend({ head, body });
}

export function reportDeviceHealthReady(): void {
    const pageInstanceId = (window as DeviceWebDebugWindow).__bambuWebViewHealth?.pageInstanceId;
    if (!pageInstanceId || reportedHealthPageInstanceId === pageInstanceId) return;

    try {
        const documentUrl = String(window.location.href).slice(0, 2048);
        for (const action of ['boot', 'ready'] as const) {
            rawSendCallback(
                { version: DEVICE_WEB_SDK_VERSION, type: 'request', seq: healthSequence++, ts: Date.now() },
                {
                    module: 'device_host',
                    submod: 'health',
                    action,
                    payload: {
                        page_instance_id: pageInstanceId,
                        document_url: documentUrl,
                    },
                }
            );
        }
        reportedHealthPageInstanceId = pageInstanceId;
    } catch {
        // Health reporting must not alter the completed business response.
    }
}

function marksHealthReady(params: Readonly<Record<string, unknown>>): boolean {
    return params.module !== 'device_host' &&
        (params.action === 'init' || (params.submod === 'state' && params.action === 'get'));
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
        marksReady: boolean;
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
            appendDeviceWebTrace('web_request', head, reqBody);
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

                pending.current.set(seq, { resolve, timer, marksReady: marksHealthReady(reqBody) });
            });
        },
        [send]
    );

    useEffect(() => {
        const handler = (e: Event) => {
            const pkt = (e as CustomEvent<Packet>).detail;
            if (!validateHeader(pkt)) return;
            if (pkt.head.type === 'response' || pkt.head.type === 'report') {
                appendDeviceWebTrace(pkt.head.type === 'response' ? 'cpp_response' : 'cpp_report', pkt.head, pkt.body);
            }

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
                if (entry.marksReady && Number(body?.error_code ?? 0) === 0) {
                    queueMicrotask(reportDeviceHealthReady);
                }
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
    }, [send]);

    return request;
}
