import Image from "../../../components/Image";

interface LinkButtonProps {
    icon: string;
    text: string;
}

export default function LinkButton({ icon, text }: LinkButtonProps) {
    return (
        <button className="left-[1.75rem] top-[6rem] flex h-[3.5rem] w-[10.6875rem] items-center justify-center rounded-[0.625rem] bg-[#F7F7F7]">
            <div className="flex flex-row items-center">
                <Image src={icon} alt={text} className="h-[1.75rem] w-[1.75rem]" />

                <p className="text-[0.875rem] leading-[1.375rem] font-normal text-[#262E30]">
                    {text}
                </p>
            </div>
        </button>
    );
}