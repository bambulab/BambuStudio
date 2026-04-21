
import BambuSlotItem from "./BambuSlot";


export default function AMSLite() {

    return (
        <div className="relative w-[10.25rem] h-[13rem]">

            <svg viewBox="0 0 56 158" fill="none" className="absolute top-[1.375rem] left-1/2 -translate-x-1/2 w-[3.5rem] h-[9.875rem]">
                <rect x="18" width="20" height="158" rx="10" fill="#DBDBDB" />
                <path d="M38 17L56 19V32L38 34V17Z" fill="#C2C2C2" />
                <path d="M18 17L0 19V32L18 34V17Z" fill="#C2C2C2" />
                <path d="M38 123L56 125V138L38 140V123Z" fill="#C2C2C2" />
                <path d="M18 123L0 125V138L18 140V123Z" fill="#C2C2C2" />
            </svg>

            <BambuSlotItem percent={10} className="absolute top-0 left-[0.2rem]" />
            <BambuSlotItem percent={10} className="absolute top-0 right-[0.2rem]" />
            <BambuSlotItem percent={10} className="absolute top-[6.875rem] left-[0.2rem]" />
            <BambuSlotItem percent={10} className="absolute top-[6.875rem] right-[0.2rem]" />

        </div>
    );
}
