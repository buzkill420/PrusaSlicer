#include <algorithm>
#include <exception>

#include "Job.hpp"
#include "../NotificationManager.hpp"
#include <libslic3r/Thread.hpp>
#include <boost/log/trivial.hpp>


namespace Slic3r {

void GUI::Job::run(std::exception_ptr &eptr)
{
    m_running.store(true);
    try {
        process();
    } catch (...) {
        eptr = std::current_exception();
    }

    m_running.store(false);
    
    // ensure to call the last status to finalize the job
    update_status(status_range(), "");
}

void GUI::Job::update_status(int st, const wxString &msg)
{
    auto evt = new wxThreadEvent(wxEVT_THREAD, m_thread_evt_id);
    evt->SetInt(st);
    evt->SetString(msg);
    wxQueueEvent(this, evt);
}

GUI::Job::Job(std::shared_ptr<NotificationManager> nm)
    : m_notifications(nm)
{
    m_thread_evt_id = wxNewId();

    Bind(wxEVT_THREAD, [this](const wxThreadEvent &evt) {
        if (m_finalizing)  return;

        auto msg = evt.GetString();
        if (!msg.empty() && !m_worker_error)
            m_notifications->progress_indicator_set_status_text(msg.ToUTF8().data());

        if (m_finalized) return;

        m_notifications->progress_indicator_set_progress(evt.GetInt());
        if (evt.GetInt() == status_range() || m_worker_error) {
            // set back the original range and cancel callback
            m_notifications->progress_indicator_set_range(m_range);
            m_notifications->progress_indicator_set_cancel_callback();
            wxEndBusyCursor();
            
            if (m_worker_error) {
                m_finalized = true;
                m_notifications->progress_indicator_set_status_text("");
                m_notifications->progress_indicator_set_progress(m_range);
                on_exception(m_worker_error);
            }
            else {
                // This is an RAII solution to remember that finalization is
                // running. The run method calls update_status(status_range(), "")
                // at the end, which queues up a call to this handler in all cases.
                // If process also calls update_status with maxed out status arg
                // it will call this handler twice. It is not a problem unless
                // yield is called inside the finilize() method, which would
                // jump out of finalize and call this handler again.
                struct Finalizing {
                    bool &flag;
                    Finalizing (bool &f): flag(f) { flag = true; }
                    ~Finalizing() { flag = false; }
                } fin(m_finalizing);

                finalize();
            }

            // dont do finalization again for the same process
            m_finalized = true;
        }
    }, m_thread_evt_id);
}

void GUI::Job::start()
{ // Start the job. No effect if the job is already running
    if (!m_running.load()) {
        prepare();
        
        // Save the current status indicatior range and push the new one
        m_range = m_notifications->progress_indicator_get_range();
        m_notifications->progress_indicator_set_range(status_range());
        
        // init cancellation flag and set the cancel callback
        m_canceled.store(false);
        m_notifications->progress_indicator_set_cancel_callback(
                    [this]() { m_canceled.store(true); });
        
        m_finalized  = false;
        m_finalizing = false;
        
        // Changing cursor to busy
        wxBeginBusyCursor();
        
        try { // Execute the job
            m_worker_error = nullptr;
            m_thread = create_thread([this] { this->run(m_worker_error); });
        } catch (std::exception &) {
            update_status(status_range(),
                          _(L("ERROR: not enough resources to "
                              "execute a new job.")));
        }
        
        // The state changes will be undone when the process hits the
        // last status value, in the status update handler (see ctor)
    }
}

bool GUI::Job::join(int timeout_ms)
{
    if (!m_thread.joinable()) return true;
    
    if (timeout_ms <= 0)
        m_thread.join();
    else if (!m_thread.try_join_for(boost::chrono::milliseconds(timeout_ms)))
        return false;
    
    return true;
}

void GUI::ExclusiveJobGroup::start(size_t jid) {
    assert(jid < m_jobs.size());
    stop_all();
    m_jobs[jid]->start();
}

void GUI::ExclusiveJobGroup::join_all(int wait_ms)
{
    std::vector<bool> aborted(m_jobs.size(), false);
    
    for (size_t jid = 0; jid < m_jobs.size(); ++jid)
        aborted[jid] = m_jobs[jid]->join(wait_ms);
    
    if (!std::all_of(aborted.begin(), aborted.end(), [](bool t) { return t; }))
        BOOST_LOG_TRIVIAL(error) << "Could not abort a job!";
}

bool GUI::ExclusiveJobGroup::is_any_running() const
{
    return std::any_of(m_jobs.begin(), m_jobs.end(),
                       [](const std::unique_ptr<GUI::Job> &j) {
        return j->is_running();
    });
}

}

