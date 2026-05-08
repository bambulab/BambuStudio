import Image from "../../../components/Image";
import AirControl from "./Air";
import MotionControl from "./Motion";
import SpeedControl from "./Speed";
import ChamberControl from "./Chamber";
import HeatbedControl from "./Heatbed";
import NozzleExtruderControl from "./NozzleExtruder";

export default function MachineControl() {
    return (
        <div className="flex flex-col">
            <div className="flex flex-row items-center justify-between">
                <AirControl />
                <SpeedControl />
                <MotionControl />
            </div>
            <div className="flex flex-row">
                <div className="flex flex-col">
                    <NozzleExtruderControl/>
                    <ChamberControl/>
                    <HeatbedControl/>
                </div>

                <Image src="machine.svg" />
            </div>
        </div>
    );
}