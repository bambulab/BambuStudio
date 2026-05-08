/* params: ams-id slot-id */
export default function AMSPreview() {
    const is_ext_ams = true;
    const width = 0.75 + 0.375 * 4 + 0.1875 * 3;

    return (
        <button className=" border-2 rounded-md border-transparent hover:border-green-500 transition-colors" >
            <div className={`relative h-[1.5rem] m-[0.25rem]`}  style={{ width: `${width}rem` }}>
                <div className="absolute top-0 bottom-0 left-[0.125rem] right-[0.125rem] rounded-md bg-[#EEEEEE]" />

                {/* ext ams invisible */}
                {is_ext_ams && <div className="absolute bottom-0 left-0 right-0 h-[0.75rem] bg-[#CECECE]" />}

                {/* slots*/}
                <div className="absolute top-[0.375rem] left-[0.375rem] right-[0.375rem] h-[0.75rem] flex flex-row justify-between items-center ">
                    <div className="w-[0.375rem] aspect-[1/2] rounded-full bg-green-200" />
                    <div className="w-[0.375rem] aspect-[1/2] rounded-full bg-red-200" />
                    <div className="w-[0.375rem] aspect-[1/2] rounded-full bg-blue-200" />
                    <div className="w-[0.375rem] aspect-[1/2] rounded-full bg-gray-200" />
                </div>
            </div>
        </button>
    );
}
