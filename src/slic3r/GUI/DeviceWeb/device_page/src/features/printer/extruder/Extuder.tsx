import { RadioGroup } from "radix-ui";
import { TempInput } from "../../../components/TempInput";
import Image from "../../../components/Image";
import ImageButton from "../../../components/ImageButton";


export default function Extuder() {
    return (
        <div className="flex flex-col">
            <div className="flex flex-row justify-between items-center">
                <h1>Extuder & Nozzle</h1>
                <p >Nozzle Settings</p>
            </div>

            <div className="flex flex-row items-center justify-between">
                <div className="flex flex-col">
                    <p>Left Nozzle</p>
                    <TempInput />
                    <p>Nozzle Diameter & Flow</p>
                </div>

                <div className="flex flex-col items-center justify-between">
                    <ImageButton icon="up" hover="up" active="up" />
                    <ImageButton icon="down" hover="down" active="down" />
                </div>
                <Image src="extuder.svg" alt="Nozzle" />
                <Image src="extuder.svg" alt="Nozzle" />

                <div className="flex flex-col items-center justify-between">
                    <ImageButton icon="up" hover="up" active="up" />
                    <ImageButton icon="down" hover="down" active="down" />
                </div>

                <div className="flex flex-col">
                    <p>Right Nozzle</p>
                    <TempInput />
                    <p>Nozzle Diameter & Flow</p>
                </div>
            </div>

            <div className="flex flex-row items-center justify-evenly">
                <RadioGroup.Root className="flex flex-row gap-2.5"
                    defaultValue="default"
                >
                    <div className="flex items-center">
                        <RadioGroup.Item className="" value="default" id="r1">
                            <RadioGroup.Indicator className="relative flex size-full items-center justify-center after:block after:size-[11px] after:rounded-full after:bg-violet11" />
                        </RadioGroup.Item>
                        <label htmlFor="r1">
                            Default
                        </label>
                    </div>
                    <div className="flex items-center">
                        <RadioGroup.Item className="" value="comfortable" id="r2" >
                            <RadioGroup.Indicator className="relative flex size-full items-center justify-center after:block after:size-[11px] after:rounded-full after:bg-violet11" />
                        </RadioGroup.Item>
                        <label htmlFor="r2" >
                            Comfortable
                        </label>
                    </div>
                </RadioGroup.Root>
            </div>
        </div>
    );
}