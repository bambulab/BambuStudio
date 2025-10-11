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
    struct SimulationInput
    {
        float chamber_temp{ -1 };
    };

    struct OptimizationInput
    {
        bool outer_wall{false};
        float chamber_temp{ -1 };
        float min_velocity{ -1 };
        float max_velocity{ -1 };
        float min_volumetric_speed{ -1 };
        float max_volumetric_speed{ -1 };
        std::array<int, 2> layers_to_optimize = { -1, -1 };

        bool isDefault() {
            return  (min_velocity == -1) &&
                    (max_velocity == -1) &&
                    (min_volumetric_speed == -1) &&
                    (max_volumetric_speed == -1);
        }
    };

    struct PresignedURLResult
    {
        unsigned    status;
        std::string key;
        std::string mimeType;
        std::string url;
        std::string error;
        std::string trace_id;
    };

    struct UploadFileResult
    {
        bool        success;
        std::string error;
        std::string trace_id;
    };

    struct SupportedData
    {
        std::string id;
        std::string name;
        std::string native_name;
    };

    struct PollResult {
        std::string status_str;
        int progress;
        int sizeKb;
        bool success;
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
        std::string trace_id;

        // V2 API fields
        float       sizeKb;
        std::string status_str;
        float       progress;
    };

    struct CreateSimulationResult
    {
        unsigned    status;
        bool        success;
        std::string name;
        std::string id;
        std::string error;
        std::string trace_id;

        void reset() {
            status  = 0;
            success = false;
            name    = "";
            id      = "";
            error   = "";
        };
    };

    struct CreateOptimizationResult
    {
        unsigned    status;
        bool        success;
        std::string name;
        std::string id;
        std::string error;
        std::string trace_id;

        void reset() {
            status  = 0;
            success = false;
            name    = "";
            id      = "";
            error   = "";
        };
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
        std::string trace_id;
    };

    struct CheckOptimizationResult
    {
        unsigned    status;
        bool        is_finished;
        float       progress;
        std::string id;
        std::string name;
        std::string url;
        std::string error;
        std::string trace_id;
        std::string qualityMeanImprovement;
        std::string qualityStdImprovement;
    };

    struct RatingData
    {
        int action = 0;
        std::string qualityMeanImprovement;
        std::string qualityStdImprovement;
    };  

    
    static std::string get_helio_api_url();
    static std::string get_helio_pat();
    static void set_helio_pat(std::string pat);
    static void request_support_machine(const std::string helio_api_url, const std::string helio_api_key, int page);
    static void request_support_material(const std::string helio_api_url, const std::string helio_api_key, int page);
    static void request_pat_token(std::function<void(std::string)> func);
    static void optimization_feedback(const std::string helio_api_url, const std::string helio_api_key, std::string optimization_id, float rating, std::string comment);
    static PresignedURLResult create_presigned_url(const std::string helio_api_url, const std::string helio_api_key);
    static UploadFileResult   upload_file_to_presigned_url(const std::string file_path_string, const std::string upload_url);

    static PollResult poll_gcode_status(const std::string& helio_api_url,
                                        const std::string& helio_api_key,
                                        const std::string& gcode_id);

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

    /*for helio simulation*/
    static CreateSimulationResult create_simulation(const std::string helio_api_url,
                                                    const std::string helio_api_key,
                                                    const std::string gcode_id,
                                                    SimulationInput sinput);

    static void stop_simulation(const std::string helio_api_url,
                                                  const std::string helio_api_key,
                                                  const std::string simulation_id);

    static CheckSimulationProgressResult check_simulation_progress(const std::string helio_api_url,
                                                                   const std::string helio_api_key,
                                                                   const std::string simulation_id);


    /*for helio optimization*/
    static CreateOptimizationResult create_optimization(const std::string helio_api_url,
                                                        const std::string helio_api_key,
                                                        const std::string gcode_id,
                                                        OptimizationInput oinput);

    static void stop_optimization(const std::string helio_api_url,
                                            const std::string helio_api_key,
                                            const std::string optimization_id);

    static CheckOptimizationResult check_optimization_progress(const std::string helio_api_url,
                                                               const std::string helio_api_key,
                                                               const std::string optimization_id);


    static std::string create_optimization_default_get(const std::string helio_api_url, const std::string helio_api_key, const std::string gcode_id);


    static std::string generate_default_optimization_query(const std::string& gcode_id);
    static std::string generate_simulation_graphql_query(const std::string& gcode_id, 
                                                         float temperatureStabilizationHeight = -1, 
                                                         float airTemperatureAboveBuildPlate = -1,
                                                         float stabilizedAirTemperature = -1);

    static std::string generate_optimization_graphql_query(const std::string& gcode_id, 
                                                           bool outerwall,
                                                           float temperatureStabilizationHeight = -1, 
                                                           float airTemperatureAboveBuildPlate = -1, 
                                                           float stabilizedAirTemperature = -1, 
                                                           double minVelocity = -1,  
                                                           double maxVelocity = -1, 
                                                           double minExtruderFlowRate = -1, 
                                                           double maxExtruderFlowRate = -1, 
                                                           int layersToOptimizeStart = -1, 
                                                           int layersToOptimizeEnd = -1);
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
    static std::string last_simulation_trace_id;
    static std::string last_optimization_trace_id;
    static double convert_speed(float mm_per_second);
    static double convert_volume_speed(float mm3_per_second);

    /*user*/
    static void request_remaining_optimizations(const std::string& helio_api_url, const std::string& helio_api_key, std::function<void(int, int)> func);
};

