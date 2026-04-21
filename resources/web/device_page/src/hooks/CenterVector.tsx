import { useCallback, useLayoutEffect, useRef, useState } from 'react';

export type Center = { x: number; y: number };
export type CentersVector = {
    dx: number; dy: number;
    distance: number;
    angleRad: number; angleDeg: number;
    fromCenter: Center; toCenter: Center;
};

type Options = {
    scrollTargets?: (Element | Window)[];
};


export function useCenterVectors<
    K extends number|string,
    TFrom extends HTMLElement = HTMLElement,
    TTo extends HTMLElement = HTMLElement
>(opts: Options = {}) {
    const { scrollTargets = [window] } = opts;

    const fromRef = useRef<TFrom | null>(null);
    const toMapRef = useRef(new Map<K, TTo>());
    const roRef = useRef<ResizeObserver | null>(null);

    const [vectors, setVectors] = useState<Map<K, CentersVector>>(new Map());

    // 计算一次
    const measureNow = useCallback(() => {
        const fromEl = fromRef.current;
        if (!fromEl) return;

        const fr = fromEl.getBoundingClientRect();
        const fx = fr.left; // + fr.width / 2;
        const fy = fr.top; // + fr.height / 2;

        const next = new Map<K, CentersVector>();
        for (const [key, el] of toMapRef.current) {
            if (!el) continue;
            const tr = el.getBoundingClientRect();
            const tx = tr.left + tr.width / 2;
            const ty = tr.top + tr.height / 2;
            const dx = tx - fx;
            const dy = ty - fy;
            const angleRad = Math.atan2(dy, dx);
            next.set(key, {
                dx, dy,
                distance: Math.hypot(dx, dy),
                angleRad,
                angleDeg: (angleRad * 180) / Math.PI,
                fromCenter: { x: fx, y: fy },
                toCenter: { x: tx, y: ty },
            });
        }
        setVectors(next);
    }, []);

    // 用 rAF 节流（滚动/resize 高频）
    const rafId = useRef<number | null>(null);
    const schedule = useCallback(() => {
        if (rafId.current != null) return;
        rafId.current = requestAnimationFrame(() => {
            rafId.current = null;
            measureNow();
        });
    }, [measureNow]);

    // 供外部把终点绑定到 key
    const getToRef = useCallback(
        (key: K) => (el: TTo | null) => {
            const prev = toMapRef.current.get(key);
            if (prev && roRef.current) roRef.current.unobserve(prev);
            if (el) {
                toMapRef.current.set(key, el);
                if (roRef.current) roRef.current.observe(el);
            } else {
                toMapRef.current.delete(key);
            }
            schedule(); // 结构变化时重测
        },
        [schedule]
    );

    useLayoutEffect(() => {
        // 初始化 ResizeObserver：from + 所有 to
        const ro = new ResizeObserver(schedule);
        roRef.current = ro;
        if (fromRef.current) ro.observe(fromRef.current);
        for (const el of toMapRef.current.values()) ro.observe(el);

        // 监听滚动与窗口变化
        const targets = Array.from(new Set([window, ...scrollTargets]));
        for (const t of targets) t.addEventListener('scroll', schedule, { passive: true });
        window.addEventListener('resize', schedule);

        // 首次测量
        schedule();

        return () => {
            ro.disconnect();
            roRef.current = null;
            for (const t of targets) t.removeEventListener('scroll', schedule);
            window.removeEventListener('resize', schedule);
            if (rafId.current != null) cancelAnimationFrame(rafId.current);
        };
    }, [schedule, scrollTargets]);

    return {
        fromRef,
        getToRef,
        vectors,
        measure: measureNow,
    };
}
