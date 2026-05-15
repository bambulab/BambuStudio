import { createFileRoute } from '@tanstack/react-router'

export const Route = createFileRoute('/')({
  component: RouteComponent,
})

function RouteComponent() {
  return (
    <div className="p-6 space-y-4 text-sm text-[#1f1f1f]">
      <h1 className="text-lg font-semibold">DeviceWeb Shell</h1>
      <p className="text-[#6b6b6b]">Use the route links below to open embedded Web pages.</p>
      <div className="flex flex-col gap-2">
        <a className="text-[#00ae42] underline" href="#/filament_manager">Filament Manager</a>
        <a className="text-[#00ae42] underline" href="#/device_page/ams_filament_hotend">Device Page / AMS Filament Hotend</a>
      </div>
    </div>
  )
}
