"use client";
import * as React from "react";
import * as Dialog from "@radix-ui/react-dialog";

/**
 * 本次修改（提高可读性）：
 * 1) 日志：修复换行（
），使用等宽字体、自动滚动到底部。
 * 2) 实时统计：改为两列键值布局，标签与数值分离，更清晰；仍用 React+Tailwind 渲染。
 * 3) 仍为非模态（modal={false}），仅右上角按钮可关闭；对齐触发器左上角；全部单位 rem。
 *
 * 规则提醒：只能 React + Tailwind + @radix-ui/react-dialog；所有尺寸单位都使用 rem；每次修改后在画布中给出完整代码并检查语法。
 */

// ====== Helpers (AvcC 处理) ======
function beU32(v: DataView, off: number) {
    return (
        (((v.getUint8(off) << 24) |
            (v.getUint8(off + 1) << 16) |
            (v.getUint8(off + 2) << 8) |
            v.getUint8(off + 3)) >>> 0)
    );
}
function beU64(v: DataView, off: number) {
    const hi = beU32(v, off), lo = beU32(v, off + 4);
    return hi * 4294967296 + lo;
}
function extractSpsPpsFromAvcC(payload: Uint8Array) {
    let off = 0,
        sps: Uint8Array | null = null,
        pps: Uint8Array | null = null,
        nals = 0;
    while (off + 4 <= payload.length) {
        const len =
            (payload[off] << 24) |
            (payload[off + 1] << 16) |
            (payload[off + 2] << 8) |
            payload[off + 3];
        if (len <= 0 || off + 4 + len > payload.length) break;
        const nal0 = payload[off + 4];
        const nalType = nal0 & 0x1f;
        const body = payload.subarray(off + 4, off + 4 + len);
        if (nalType === 7 && !sps) sps = body; // SPS
        if (nalType === 8 && !pps) pps = body; // PPS
        off += 4 + len;
        nals++;
        if (sps && pps) break;
    }
    return { sps, pps, nals };
}
function buildAvcCFromSpsPps(sps: Uint8Array, pps: Uint8Array) {
    const cfgLen = 7 + 2 + sps.length + 1 + 2 + pps.length;
    const out = new Uint8Array(cfgLen);
    let i = 0;
    out[i++] = 1; // configurationVersion
    out[i++] = sps[1]; // AVCProfileIndication
    out[i++] = sps[2]; // profile_compatibility
    out[i++] = sps[3]; // AVCLevelIndication
    out[i++] = 0xff; // 111111 + lengthSizeMinusOne(=3 -> 4字节)
    out[i++] = 0xe1; // 111 + numOfSPS(=1)
    out[i++] = (sps.length >> 8) & 0xff;
    out[i++] = sps.length & 0xff;
    out.set(sps, i);
    i += sps.length;
    out[i++] = 1; // numOfPPS
    out[i++] = (pps.length >> 8) & 0xff;
    out[i++] = pps.length & 0xff;
    out.set(pps, i);
    return out;
}
const toCodecStr = (sps: Uint8Array) =>
    `avc1.${[sps[1], sps[2], sps[3]]
        .map((b) => b.toString(16).toUpperCase().padStart(2, "0"))
        .join("")}`;

// ====== UI Pieces ======
function FieldLabel(props: React.PropsWithChildren) {
    return <div className="text-[0.875rem] leading-[1.25rem] text-black/80">{props.children}</div>;
}
function Button(
    props: React.ButtonHTMLAttributes<HTMLButtonElement> & { variant?: "solid" | "outline" }
) {
    const { className = "", variant = "solid", ...rest } = props;
    const base =
        "inline-flex items-center justify-center rounded-[0.5rem] px-[0.875rem] py-[0.5rem] text-[0.875rem] leading-[1rem]";
    const solid = "bg-black text-white hover:opacity-90 disabled:opacity-50";
    const outline = "border border-black/20 text-black hover:bg-black/5 disabled:opacity-50";
    const cls = `${base} ${variant === "solid" ? solid : outline} ${className}`;
    return <button className={cls} {...rest} />;
}

