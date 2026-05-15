import Image from "../../../components/Image";

export default function AirControl() {
    return (
        <button type="button">
            <div className="flex flex-row items-center">
                <Image src="air.svg" />
                <div className="flex flex-col">
                    <p>Air Condition {">"}</p>
                    <p>Cooling</p>
                </div>
            </div>


        </button>
    );
}