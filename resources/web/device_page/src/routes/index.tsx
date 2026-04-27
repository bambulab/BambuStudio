import { createFileRoute } from '@tanstack/react-router'

import PrinterSelector from '../features/printer/device/PrinterSelector'
import ScrollView from '../components/ScrollView'
import WSAvcCDialogDemo from '../features/printer/liveview/LiveView'
import PrintProgress from '../features/printer/task/PrintProgress'
import AMSControl from '../features/printer/ams/AMSControl'
import AxisControl from '../features/printer/motion/Motion'
// import CardGrid from '../features/Card'
import ExtuderDialog from '../features/printer/extruder/ExtuderDialog'
import MachineSetting from '../features/printer/machine/MachineSetting'

import AMSItem from '../features/printer/ams/AMSItem'
import AMSLite from "../features/printer/ams/AMSLite"

import Extuder from '../features/printer/extruder/Extuder'
import MachineControl from '../features/printer/control/MachineControl'
import FilamentDemo from '../features/printer/ams/FilamentDialog'
import AirConditionDemo from "../features/printer/control/AirControl"
import AMSPreview from '../features/printer/ams/AMSPreview'

import {XYControl} from '../features/printer/motion/Motion'

export const Route = createFileRoute('/')({
  component: RouteComponent,
})


function RouteComponent() {
  return (
    <div className="flex flex-row h-full">
      <div className='flex-col w-[6.25rem] h-full'>
        <PrinterSelector />
        <p className='text-body'> hello world </p>
      </div>

      <div className="flex flex-col w-[37.5rem] h-full">
        <ScrollView >
          <MachineSetting/>
          <AMSControl />
          <AxisControl />
          <ExtuderDialog />
          <FilamentDemo/>
          <AirConditionDemo />
          <AMSPreview/>

          <XYControl/>

          <AMSItem/>
          <AMSLite/>
          <Extuder></Extuder>
          <MachineControl/>
        </ScrollView>
      </div>

      <div className="flex flex-col  flex-1">
        <div className="flex-1">
          <WSAvcCDialogDemo />
        </div>

        <div className='w-full h-[9.5rem]'>
          <PrintProgress />
        </div>

      </div>
    </div>
  )
}
