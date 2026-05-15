import Image from "../../../components/Image";
import LinkButton from "./LinkButton";
import WifiIcon from "../../../components/WifiIcon";

export default function MachineSetting() {

  return (
    <div className="flex flex-col">
      <div className="flex flex-row">
        <Image src="machine_icon.svg" alt="machine icon" />

        <div className="flex flex-col">
          <div className="flex flex-row">
            <div className="">Machine Name </div>
            <WifiIcon level={2} className="h-5 w-5 text-black" />
          </div>

          <div className="flexflex-row">
            <p> machine type | nozzle info</p>
          </div>
        </div>

        <div className="ml-auto flex flex-col">
          <p className="md-auto"> setting icon</p>
        </div>

      </div>

      <div className="flex flex-row w-full items-center justify-between ">
        <LinkButton icon="assistant.svg" text="Assistant" />
        <LinkButton icon="assistant.svg" text="Timelapse" />
        <LinkButton icon="assistant.svg" text="File" />
      </div>

    </div>
  );
}