class HelioBackgroundProcess
{
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

    int                     action; //0-simulation 1-optimization

    /*task data*/
    HelioQuery::CreateSimulationResult current_simulation_result;
    HelioQuery::CreateOptimizationResult current_optimization_result;

    //for user input
    HelioQuery::SimulationInput         simulation_input_data;
    HelioQuery::OptimizationInput       optimization_input_data;

    Slic3r::GCodeProcessorResult* m_gcode_result{nullptr};
    Slic3r::GCodeProcessor        m_gcode_processor;
    Slic3r::GUI::Preview*         m_preview;
    std::function<void()>         m_update_function;

    void set_action(int ac)
    {
        action = ac;
    }

    void set_simulation_input_data(HelioQuery::SimulationInput data)
    {
        simulation_input_data = data;
    }

    void set_optimization_input_data(HelioQuery::OptimizationInput data)
    {
        optimization_input_data = data;
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

    void stop_current_helio_action();
    void feedback_current_helio_action(float rating, std::string commend);
    void clear_helio_file_cache();

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
    void create_simulation_step(HelioQuery::CreateGCodeResult create_gcode_res,std::unique_ptr<GUI::NotificationManager>& notification_manager);
    void create_optimization_step(HelioQuery::CreateGCodeResult create_gcode_res, std::unique_ptr<GUI::NotificationManager>& notification_manager);
    void save_downloaded_gcode_and_load_preview(std::string                                file_download_url,
                                                std::string                                helio_gcode_path,
                                                std::string                                tmp_path,
                                                std::unique_ptr<GUI::NotificationManager>& notification_manager,
                                                HelioQuery::RatingData                    rating_data);

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

    std::string create_path_for_optimization_gcode(std::string unoptimized_gcode_path)
    {
        boost::filesystem::path p(unoptimized_gcode_path);

        if (!p.has_filename()) {
            throw std::runtime_error("Invalid path: No filename present.");
        }

        boost::filesystem::path parent = p.parent_path();
        std::string             new_filename = "optimized_" + p.filename().string();

        return (parent / new_filename).string();
    }

    void load_helio_file_to_viwer(std::string file_path, std::string tmp_path, HelioQuery::RatingData rating_data);
};
} // namespace Slic3r
#endif
