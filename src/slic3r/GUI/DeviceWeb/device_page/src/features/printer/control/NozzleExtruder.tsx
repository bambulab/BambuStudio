import Image from "../../../components/Image"

export default function NozzleExtruderControl() {
    const extruder_switch = false;

    return (
        <div className="flex flex-col">
            <div className="flex flex-row">

            </div>
            <div className="flex flex-row">
                <div className="flex flex-col">
                    <p>current temp</p>
                    <p>/target temp</p>
                </div>

                {extruder_switch ? <Image src="nozzle_extuder_left_active.svg" /> : <Image src="nozzle_extuder_right_active.svg" />}

                <div className="flex flex-col">
                    <p>current temp</p>
                    <p>/target temp</p>
                </div>

            </div>

        </div>);
}