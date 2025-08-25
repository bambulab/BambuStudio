#ifndef slic3r_HelioDragon_hpp_
#define slic3r_HelioDragon_hpp_

#include <string>
#include <wx/string.h>
#include <boost/optional.hpp>
#include <boost/asio/ip/address.hpp>
#include <boost/date_time/posix_time/posix_time.hpp>

#include <condition_variable>
#include <mutex>
#include <boost/thread.hpp>
#include <wx/event.h>

#include "PrintHost.hpp"
#include "libslic3r/PrintConfig.hpp"
#include "nlohmann/json.hpp"
#include "../GUI/BackgroundSlicingProcess.hpp"
#include "../GUI/NotificationManager.hpp"
#include "libslic3r/GCode/GCodeProcessor.hpp"
#include "../GUI/GUI_Preview.hpp"
#include "../GUI/Plater.hpp"
#include <vector>

namespace Slic3r {

class DynamicPrintConfig;
class Http;
class AppConfig;

class HelioQuery
{
public:
    struct PresignedURLResult
    {
        std::string key;
        std::string mimeType;
        std::string url;
        unsigned    status;
        std::string error;
    };

    struct UploadFileResult
    {
        bool        success;
        std::string error;
    };

    struct SupportedData
    {
        std::string id;
        std::string name;
        std::string native_name;
    };

    struct CreateGCodeResult
    {
        unsigned    status;
        bool        success;
        std::string name;
        std::string id;
        std::string error;
        vector<std::string> warning_flags;
        vector<std::string> error_flags;
    };

    struct CreateSimulationResult
    {
        unsigned    status;
        bool        success;
        std::string name;
        std::string id;
        std::string error;
    };

    struct CheckSimulationProgressResult
    {
        unsigned    status;
        bool        is_finished;
        float       progress;
        std::string id;
        std::string name;
        std::string url;
        std::string error;
    };

    static std::string get_helio_api_url();
    static std::string get_helio_pat();
    static void set_helio_pat(std::string pat);
    static void               request_support_machine(const std::string helio_api_url, const std::string helio_api_key, int page);
    static void request_support_material(const std::string helio_api_url, const std::string helio_api_key, int page);
    static void               request_pat_token(std::function<void(std::string)> func);
    static PresignedURLResult create_presigned_url(const std::string helio_api_url, const std::string helio_api_key);
    static UploadFileResult   upload_file_to_presigned_url(const std::string file_path_string, const std::string upload_url);
    static CreateGCodeResult  create_gcode(const std::string key,
                                           const std::string helio_api_url,
                                           const std::string helio_api_key,
                                           const std::string printer_id,
                                           const std::string filament_id);

    static void request_all_support_machine(const std::string helio_api_url, const std::string helio_api_key)
    {
        global_supported_printers.clear();
        request_support_machine(helio_api_url, helio_api_key, 1);
    }

    static void request_all_support_materials(const std::string helio_api_url, const std::string helio_api_key)
    {
        global_supported_materials.clear();
        request_support_material(helio_api_url, helio_api_key, 1);
    }

    static CreateSimulationResult create_simulation(const std::string helio_api_url,
                                                    const std::string helio_api_key,
                                                    const std::string gcode_id,
                                                    const float       initial_room_airtemp,
                                                    const float       layer_threshold,
                                                    const float       object_proximity_airtemp);

    static CheckSimulationProgressResult check_simulation_progress(const std::string helio_api_url,
                                                                   const std::string helio_api_key,
                                                                   const std::string simulation_id);

    static std::string generate_graphql_query(const std::string &gcode_id, float temperatureStabilizationHeight = -1, float airTemperatureAboveBuildPlate = -1, float stabilizedAirTemperature = -1);
    static std::string generateTimestampedString()
    {
        // Get the current UTC time
        boost::posix_time::ptime now = boost::posix_time::second_clock::universal_time();

        // Format as ISO 8601 (e.g., "2025-03-12T14:23:45")
        std::string iso_datetime = boost::posix_time::to_iso_extended_string(now);

        // Combine with your desired prefix
        return "BambuSlicer " + iso_datetime;
    }

