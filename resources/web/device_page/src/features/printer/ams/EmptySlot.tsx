export default function EmptySlotItem(){

    return (
        <div className="flex flex-col">
            <div className="flex items-center justify-center w-[3.25rem] h-[0.5rem]"/>

            <div className={`relative w-[3.25rem] h-[5rem]`}>
                {/* 左侧灰色竖条 */}
                <div className="absolute left-0 top-0 w-[0.3125rem] h-[5rem] bg-gray-200 rounded-[2.5rem]" />

                {/* 右侧灰色竖条 */}
                <div className="absolute right-0 top-0 w-[0.3125rem] h-[5rem] bg-gray-200 rounded-[2.5rem]" />

                {/* 背景灰色卡片 */}
                <div className="absolute left-[0.3125rem] right-[0.3125rem] top-[0.5rem] bottom-[0.5rem] bg-gray-200 border-gray-200 z-0" />

                {/* 主体红色卡片 */}
                <div className="absolute left-[0.3125rem] right-[0.3125rem] top-[0.625rem] bottom-[0.625rem] bg-white border border-gray-200" />

                <div className="absolute left-[0.3125rem] right-[0.3125rem] top-[0.625rem] bottom-[0.625rem] flex flex-col justify-center items-center">
                    <p className="slot-id-text"> hello</p>
                    <p className="slot-filament-text"> Empty</p>
                    <div className="w-[1.125rem] aspect-square"/>
                </div>

            </div>
        </div >
    );
};