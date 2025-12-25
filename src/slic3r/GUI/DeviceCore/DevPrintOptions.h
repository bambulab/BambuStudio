#pragma once
#include <nlohmann/json.hpp>
#include "slic3r/Utils/json_diff.hpp"
#include <wx/string.h>

#include "DevDefs.h"

namespace Slic3r {

class MachineObject;

enum class PrintOptionEnum{
    AI_Monitoring,
    Spaghetti_Detection,
    PurgeChutePileup_Detection,
    NozzleClumping_Detection,
    AirPrinting_Detection,
    Buildplate_Mark_Detection,
    Buildplate_Type_Detection,
    Buildplate_Align_Detection,
    Auto_Recovery_Detection,
    Save_Remote_Print_File_To_Storage,
    Allow_Prompt_Sound_Detection,
    Filament_Tangle_Detection,
    Nozzle_Blob_Detection,
    Open_Door_Detection,
    Idle_Heating_Protect_Detection,
    First_Layer_Detection,
    Purify_Air_At_Print_End,
    Snapshot_Detection,
};

struct PrintOptionData
{
    bool is_support_detect{false}; //Some detections do not have supporting field
    int current_detect_value{-1};  //
    std::string current_detect_sensitivity_value;
    time_t detect_hold_start{0};
};

class DevPrintOptions
{
    friend class DevPrintOptionsParser;

public:
    DevPrintOptions(MachineObject *obj);

    enum PlateMakerDectect : int {
        POS_CHECK      = 1,
        TYPE_POS_CHECK = 2,
    };

    enum class PurifyAirAtPrintEndState : int {
        PurifyAirDisable = 0,
        PurifyAirByInside = 1,
        PurifyAirByOutside = 2,
    };
public:

    void SetPrintingSpeedLevel(DevPrintingSpeedLevel speed_level);
    DevPrintingSpeedLevel GetPrintingSpeedLevel() const { return m_speed_level;}
    PlateMakerDectect     GetPlateMakerDectectType() { return m_plate_maker_detect_type; }
    PrintOptionData* GetDetectionOption(PrintOptionEnum print_option);

    // detect options
    int command_xcam_control_ai_monitoring(bool on_off, std::string lvl);
    int command_xcam_control_first_layer_inspector(bool on_off, bool print_halt);
    int command_xcam_control_buildplate_marker_detector(bool on_off);
    int command_xcam_control_spaghetti_detection(bool on_off, std::string lvl);
    int command_xcam_control_purgechutepileup_detection(bool on_off, std::string lvl);
    int command_xcam_control_nozzleclumping_detection(bool on_off, std::string lvl);
    int command_xcam_control_airprinting_detection(bool on_off, std::string lvl);
    int command_xcam_control_auto_recovery_step_loss(bool on_off);
    int command_xcam_control_allow_prompt_sound(bool on_off);
    int command_xcam_control_filament_tangle_detect(bool on_off);
    int command_xcam_control_idelheatingprotect_detector(bool on_off);
    int command_xcam_control(std::string module_name, bool on_off,  MachineObject *obj ,std::string lvl = "");
    int command_xcam_control_build_plate_type_detector(bool on_off);
    int command_xcam_control_build_plate_align_detector(bool on_off);
    int command_xcam_control_purify_air_at_print_end(int on_off);
    int command_snapshot_control(int on_off);
    int command_nozzle_blob_detect(bool nozzle_blob_detect);

private:
    int command_set_purify_air_at_print_end(PurifyAirAtPrintEndState state, MachineObject *obj);
    int command_set_printing_option(bool auto_recovery, MachineObject *obj);
    int command_set_prompt_sound(bool prompt_sound, MachineObject *obj);
    int command_set_filament_tangle_detect(bool fliament_tangle_detect, MachineObject *obj);
    int command_set_against_continued_heating_mode(bool on_off);
    int command_set_snapshot_control(int on_off, MachineObject *obj);

 private:
    MachineObject *m_obj; /*owner*/

    PrintOptionData m_ai_monitoring_detection;
    PrintOptionData m_spaghetti_detection;
    PrintOptionData m_purgechutepileup_detection;
    PrintOptionData m_nozzleclumping_detection;
    PrintOptionData m_airprinting_detection;
    PrintOptionData m_buildplate_mark_detection;
    PrintOptionData m_buildplate_type_detection;
    PrintOptionData m_buildplate_align_detection;
    PrintOptionData m_auto_recovery_detection;
    PrintOptionData m_save_remote_print_file_to_storage;
    PrintOptionData m_allow_prompt_sound_detection;
    PrintOptionData m_filament_tangle_detection;
    PrintOptionData m_nozzle_blob_detection;
    PrintOptionData m_open_door_detection;
    PrintOptionData m_idel_heating_protect_detection;
    PrintOptionData m_first_layer_detection;
    PrintOptionData m_purify_air_at_print_end;
    PrintOptionData m_snapshot_detection;

    std::map<PrintOptionEnum, PrintOptionData*> m_detection_list;
    DevPrintingSpeedLevel m_speed_level = SPEED_LEVEL_INVALID;
    PlateMakerDectect m_plate_maker_detect_type{POS_CHECK};
};

class DevPrintOptionsParser
{
public:
    static void ParseDetectionV1_0(DevPrintOptions *opts, const nlohmann::json &print_json);
};

} // namespace Slic3r