    static std::vector<SupportedData> global_supported_printers;
    static std::vector<SupportedData> global_supported_materials;
};

class HelioBackgroundProcess
{
public:
    struct SimulationInput
    {
        float chamber_temp{-1};
    };

public:
    enum State {
        // m_thread  is not running yet, or it did not reach the STATE_IDLE yet (it does not wait on the condition yet).
        STATE_INITIAL = 0,
        STATE_STARTED,
        STATE_RUNNING,
        STATE_FINISHED,
        STATE_CANCELED,
    };

private:
    State m_state;

public:
    std::mutex              m_mutex;
    std::condition_variable m_condition;
    boost::thread           m_thread;
    std::string             helio_origin_key;
    std::string             helio_api_key;
    std::string             helio_api_url;
    std::string             printer_id;
    std::string             filament_id;

    //for user input
    SimulationInput         simulation_input_data;

    Slic3r::GCodeProcessorResult* m_gcode_result;
    Slic3r::GCodeProcessor        m_gcode_processor;
    Slic3r::GUI::Preview*         m_preview;
    std::function<void()>         m_update_function;

    void set_simulation_input_data(SimulationInput data)
    {
        simulation_input_data = data;
    }

    void stop()
    {
        m_mutex.lock();
        m_state = STATE_CANCELED;
        m_mutex.unlock();
    }

    bool is_running()
    {
        m_mutex.lock();
        bool running_state = (m_state == STATE_STARTED || m_state == STATE_RUNNING);
        m_mutex.unlock();

        return running_state;
    }

    bool was_canceled()
    {
        m_mutex.lock();
        bool canceled_state = (m_state == STATE_CANCELED);
        m_mutex.unlock();
        return canceled_state;
    }

    void set_state(State state)
    {
        m_mutex.lock();
        m_state = state;
        m_mutex.unlock();
    }

    State get_state()
    {
        m_mutex.lock();
        auto state = m_state;
        m_mutex.unlock();

        return state;
    }

    void helio_threaded_process_start(std::mutex&                                slicing_mutex,
                                      std::condition_variable&                   slicing_condition,
                                      BackgroundSlicingProcess::State&           slicing_state,
                                      std::unique_ptr<GUI::NotificationManager>& notification_manager);

    void helio_thread_start(std::mutex&                                slicing_mutex,
                            std::condition_variable&                   slicing_condition,
                            BackgroundSlicingProcess::State&           slicing_state,
                            std::unique_ptr<GUI::NotificationManager>& notification_manager);

    HelioBackgroundProcess() {}

    ~HelioBackgroundProcess()
    {
        m_gcode_result = nullptr;
        if (m_thread.joinable()) {
            m_thread.join();
        }
    }

    void init(std::string                   api_key,
              std::string                   api_url,
              std::string                   printer_id,
              std::string                   filament_id,
              Slic3r::GCodeProcessorResult* gcode_result,
              Slic3r::GUI::Preview*         preview,
              std::function<void()>         function)
    {
        m_state = STATE_STARTED;
        m_gcode_processor.reset();
        helio_origin_key  = api_key;
        helio_api_key     = "Bearer " + api_key;
        helio_api_url     = api_url;
        this->printer_id  = printer_id;
        this->filament_id = filament_id;
        m_gcode_result    = gcode_result;
        m_preview         = preview;
        m_update_function = function;
    }

    void reset()
    {
        m_state = STATE_INITIAL;
        m_gcode_processor.reset();
        m_gcode_result = nullptr;
    }

    void set_helio_api_key(std::string api_key);
    void set_gcode_result(Slic3r::GCodeProcessorResult* gcode_result);
    void create_simulation_step(HelioQuery::CreateGCodeResult              create_gcode_res,
                                std::unique_ptr<GUI::NotificationManager>& notification_manager);
    void save_downloaded_gcode_and_load_preview(std::string                                file_download_url,
                                                std::string                                simulated_gcode_path,
                                                std::string                                tmp_path,
                                                std::unique_ptr<GUI::NotificationManager>& notification_manager);

    std::string create_path_for_simulated_gcode(std::string unsimulated_gcode_path)
    {
        boost::filesystem::path p(unsimulated_gcode_path);

        if (!p.has_filename()) {
            throw std::runtime_error("Invalid path: No filename present.");
        }

        boost::filesystem::path parent       = p.parent_path();
        std::string             new_filename = "simulated_" + p.filename().string();

        return (parent / new_filename).string();
    }

    void load_simulation_to_viwer(std::string file_path, std::string tmp_path);
};
} // namespace Slic3r
#endif
