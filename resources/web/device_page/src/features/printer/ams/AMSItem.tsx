
import { useState, useRef, useLayoutEffect } from "react";
import { useRafCallback } from "../../../hooks/Raf";
import { CenterRelativePosition, BottumCenterRelativePosition, LeftCenterRelativePosition, RightCenterRelativePosition } from "../../../utils/Position";
import BambuSlotItem from "./BambuSlot";


export default function AMSItem() {
    const originRef = useRef<HTMLDivElement | null>(null);

    const slotRef0 = useRef<HTMLDivElement | null>(null);
    const slotRef1 = useRef<HTMLDivElement | null>(null);
    const slotRef2 = useRef<HTMLDivElement | null>(null);
    const slotRef3 = useRef<HTMLDivElement | null>(null);

    const portRef = useRef<HTMLDivElement | null>(null);

    const [path, setPath] = useState("");
    const [selPath, setSelPath] = useState("");
    const [viewbox, setViewbox] = useState("0 0 262 168");

    const computePath = () => {
        const [dx0, dy0] = CenterRelativePosition(originRef, slotRef0);
        const [dx1, dy1] = CenterRelativePosition(originRef, slotRef1);
        const [dx2, dy2] = CenterRelativePosition(originRef, slotRef2);
        const [dx3, dy3] = CenterRelativePosition(originRef, slotRef3);

        const [plx, ply] = LeftCenterRelativePosition(originRef, portRef);
        const [prx, pry] = RightCenterRelativePosition(originRef, portRef);
        const [pbx, pby] = BottumCenterRelativePosition(originRef, portRef);
        const [odx, ody] = BottumCenterRelativePosition(originRef, originRef);

        setPath(`M ${dx0} ${dy0} V ${ply}  H ${plx}`
                + `M ${dx1} ${dy1} V ${ply}  H ${plx}`
                + `M ${dx2} ${dy2} V ${pry}  H ${prx}`
                + `M ${dx3} ${dy3} V ${pry}  H ${prx}`
                + `M ${pbx} ${pby} V ${ody}  H ${odx}`);

        const sel: number = 0;
        switch(sel){
            case 0: setSelPath(`M ${dx0} ${dy0} V ${ply}  H ${plx} M ${pbx} ${pby} V ${ody} H ${odx} `); break;
            case 1: setSelPath(`M ${dx1} ${dy1} V ${ply}  H ${plx} M ${pbx} ${pby} V ${ody} H ${odx} `); break;
            case 2: setSelPath(`M ${dx2} ${dy2} V ${pry}  H ${prx} M ${pbx} ${pby} V ${ody} H ${odx} `); break;
            case 3: setSelPath(`M ${dx3} ${dy3} V ${pry}  H ${prx} M ${pbx} ${pby} V ${ody} H ${odx} `); break;
            default: setSelPath(""); break;
        }

        if (originRef.current) {
            const rect = originRef.current.getBoundingClientRect();
            setViewbox(`0 0 ${rect.width} ${rect.height}`);
        }
    }

    const scheduleRecompute = useRafCallback(computePath);

    useLayoutEffect(() => {
        scheduleRecompute();
        window.addEventListener("resize", scheduleRecompute, { passive: true });
        window.addEventListener("scroll", scheduleRecompute, { passive: true });

        const ro = new ResizeObserver(scheduleRecompute);
        [
            originRef.current,
            slotRef0.current,
            slotRef1.current,
            slotRef2.current,
            slotRef3.current,
            portRef.current,
        ].filter(Boolean).forEach((n) => ro.observe(n!));

        return () => {
            window.removeEventListener("resize", scheduleRecompute);
            window.removeEventListener("scroll", scheduleRecompute);
            ro.disconnect();
        };
    }, []);

    return (
        <div ref={originRef} className="relative w-[16.375rem] h-[10.5rem]">
            {/* layer 0 */}
            <div className="absolute inset-x-0 top-[1rem] bottom-0 bg-[rgb(248,248,248)] rounded-md" />

            {/* layer 1 */}
            <div className="absolute top-0 left-1/2 -translate-x-1/2  w-[9.3125rem] h-[2rem] bg-white rounded-full flex flex-row">
                <p>AMS-A</p>
            </div>

            <div className="absolute inset-x-0 bottom-0  w-full h-[1.75rem] bg-[rgb(235,235,235)] rounded-b-md/" />

            {/* layer 2 */}
            <svg viewBox={viewbox} className="absolute w-full h-full">
                <path d={path} fill="none" stroke="#111" strokeWidth="2" />
            </svg>

            {
                selPath.length > 0 &&
                <svg viewBox={viewbox} className="absolute w-full h-full">
                    <path d={selPath} fill="none" stroke="#ABAB00" strokeWidth="6" />
                </svg>
            }

            {/* layer 3 */}
            <div className="absolute top-0 left-1/2 -translate-x-1/2  w-[9.3125rem] h-[2rem] bg-white rounded-full flex flex-row">
                <p>dry</p>
            </div>

            <div ref={portRef} className="absolute bottom-[0.5rem] left-1/2 -translate-x-1/2 w-[1.75rem] h-[0.75rem] bg-[rgb(194,194,194)]" />

            {/* layer 5 */}
            <div className="absolute top-[2.75rem] left-[1rem] right-[1rem] flex flex-row justify-evenly items-center">
                <BambuSlotItem slotRef={slotRef0} percent={10} />
                <BambuSlotItem slotRef={slotRef1} percent={20} />
                <BambuSlotItem slotRef={slotRef2} percent={50} />
                <BambuSlotItem slotRef={slotRef3} percent={70} />
            </div>
        </div>
    );
}