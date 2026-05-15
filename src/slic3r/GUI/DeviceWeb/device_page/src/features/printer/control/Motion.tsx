import Image from "../../../components/Image";

export default function MotionControl() {
    return (
        <button type="button">
            <div className="flex flex-row items-center">
                <Image src="motion.svg" />
                <div className="flex flex-col">
                    <p>Motion {">"}</p>
                    <p>XYZ</p>
                </div>
            </div>


        </button>
    );
}