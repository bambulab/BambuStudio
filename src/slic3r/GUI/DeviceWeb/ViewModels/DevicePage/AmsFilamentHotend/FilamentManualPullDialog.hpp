#ifndef DEVICEPAGEFILAMENTMANUALPULLDIALOG_HPP
#define DEVICEPAGEFILAMENTMANUALPULLDIALOG_HPP

namespace Slic3r {
class MachineObject;
}

namespace Slic3r { namespace GUI {

void open_filament_manual_pull_dialog();
void close_filament_manual_pull_dialog();
void confirm_exit_filament_manual_pull(MachineObject* machine_obj);

}} // namespace Slic3r::GUI

#endif // DEVICEPAGEFILAMENTMANUALPULLDIALOG_HPP