// ====== Main Component ======
export default function WSAvcCDialogDemo() {
    const [open, setOpen] = React.useState(false);
    const [url, setUrl] = React.useState("ws://127.0.0.1:9000/live");
    const [status, setStatus] = React.useState("未连接");
    const [canStart, setCanStart] = React.useState(true);
    const [canStop, setCanStop] = React.useState(false);
    const [logs, setLogs] = React.useState<string>("");
    const [statsLines, setStatsLines] = React.useState<string[]>([]);

    // 定位（以 rem 为单位）
    const [pos, setPos] = React.useState<{ leftRem: number; topRem: number }>({ leftRem: 0, topRem: 0 });
    const triggerRef = React.useRef<HTMLButtonElement | null>(null);

    const canvasRef = React.useRef<HTMLCanvasElement | null>(null);
    const logAreaRef = React.useRef<HTMLTextAreaElement | null>(null);

    const wsRef = React.useRef<WebSocket | null>(null);
    const decoderRef = React.useRef<VideoDecoder | null>(null as any);
    const avccRef = React.useRef<Uint8Array | null>(null);
    const needConfigRef = React.useRef(true);
    const codecRef = React.useRef("avc1.42E01E");

    // stats state (滚动统计窗口)
    const frameCountRef = React.useRef(0);
    const bytesWinRef = React.useRef<{ t: number; bytes: number }[]>([]);
    const latencyWinRef = React.useRef<number[]>([]);
    const inflightRef = React.useRef(0);
    const textMsgsRef = React.useRef(0);
    const binMsgsRef = React.useRef(0);
    const parsedFramesRef = React.useRef(0);
    const dropNoCfgRef = React.useRef(0);
    const dropMalformedRef = React.useRef(0);
    const decodeErrRef = React.useRef(0);
    const lastKeyAtMsRef = React.useRef<number | null>(null);
    const lastFrameInfoRef = React.useRef<{
        tsUs: number;
        key: boolean;
        bytes: number;
        nalCount?: number;
    } | null>(null);

    // 日志追加（修复换行 + 自动滚动）
    const log = React.useCallback((...a: any[]) => {
        const line = `[${new Date().toLocaleTimeString()}] ` + a.map(String).join(" ");
        setLogs((old) => (old ? old + "" : "") + line);
  }, []);
    React.useEffect(() => {
        const el = logAreaRef.current;
        if (el) {
            el.scrollTop = el.scrollHeight;
        }
    }, [logs]);

    // 计算并设置对齐位置（左上角对齐 Trigger 左上角）
    const measureAndPlace = React.useCallback(() => {
        const el = triggerRef.current;
        if (!el) return;
        const rect = el.getBoundingClientRect();
        const base = parseFloat(getComputedStyle(document.documentElement).fontSize) || 16;
        setPos({ leftRem: rect.left / base, topRem: rect.top / base });
    }, []);

    React.useLayoutEffect(() => {
        if (!open) return;
        measureAndPlace();
        let raf = 0;
        const onScrollOrResize = () => {
            cancelAnimationFrame(raf);
            raf = requestAnimationFrame(measureAndPlace);
        };
        window.addEventListener("scroll", onScrollOrResize, true);
        window.addEventListener("resize", onScrollOrResize);
        return () => {
            window.removeEventListener("scroll", onScrollOrResize, true);
            window.removeEventListener("resize", onScrollOrResize);
            cancelAnimationFrame(raf);
        };
    }, [open, measureAndPlace]);

    // 定时刷新统计（改为行数组）
    React.useEffect(() => {
        const id = window.setInterval(() => {
            const now = performance.now();
            bytesWinRef.current = bytesWinRef.current.filter((e) => now - e.t <= 2000);
            const bytes = bytesWinRef.current.reduce((s, e) => s + e.bytes, 0);
            const kbps = (bytes * 8) / 2000;
            let avgLat: number | null = null;
            if (latencyWinRef.current.length) {
                avgLat =
                    latencyWinRef.current.reduce((a, b) => a + b, 0) /
                    latencyWinRef.current.length;
                latencyWinRef.current.length = 0;
            }
            const lastKeyAgo =
                lastKeyAtMsRef.current == null
                    ? "—"
                    : Math.max(0, now - lastKeyAtMsRef.current).toFixed(0) + " ms";
            const lastF = lastFrameInfoRef.current
                ? `${lastFrameInfoRef.current.key ? "K" : "P"} ts=${lastFrameInfoRef.current.tsUs} len=${lastFrameInfoRef.current.bytes}B nal=${lastFrameInfoRef.current.nalCount ?? "?"}`
                : "—";
            const lines = [
                `状态: ${wsRef.current ? (wsRef.current.readyState === 1 ? "已连接" : "连接中") : "未连接"}`,
                `Decoder: ${decoderRef.current ? (decoderRef.current as any).state ?? "—" : "—"}`,
                `FPS: ${frameCountRef.current.toFixed(0)}`,
                `码率: ${kbps.toFixed(0)} kbps`,
                `延迟: ${avgLat == null ? "—" : avgLat.toFixed(1) + " ms"}`,
                `文本消息: ${textMsgsRef.current}`,
                `二进制消息: ${binMsgsRef.current}`,
                `已入解码: ${parsedFramesRef.current}`,
                `丢(未配置): ${dropNoCfgRef.current}`,
                `丢(格式错): ${dropMalformedRef.current}`,
                `解码错误: ${decodeErrRef.current}`,
                `待解码: ${inflightRef.current}`,
                `最近关键帧: ${lastKeyAgo}`,
                `最近帧: ${lastF}`,
                `avcC: ${avccRef.current ? avccRef.current.byteLength : 0} B`,
                `需配置: ${needConfigRef.current ? "是" : "否"}`,
                `codec: ${codecRef.current}`];
        setStatsLines(lines);
        frameCountRef.current = 0;
    }, 1000);
    return () => window.clearInterval(id);
}, []);

// 解码器
const ensureDecoder = React.useCallback(async () => {
    if (decoderRef.current) return;
    // @ts-ignore - Safari 早期可能没有 VideoDecoder
    if (!("VideoDecoder" in window)) {
        log("ERR: 当前浏览器不支持 WebCodecs");
        setStatus("浏览器不支持 WebCodecs");
        return;
    }
    const canvas = canvasRef.current;
    if (!canvas) return;
    const ctx = canvas.getContext("2d");
    if (!ctx) {
        log("ERR: Canvas 2D not available");
        return;
    }
    // @ts-ignore
    decoderRef.current = new VideoDecoder({
        output: (frame: VideoFrame) => {
            if (
                canvas.width !== frame.codedWidth ||
                canvas.height !== frame.codedHeight
            ) {
                canvas.width = frame.codedWidth;
                canvas.height = frame.codedHeight;
                log("resize canvas", `${frame.codedWidth}x${frame.codedHeight}`);
            }
            // @ts-ignore
            ctx.drawImage(frame, 0, 0);
            frame.close();
            frameCountRef.current++;
            inflightRef.current = Math.max(0, inflightRef.current - 1);
        },
        error: (e: any) => {
            decodeErrRef.current++;
            log("DECODER ERROR:", e);
        },
    });
    log("decoder created");
}, [log]);

const configureDecoder = React.useCallback(async () => {
    const decoder = decoderRef.current;
    if (!decoder) await ensureDecoder();
    if (!decoderRef.current) return;
    if (!avccRef.current) {
        log("ERR: No avcC config yet");
        return;
    }
    try {
        decoderRef.current.configure({
            codec: codecRef.current,
            description: avccRef.current.buffer.slice(
                avccRef.current.byteOffset,
                avccRef.current.byteOffset + avccRef.current.byteLength
            ),
        } as any);
        needConfigRef.current = false;
        log(
            "decoder configured",
            `codec=${codecRef.current}`,
            `avcc=${avccRef.current.byteLength}B`
        );
    } catch (e) {
        log("ERR: configure failed", e);
        throw e;
    }
}, [ensureDecoder, log]);

// 处理 WS 消息
const onMessage = React.useCallback(
    (ev: MessageEvent) => {
        if (typeof ev.data === "string") {
            textMsgsRef.current++;
            try {
                const obj = JSON.parse(ev.data);
                if (obj.type === "config") {
                    if (obj.codec) {
                        codecRef.current = obj.codec;
                        log("cfg codec=", obj.codec);
                    }
                    if (!obj.avccBase64) {
                        log("ERR: config missing avccBase64");
                        return;
                    }
                    avccRef.current = Uint8Array.from(atob(obj.avccBase64), (c) =>
                        c.charCodeAt(0)
                    );
                    needConfigRef.current = true;
                    log("cfg avcC bytes=", avccRef.current.byteLength);
                    configureDecoder();
                } else {
                    log("text msg:", ev.data.slice(0, 120));
                }
            } catch {
                log("text msg (non-JSON):", (ev.data as string).slice(0, 120));
            }
            return;
        }

        // 二进制
        binMsgsRef.current++;
        const buf = ev.data as ArrayBuffer;
        const v = new DataView(buf);

        let isKey = false,
            tsUs = 0,
            payload: Uint8Array | null = null;

        // 14 字节头格式
        if (v.byteLength >= 14 && v.getUint8(0) === 0x02) {
            const len = beU32(v, 10);
            if (14 + len > v.byteLength) {
                dropMalformedRef.current++;
                log(
                    "drop: malformed, declared len=",
                    len,
                    " actual=",
                    v.byteLength - 14
                );
                return;
            }
            isKey = v.getUint8(1) !== 0;
            tsUs = beU64(v, 2);
            payload = new Uint8Array(buf, 14, len);

            const nowUs = performance.timeOrigin * 1000 + performance.now() * 1000;
            const latMs = (nowUs - tsUs) / 1000;
            if (latMs >= 0 && latMs < 60000) {
                latencyWinRef.current.push(latMs);
                if (latencyWinRef.current.length > 120) latencyWinRef.current.shift();
            }
            bytesWinRef.current.push({ t: performance.now(), bytes: len + 14 });
        } else {
            // 裸 AvcC
            payload = new Uint8Array(buf);
            if (payload.byteLength >= 5) {
                const nalHdr = payload[4];
                const nalType = nalHdr & 0x1f;
                isKey = nalType === 5;
            }
            tsUs = Math.floor(performance.timeOrigin * 1000 + performance.now() * 1000);
            bytesWinRef.current.push({ t: performance.now(), bytes: payload.byteLength });
        }

        // 若尚未配置 avcC，则尝试自举
        if (!avccRef.current && payload) {
            const { sps, pps, nals } = extractSpsPpsFromAvcC(payload);
            log("bootstrap: probe nals=", nals, " sps=", !!sps, " pps=", !!pps);
            if (sps && pps) {
                codecRef.current = toCodecStr(sps);
                avccRef.current = buildAvcCFromSpsPps(sps, pps);
                needConfigRef.current = true;
                log(
                    "bootstrap: codec=",
                    codecRef.current,
                    " avcC=",
                    avccRef.current.byteLength,
                    "B"
                );
                configureDecoder();
            }
        }

        // 快速校验
        if (payload) {
            const probe = (function (p: Uint8Array) {
                let off = 0,
                    n = 0;
                while (off + 4 <= p.length) {
                    const len =
                        (p[off] << 24) |
                        (p[off + 1] << 16) |
                        (p[off + 2] << 8) |
                        p[off + 3];
                    if (len <= 0 || off + 4 + len > p.length)
                        return { ok: false, n, badAt: off, len } as const;
                    off += 4 + len;
                    n++;
                }
                return { ok: off === p.length, n } as const;
            })(payload);
            lastFrameInfoRef.current = {
                tsUs,
                key: isKey,
                bytes: payload.byteLength,
                nalCount: probe.n,
            };
            if (!probe.ok) log("warn: avcC NAL parse fail at", probe.badAt, " len=", probe.len, " payload=", payload.byteLength);
            if (isKey) lastKeyAtMsRef.current = performance.now();
        }

        if (needConfigRef.current) {
            dropNoCfgRef.current++;
            log("drop: need avcC config before frames");
            return;
        }

        try {
            inflightRef.current++;
            // @ts-ignore
            const chunk = new EncodedVideoChunk({
                type: isKey ? "key" : "delta",
                timestamp: tsUs,
                data: payload!,
            });
            // @ts-ignore
            decoderRef.current!.decode(chunk);
            parsedFramesRef.current++;
        } catch (e) {
            decodeErrRef.current++;
            inflightRef.current = Math.max(0, inflightRef.current - 1);
            log("ERR: decode()", e);
        }
    },
    [configureDecoder, log]
);

// 连接 / 断开
const handleStart = React.useCallback(async () => {
    if (wsRef.current && wsRef.current.readyState === 1) return;
    log("connecting", url);
    setStatus("连接中…");
    await ensureDecoder();
    const ws = new WebSocket(url);
    ws.binaryType = "arraybuffer";
    ws.onopen = () => {
        setStatus("已连接");
        setCanStart(false);
        setCanStop(true);
        log("ws open");
    };
    ws.onclose = () => {
        setStatus("未连接");
        setCanStart(true);
        setCanStop(false);
        log("ws close");
        try {
            decoderRef.current && decoderRef.current.close();
        } catch { }
        decoderRef.current = null as any;
        avccRef.current = null;
        needConfigRef.current = true;
    };
    ws.onerror = (e: any) => {
        log("ws error", e?.message || e);
    };
    ws.onmessage = onMessage;
    wsRef.current = ws;
}, [ensureDecoder, log, onMessage, url]);

const handleStop = React.useCallback(() => {
    if (wsRef.current) {
        log("ws close (manual)");
        wsRef.current.close();
        wsRef.current = null;
    }
}, [log]);

const handleClear = React.useCallback(() => setLogs(""), []);
const handleCopy = React.useCallback(() => {
    navigator.clipboard.writeText(logs).catch(() => { });
}, [logs]);

// 清理
React.useEffect(() => {
    return () => {
        try {
            wsRef.current?.close();
        } catch { }
        try {
            decoderRef.current?.close();
        } catch { }
    };
}, []);

return (
    <div className="p-[1rem] font-sans text-[1rem] leading-[1.5rem] text-[#111]">
        {/* 唤醒按钮 */}
        <Dialog.Root open={open} onOpenChange={setOpen} modal={false}>
            <Dialog.Trigger asChild>
                <button ref={triggerRef} className="inline-flex items-center justify-center rounded-[0.5rem] bg-black px-[1rem] py-[0.625rem] text-white hover:opacity-90">
                    打开 WS AvcC 播放器（非模态/左上角对齐）
                </button>
            </Dialog.Trigger>

            {/* 非模态：不使用 Overlay；内容 fixed 定位到 Trigger 左上角 */}
            <Dialog.Content
                onInteractOutside={(e) => e.preventDefault()}
                onEscapeKeyDown={(e) => e.preventDefault()}
                className="fixed z-[100] w-[56rem] max-w-[90vw] rounded-[0.75rem] bg-white shadow-[0_0.5rem_1.5rem_rgba(0,0,0,0.12)] focus:outline-none"
                style={{ left: `${pos.leftRem}rem`, top: `${pos.topRem}rem` }}
            >
                {/* Header */}
                <div className="flex items-center justify-between rounded-t-[0.75rem] bg-[#F8F8F8] px-[1rem] py-[0.75rem]">
                    <div className="text-[1rem] font-medium">WebSocket AvcC Player</div>
                    <Dialog.Close asChild>
                        <button
                            aria-label="Close"
                            className="inline-flex h-[2rem] w-[2rem] items-center justify-center rounded-[0.5rem] hover:bg-black/5"
                        >
                            <svg viewBox="0 0 24 24" className="h-[1.25rem] w-[1.25rem]" aria-hidden>
                                <path d="M6 6l12 12M18 6 6 18" stroke="currentColor" strokeWidth="2" strokeLinecap="round" />
                            </svg>
                        </button>
                    </Dialog.Close>
                </div>

                {/* Body */}
                <div className="p-[1rem]">
                    {/* 控件行 */}
                    <div className="flex flex-wrap items-center gap-[0.5rem]">
                        <label className="flex items-center gap-[0.5rem]">
                            <FieldLabel>URL:</FieldLabel>
                            <input
                                value={url}
                                onChange={(e) => setUrl(e.target.value)}
                                className="min-w-[24rem] rounded-[0.5rem] border border-black/20 px-[0.75rem] py-[0.5rem] text-[0.875rem] leading-[1.25rem] outline-none focus:border-black/40"
                                type="text"
                            />
                        </label>
                        <Button onClick={handleStart} disabled={!canStart} className="px-[0.875rem] py-[0.5rem]">开始</Button>
                        <Button onClick={handleStop} disabled={!canStop} variant="outline" className="px-[0.875rem] py-[0.5rem]">停止</Button>
                        <span className="text-[0.875rem] leading-[1.25rem] text-black/60">{status}</span>
                    </div>

                    {/* 主体网格 */}
                    <div className="mt-[0.75rem] grid grid-cols-[2fr_1fr] gap-[0.75rem]">
                        <div className="rounded-[0.5rem] border border-black/10 bg-white p-[0.5rem]">
                            <canvas ref={canvasRef} className="max-w-full bg-white" />
                        </div>
                        <div className="rounded-[0.5rem] border border-black/10 bg-white p-[0.75rem]">
                            <div className="text-[1rem] font-semibold leading-[1.5rem]">实时统计</div>
                            {/* 键值两列布局 */}
                            <ul className="mt-[0.5rem] divide-y divide-black/5 rounded-[0.5rem] border border-black/10">
                                {statsLines.map((line, i) => {
                                    const idx = line.indexOf(":");
                                    const k = idx >= 0 ? line.slice(0, idx) : line;
                                    const v = idx >= 0 ? line.slice(idx + 1).trim() : "";
                                    return (
                                        <li key={i} className="grid grid-cols-[7rem_1fr] items-start gap-[0.5rem] px-[0.5rem] py-[0.375rem] text-[0.8125rem] leading-[1.25rem]">
                                            <span className="text-black/70">{k}</span>
                                            <span className="font-mono">{v}</span>
                                        </li>
                                    );
                                })}
                            </ul>
                            <div className="mt-[0.5rem] text-[0.75rem] leading-[1.125rem] text-black/60">
                                * 若后端不发 config，前端会从首包 AvcC 自动提取 SPS/PPS 构造 avcC。
                                <br />* 文本 config（可选）：{"{"}"type":"config","codec":"avc1.640028","avccBase64":"..."{"}"}
                                <br />* 二进制：支持 ①14 字节头 (0x02+key+ts+len+payload) ②裸 AvcC（4 字节大端长度 + NAL…）
                            </div>
                        </div>
                    </div>

                    {/* 日志 */}
                    <div className="mt-[0.75rem] rounded-[0.5rem] border border-black/10 bg-white p-[0.75rem]">
                        <div className="mb-[0.5rem] flex items-center justify-between">
                            <div className="text-[1rem] font-semibold leading-[1.5rem]">日志</div>
                            <div className="flex items-center gap-[0.5rem]">
                                <Button variant="outline" onClick={handleClear} className="px-[0.875rem] py-[0.5rem]">清空</Button>
                                <Button variant="outline" onClick={handleCopy} className="px-[0.875rem] py-[0.5rem]">复制</Button>
                            </div>
                        </div>
                        <textarea
                            ref={logAreaRef}
                            readOnly
                            value={logs}
                            className="h-[16rem] w-full resize-none rounded-[0.5rem] border border-black/10 p-[0.5rem] text-[0.75rem] leading-[1.125rem] outline-none font-mono"
                        />
                    </div>
                </div>
            </Dialog.Content>
        </Dialog.Root>
    </div>
);
}
