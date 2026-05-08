import Image from "../../../components/Image";

export default function SpeedControl() {
    return (
        <button type="button">
            <div className="flex flex-row items-center">
                <Image src="speed.svg" />
                <div className="flex flex-col">
                    <p>Speed {">"}</p>
                    <p>Standard</p>
                </div>
            </div>


        </button>
    );
}