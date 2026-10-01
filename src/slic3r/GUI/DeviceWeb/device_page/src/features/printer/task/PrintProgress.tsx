import Image from "../../../components/Image";
import ProgressBar from "../../../components/ProgressBar";

export default function PrintProgress() {
    return (
        <div className="flex flex-row w-full h-full">
            <Image className="h-full aspect-square" src="" alt="progress icon" />
            <div className="flex-1 flex flex-col ">
                <p> task name </p>

                <div className='mt-auto flex flex-row'>

                    <div className='flex-1 flex flex-col'>
                        <div className="flex flex-row">
                            <p>percent</p>
                            <p>task status</p>
                            <p className='ml-auto'>time remaining</p>
                        </div>
                        <ProgressBar percent={50} fg_color="bg-green-500" className="h-[0.5rem] w-full" />
                        <div className='flex flex-row'>
                            <p>remain layers</p>
                            <p className="ml-auto">left time</p>
                        </div>
                    </div>

                    <div className="ml-auto flex flex-row">
                        <button>t{"pause"}</button>
                        <button>cancel</button>
                    </div>
                </div>
            </div>
        </div>
    );
}