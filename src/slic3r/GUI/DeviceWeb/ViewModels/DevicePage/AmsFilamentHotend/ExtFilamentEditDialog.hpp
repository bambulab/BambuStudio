#ifndef DEVICEPAGEEXTFILAMENTEDITDIALOG_HPP
#define DEVICEPAGEEXTFILAMENTEDITDIALOG_HPP

namespace Slic3r { namespace GUI {

/// Closes the batch external-spool edit dialog, if one is open. Opening goes
/// through DevicePageAmsFilamentHotendVM::OpenBatchFilamentEditDialog(), which
/// also needs the modal result; only closing is driven from the outside (by the
/// `close_dialog` / `close_dialog_after_run` web actions).
///
/// \param result wx result id handed to EndModal, e.g. wxID_OK for a plain
///               close or wxID_APPLY when the user committed an Auto Load run.
void close_ext_filament_edit_dialog(int result);

}} // namespace Slic3r::GUI

#endif // DEVICEPAGEEXTFILAMENTEDITDIALOG_HPP
