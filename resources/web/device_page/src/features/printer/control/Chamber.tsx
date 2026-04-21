import Image from "../../../components/Image";
import { TempInput } from "../../../components/TempInput";

export default function ChamberControl() {
    return (
        <button type="button">
            <div className="flex flex-row items-center">
                <Image src="chamber.svg" />
                <div className="flex flex-col">
                    <p>Chamber {">"}</p>
                    <TempInput />
                </div>
            </div>

        </button>
    );
}