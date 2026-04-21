import Image from "../../../components/Image";
import { TempInput } from "../../../components/TempInput";

export default function HeatbedControl() {
    return (
        <button type="button">
            <div className="flex flex-row items-center">
                <Image src="heatbed.svg" />
                <div className="flex flex-col">
                    <p>Heatbed {">"}</p>
                    <TempInput />
                </div>
            </div>

        </button>
    );
}