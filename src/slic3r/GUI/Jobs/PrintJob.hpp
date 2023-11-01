#ifndef PrintJOB_HPP
#define PrintJOB_HPP

#include <boost/filesystem/path.hpp>
#include <boost/filesystem/operations.hpp>
#include "PlaterJob.hpp"

namespace fs = boost::filesystem;

namespace Slic3r {
namespace GUI {

class PrintPrepareData
{
public:
    bool            is_from_plater = true;
    int             plate_idx;
    fs::path        _3mf_path;
    fs::path        _3mf_config_path;
    fs::path        _temp_path;
    PrintPrepareData() {
        plate_idx = 0;
    }
};

class PlateListData
{
public:
    bool is_valid = false;
    int plate_count = 0;
    int cur_plate_index = 0;
    BedType bed_type = BedType::btDefault;
};

class PrintJob : public PlaterJob
{
    std::function<void()> m_success_fun{nullptr};
    std::string         m_dev_id;
    bool                m_job_finished{ false };
    int                 m_print_job_completed_id = 0;
    wxString            m_completed_evt_data;
    std::function<void()> m_enter_ip_address_fun_fail{ nullptr };
    std::function<void()> m_enter_ip_address_fun_success{ nullptr };

public:
    PrintPrepareData job_data;
    PlateListData    plate_data;

protected:
    void prepare() override;
    void on_exception(const std::exception_ptr &) override;
public:
    PrintJob(std::shared_ptr<ProgressIndicator> pri, Plater *plater, std::string dev_id = "");

    std::string m_project_name;
    std::string m_dev_ip;
    std::string m_ftp_folder;
    std::string m_access_code;
    std::string task_bed_type;
    std::string task_ams_mapping;
    std::string task_ams_mapping_info;
    std::string connection_type;
    std::string m_print_type;
    std::string m_dst_path;

    bool m_is_calibration_task = false;

    int         m_print_from_sdc_plate_idx = 0;
    
    bool        m_local_use_ssl_for_mqtt { true };
    bool        m_local_use_ssl_for_ftp { true };
    bool        task_bed_leveling;
    bool        task_flow_cali;
    bool        task_vibration_cali;
    bool        task_record_timelapse;
    bool        task_layer_inspect;
    bool        cloud_print_only { false };
    bool        has_sdcard { false };
    bool        task_use_ams { true };

    void set_print_config(std::string bed_type, bool bed_leveling, bool flow_cali, bool vabration_cali, bool record_timelapse, bool layer_inspect) 
    {
        task_bed_type       = bed_type;
        task_bed_leveling   = bed_leveling;
        task_flow_cali      = flow_cali;
        task_vibration_cali = vabration_cali;
        task_record_timelapse = record_timelapse;
        task_layer_inspect    = layer_inspect;
    }

    int  status_range() const override
    {
        return 100;
    }

    bool is_finished() { return m_job_finished;  }
    void set_print_job_finished_event(int event_id, wxString evt_data = wxEmptyString) {
        m_print_job_completed_id = event_id;
        m_completed_evt_data = evt_data;
    }
    void on_success(std::function<void()> success);
    void process() override;
    void finalize() override;
    void set_project_name(std::string name);
    void set_dst_name(std::string path);
    void on_check_ip_address_fail(std::function<void()> func);
    void on_check_ip_address_success(std::function<void()> func);
    void connect_to_local_mqtt();
    wxString get_http_error_msg(unsigned int status, std::string body);
    std::string truncate_string(const std::string& str, size_t maxLength);
    void set_calibration_task(bool is_calibration);

};

}} // namespace Slic3r::GUI

#endif
