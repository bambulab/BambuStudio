#include "Exception.hpp"
#include "PrintBase.hpp"

#include <boost/filesystem.hpp>
#include <boost/format.hpp>
#include <boost/lexical_cast.hpp>
#include <boost/log/trivial.hpp>

#include "I18N.hpp"

//! macro used to mark string used at localization,
//! return same string
#define L(s) Slic3r::I18N::translate(s)

namespace Slic3r
{

void PrintTryCancel::operator()()
{
    m_print->throw_if_canceled();
}

size_t PrintStateBase::g_last_timestamp = 0;

//BBS: move set_status from hpp to cpp
void  PrintBase::set_status(int percent, const std::string &message, unsigned int flags, int warning_step) const
{
	if (m_status_callback)
        m_status_callback(SlicingStatus(percent, message, flags, warning_step));
    else
        BOOST_LOG_TRIVIAL(debug) <<boost::format("Percent %1%: %2%\n")%percent %message.c_str();
}

void PrintBase::status_update_warnings(int step, PrintStateBase::WarningLevel  warning_level,
    const std::string &message, const PrintObjectBase* print_object, PrintStateBase::SlicingNotificationType message_id)
{
    if (this->m_status_callback) {
        auto status = print_object ? SlicingStatus(*print_object, step, message, message_id, warning_level) : SlicingStatus(*this, step, message, message_id, warning_level);
        m_status_callback(status);
    }
    else if (! message.empty())
        BOOST_LOG_TRIVIAL(info) << __FUNCTION__ << boost::format(", Print warning: %1%\n")% message.c_str();
}

//BBS: add PrintObject id into slicing status
void PrintBase::status_update_warnings(int step, PrintStateBase::WarningLevel warning_level,
    const std::string& message, PrintObjectBase &object, PrintStateBase::SlicingNotificationType message_id)
{
    //BBS: add object it into slicing status
    if (this->m_status_callback) {
        m_status_callback(SlicingStatus(object, step, message, message_id, warning_level));
    }
    else if (!message.empty())
        BOOST_LOG_TRIVIAL(info) << __FUNCTION__ << boost::format(", PrintObject warning: %1%\n")% message.c_str();
}


std::mutex& PrintObjectBase::state_mutex(PrintBase *print)
{
	return print->state_mutex();
}

std::function<void()> PrintObjectBase::cancel_callback(PrintBase *print)
{
	return print->cancel_callback();
}

void PrintObjectBase::status_update_warnings(PrintBase *print, int step, PrintStateBase::WarningLevel warning_level,
    const std::string &message, PrintStateBase::SlicingNotificationType message_id)
{
    print->status_update_warnings(step, warning_level, message, this, message_id);
}

} // namespace Slic3r
