#ifndef NOTIFICATIONPROGRESSINDICATOR_HPP
#define NOTIFICATIONPROGRESSINDICATOR_HPP

#include "ProgressIndicator.hpp"

namespace Slic3r { namespace GUI {

class NotificationManager;

class NotificationProgressIndicator: public ProgressIndicator {
    NotificationManager *m_nm = nullptr;

public:

    explicit NotificationProgressIndicator(NotificationManager *nm);

    void set_range(int range) override;
    void set_cancel_callback(CancelFn = CancelFn()) override;
    void set_progress(int pr) override;
    void set_status_text(const char *) override; // utf8 char array
    int  get_range() const override;
};

}} // namespace Slic3r::GUI

#endif // NOTIFICATIONPROGRESSINDICATOR_HPP
