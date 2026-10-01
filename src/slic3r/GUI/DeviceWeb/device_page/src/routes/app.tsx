import { createFileRoute } from '@tanstack/react-router'


import PrinterSelector from '../features/printer/device/PrinterSelector'
import ScrollView from '../components/ScrollView'
import LiveView from '../features/printer/liveview/LiveView'
import PrintProgress from '../features/printer/task/PrintProgress'
import DialogExample from '../features/printer/machine/MachineSetting'
import AMSControl from '../features/printer/ams/AMSControl'
import AxisControl from '../features/printer/motion/Motion'
// import CardGrid from '../features/Card'

export const Route = createFileRoute('/app')({
  component: RouteComponent,
})

function RouteComponent() {
  return (
    <div className="flex flex-row h-full">
      <div className='flex-col w-[6.25rem] h-full'>
        <PrinterSelector />
        <p className='text-body'> hello world </p>
      </div>

      <div className="flex flex-col w-[18.75rem] h-[25rem]  min-w-[18.75rem] min-h-[25rem]">
        <ScrollView >
          <DialogExample />
          <AMSControl />
          <AxisControl />

        </ScrollView>
      </div>

      <div className="flex flex-col  flex-1">
        <div className="flex-1">
          <LiveView />
        </div>
        <div className='h-[3.125rem]'>
          <PrintProgress />
        </div>
      </div>
    </div>
  )
}
