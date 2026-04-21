import Image from "../../../components/Image";
import ProgressBar from "../../../components/ProgressBar";

interface BambuSlotItemProps{
    percent: number;
    slotRef?: React.Ref<HTMLDivElement>;
    className?: string;
}

export default function BambuSlotItem({ percent, slotRef, className}: BambuSlotItemProps) {

    return (
        <div ref={slotRef} className={`flex flex-col ${className}`}>

            <div className="flex items-center justify-center w-[3.25rem] h-[0.5rem]">
                <ProgressBar percent={percent} fg_color="bg-[#FF8181]" className="h-[0.25rem] w-[1.75rem]" />
            </div>

            <div className={`relative w-[3.25rem] h-[5rem]`}>
                {/* 左侧灰色竖条 */}
                <div className="absolute left-0 top-0 w-[0.3125rem] h-[5rem] bg-gray-200 rounded-[2.5rem]" />

                {/* 右侧灰色竖条 */}
                <div className="absolute right-0 top-0 w-[0.3125rem] h-[5rem] bg-gray-200 rounded-[2.5rem]" />

                {/* 背景灰色卡片 */}
                <div className="absolute left-[0.3125rem] right-[0.3125rem] top-[0.5rem] bottom-[0.5rem] bg-gray-200 border-gray-200 z-0" />

                {/* 主体红色卡片 */}
                <div className="absolute left-[0.3125rem] right-[0.3125rem] top-[0.625rem] bottom-[0.625rem] bg-red-300 border border-gray-200" />

                <div className="absolute left-[0.3125rem] right-[0.3125rem] top-[0.625rem] bottom-[0.625rem] flex flex-col justify-center items-center">
                    <p className="slot-id-text"> hello</p>
                    <p className="slot-filament-text"> world</p>
                    {/* 底部徽章样式的图形 (简化表示) */}
                    <Image src="slot_icon.svg" alt="slot_icon" />
                </div>

            </div>
        </div >
    );
};