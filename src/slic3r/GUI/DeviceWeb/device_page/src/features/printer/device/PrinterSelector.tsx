import ImageButton from "../../../components/ImageButton";
import ScrollView from "../../../components/ScrollView";


function PrinterSelector() {

  return (
    <div className="flex-col">
        <ScrollView>
          <div className="flex flex-row items-center">
            <ImageButton icon="device_add.svg" className="mr-auto"/>
            <ImageButton icon="device_layout.svg"/>
            <ImageButton icon="device_query.svg"/>
          </div>

          <div className="flex flex-col">

          </div>

        </ScrollView>
    </div>
  )
}

export default PrinterSelector;