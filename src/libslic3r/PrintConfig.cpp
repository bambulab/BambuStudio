#include "PrintConfig.hpp"
#include "ClipperUtils.hpp"
#include "Config.hpp"
#include "I18N.hpp"

#include <set>
#include <boost/algorithm/string/case_conv.hpp>
#include <boost/algorithm/string/replace.hpp>
#include <boost/algorithm/string/split.hpp>
#include <boost/format.hpp>
#include <boost/lexical_cast.hpp>
#include <boost/log/trivial.hpp>
#include <boost/thread.hpp>

#include <float.h>

namespace {
std::set<std::string> SplitStringAndRemoveDuplicateElement(const std::string &str, const std::string &separator)
{
    std::set<std::string> result;
    if (str.empty()) return result;

    std::string strs = str + separator;
    size_t      pos;
    size_t      size = strs.size();

    for (int i = 0; i < size; ++i) {
        pos = strs.find(separator, i);
        if (pos < size) {
            std::string sub_str = strs.substr(i, pos - i);
            result.insert(sub_str);
            i = pos + separator.size() - 1;
        }
    }

    return result;
}

void ReplaceString(std::string &resource_str, const std::string &old_str, const std::string &new_str)
{
    std::string::size_type pos = 0;
    size_t new_size = 0;
    while ((pos = resource_str.find(old_str, pos + new_size)) != std::string::npos)
    {
        resource_str.replace(pos, old_str.length(), new_str);
        new_size = new_str.size();
    }
}
}

namespace Slic3r {

//! macro used to mark string used at localization,
//! return same string
#define L(s) (s)
#define _(s) Slic3r::I18N::translate(s)


const std::vector<std::string> filament_extruder_override_keys = {
    // floats
    "filament_retraction_length", 
    "filament_z_hop",
    "filament_z_hop_types",
    "filament_retract_lift_above",
    "filament_retract_lift_below",
    "filament_retraction_speed",
    "filament_deretraction_speed",
    "filament_retract_restart_extra",
    "filament_retraction_minimum_travel",
    // BBS: floats
    "filament_wipe_distance",
    // bools
    "filament_retract_when_changing_layer",
    "filament_wipe",
    // percents
    "filament_retract_before_wipe",
    "filament_long_retractions_when_cut",
    "filament_retraction_distances_when_cut"
};

size_t get_extruder_index(const GCodeConfig& config, unsigned int filament_id)
{
    if (filament_id < config.filament_map.size()) {
        return config.filament_map.get_at(filament_id)-1;
    }
    return 0;
}

static t_config_enum_names enum_names_from_keys_map(const t_config_enum_values &enum_keys_map)
{
    t_config_enum_names names;
    int cnt = 0;
    for (const auto& kvp : enum_keys_map)
        cnt = std::max(cnt, kvp.second);
    cnt += 1;
    names.assign(cnt, "");
    for (const auto& kvp : enum_keys_map)
        names[kvp.second] = kvp.first;
    return names;
}

#define CONFIG_OPTION_ENUM_DEFINE_STATIC_MAPS(NAME) \
    static t_config_enum_names s_keys_names_##NAME = enum_names_from_keys_map(s_keys_map_##NAME); \
    template<> const t_config_enum_values& ConfigOptionEnum<NAME>::get_enum_values() { return s_keys_map_##NAME; } \
    template<> const t_config_enum_names& ConfigOptionEnum<NAME>::get_enum_names() { return s_keys_names_##NAME; }

static t_config_enum_values s_keys_map_PrinterTechnology {
    { "FFF",            ptFFF },
    { "SLA",            ptSLA }
};
CONFIG_OPTION_ENUM_DEFINE_STATIC_MAPS(PrinterTechnology)

static t_config_enum_values s_keys_map_PrintHostType{
    { "prusalink",      htPrusaLink },
    { "octoprint",      htOctoPrint },
    { "duet",           htDuet },
    { "flashair",       htFlashAir },
    { "astrobox",       htAstroBox },
    { "repetier",       htRepetier },
    { "mks",            htMKS }
};
CONFIG_OPTION_ENUM_DEFINE_STATIC_MAPS(PrintHostType)

static t_config_enum_values s_keys_map_AuthorizationType{
    { "key",            atKeyPassword },
    { "user",           atUserPassword }
};
CONFIG_OPTION_ENUM_DEFINE_STATIC_MAPS(AuthorizationType)

static t_config_enum_values s_keys_map_GCodeFlavor {
    { "marlin",         gcfMarlinLegacy },
    { "klipper",        gcfKlipper },
    { "reprap",         gcfRepRapSprinter },
    { "reprapfirmware", gcfRepRapFirmware },
    { "repetier",       gcfRepetier },
    { "teacup",         gcfTeacup },
    { "makerware",      gcfMakerWare },
    { "marlin2",        gcfMarlinFirmware },
    { "sailfish",       gcfSailfish },
    { "smoothie",       gcfSmoothie },
    { "mach3",          gcfMach3 },
    { "machinekit",     gcfMachinekit },
    { "no-extrusion",   gcfNoExtrusion }
};
CONFIG_OPTION_ENUM_DEFINE_STATIC_MAPS(GCodeFlavor)

static t_config_enum_values s_keys_map_BedTempFormula {
    { "by_first_filament",int(BedTempFormula::btfFirstFilament) },
    { "by_highest_temp", int(BedTempFormula::btfHighestTemp)}
};
CONFIG_OPTION_ENUM_DEFINE_STATIC_MAPS(BedTempFormula)

static t_config_enum_values s_keys_map_FuzzySkinType {
    { "none",           int(FuzzySkinType::None) },
    { "external",       int(FuzzySkinType::External) },
    { "all",            int(FuzzySkinType::All) },
    { "allwalls",       int(FuzzySkinType::AllWalls)}
};
CONFIG_OPTION_ENUM_DEFINE_STATIC_MAPS(FuzzySkinType)

static t_config_enum_values s_keys_map_InfillPattern {
    { "concentric",         ipConcentric },
    { "zig-zag",            ipRectilinear },
    { "grid",               ipGrid },
    { "line",               ipLine },
    { "cubic",              ipCubic },
    { "triangles",          ipTriangles },
    { "tri-hexagon",        ipStars },
    { "gyroid",             ipGyroid },
    { "honeycomb",          ipHoneycomb },
    { "adaptivecubic",      ipAdaptiveCubic },
    { "monotonic",          ipMonotonic },
    { "monotonicline",      ipMonotonicLine },
    { "alignedrectilinear", ipAlignedRectilinear },
    { "3dhoneycomb",        ip3DHoneycomb },
    { "hilbertcurve",       ipHilbertCurve },
    { "archimedeanchords",  ipArchimedeanChords },
    { "octagramspiral",     ipOctagramSpiral },
    { "supportcubic",       ipSupportCubic },
    { "lightning",          ipLightning },
    { "crosshatch",         ipCrossHatch},
    { "zigzag",             ipZigZag },
    { "crosszag",           ipCrossZag },
    { "lockedzag",          ipLockedZag }
};
CONFIG_OPTION_ENUM_DEFINE_STATIC_MAPS(InfillPattern)

static t_config_enum_values s_keys_map_IroningType {
    { "no ironing",     int(IroningType::NoIroning) },
    { "top",            int(IroningType::TopSurfaces) },
    { "topmost",        int(IroningType::TopmostOnly) },
    { "solid",          int(IroningType::AllSolid) }
};
CONFIG_OPTION_ENUM_DEFINE_STATIC_MAPS(IroningType)

//BBS:
static t_config_enum_values s_keys_map_TopOneWallType {
    {"not apply", int(TopOneWallType::None)},
    {"all top", int(TopOneWallType::Alltop)},
    {"topmost", int(TopOneWallType::Topmost)}
};
CONFIG_OPTION_ENUM_DEFINE_STATIC_MAPS(TopOneWallType)

//BBS
static t_config_enum_values s_keys_map_WallInfillOrder {
    { "inner wall/outer wall/infill",     int(WallInfillOrder::InnerOuterInfill) },
    { "outer wall/inner wall/infill",     int(WallInfillOrder::OuterInnerInfill) },
    { "infill/inner wall/outer wall",     int(WallInfillOrder::InfillInnerOuter) },
    { "infill/outer wall/inner wall",     int(WallInfillOrder::InfillOuterInner) },
    { "inner-outer-inner wall/infill",     int(WallInfillOrder::InnerOuterInnerInfill)}
};
CONFIG_OPTION_ENUM_DEFINE_STATIC_MAPS(WallInfillOrder)

//BBS
static t_config_enum_values s_keys_map_WallSequence {
    { "inner wall/outer wall",     int(WallSequence::InnerOuter) },
    { "outer wall/inner wall",     int(WallSequence::OuterInner) },
    { "inner-outer-inner wall",    int(WallSequence::InnerOuterInner)}

};
CONFIG_OPTION_ENUM_DEFINE_STATIC_MAPS(WallSequence)

static t_config_enum_values s_keys_map_EnsureVerticalThicknessLevel {
    { "disabled",     int(EnsureVerticalThicknessLevel::evtDisabled) },
    { "partial",      int(EnsureVerticalThicknessLevel::evtPartial) },
    { "enabled",      int(EnsureVerticalThicknessLevel::evtEnabled)}
};
CONFIG_OPTION_ENUM_DEFINE_STATIC_MAPS(EnsureVerticalThicknessLevel)

//BBS
static t_config_enum_values s_keys_map_PrintSequence {
    { "by layer",     int(PrintSequence::ByLayer) },
    { "by object",    int(PrintSequence::ByObject) }
};
CONFIG_OPTION_ENUM_DEFINE_STATIC_MAPS(PrintSequence)

static t_config_enum_values s_keys_map_SlicingMode {
    { "regular",        int(SlicingMode::Regular) },
    { "even_odd",       int(SlicingMode::EvenOdd) },
    { "close_holes",    int(SlicingMode::CloseHoles) }
};
CONFIG_OPTION_ENUM_DEFINE_STATIC_MAPS(SlicingMode)

static t_config_enum_values s_keys_map_SupportMaterialPattern {
    { "rectilinear",        smpRectilinear },
    { "rectilinear-grid",   smpRectilinearGrid },
    { "honeycomb",          smpHoneycomb },
    { "lightning",          smpLightning },
    { "default",            smpDefault},
    { "hollow",               smpNone},
};
CONFIG_OPTION_ENUM_DEFINE_STATIC_MAPS(SupportMaterialPattern)

static t_config_enum_values s_keys_map_SupportMaterialStyle {
    { "default",        smsDefault },
    { "grid",           smsGrid },
    { "snug",           smsSnug },
    { "tree_slim",      smsTreeSlim },
    { "tree_strong",    smsTreeStrong },
    { "tree_hybrid",    smsTreeHybrid },
    { "tree_organic",   smsTreeOrganic }
};
CONFIG_OPTION_ENUM_DEFINE_STATIC_MAPS(SupportMaterialStyle)

static t_config_enum_values s_keys_map_SupportMaterialInterfacePattern {
    { "auto",           smipAuto },
    { "rectilinear",    smipRectilinear },
    { "concentric",     smipConcentric },
    { "rectilinear_interlaced", smipRectilinearInterlaced},
    { "grid",           smipGrid }
};
CONFIG_OPTION_ENUM_DEFINE_STATIC_MAPS(SupportMaterialInterfacePattern)

static t_config_enum_values s_keys_map_SupportType{
    { "normal(auto)",   stNormalAuto },
    { "tree(auto)", stTreeAuto },
    { "normal(manual)", stNormal },
    { "tree(manual)", stTree }
};
CONFIG_OPTION_ENUM_DEFINE_STATIC_MAPS(SupportType)

static t_config_enum_values s_keys_map_SeamPosition {
    { "nearest",        spNearest },
    { "aligned",        spAligned },
    { "back",           spRear },
    { "random",         spRandom },
};
CONFIG_OPTION_ENUM_DEFINE_STATIC_MAPS(SeamPosition)


// Orca
static t_config_enum_values s_keys_map_SeamScarfType{
    {"none",     int(SeamScarfType::None)},
    {"external", int(SeamScarfType::External)},
    {"all",      int(SeamScarfType::All)},
};
CONFIG_OPTION_ENUM_DEFINE_STATIC_MAPS(SeamScarfType)

static const t_config_enum_values s_keys_map_SLADisplayOrientation = {
    { "landscape",      sladoLandscape},
    { "portrait",       sladoPortrait}
};
CONFIG_OPTION_ENUM_DEFINE_STATIC_MAPS(SLADisplayOrientation)

static const t_config_enum_values s_keys_map_SLAPillarConnectionMode = {
    {"zigzag",          slapcmZigZag},
    {"cross",           slapcmCross},
    {"dynamic",         slapcmDynamic}
};
CONFIG_OPTION_ENUM_DEFINE_STATIC_MAPS(SLAPillarConnectionMode)

static const t_config_enum_values s_keys_map_SLAMaterialSpeed = {
    {"slow", slamsSlow},
    {"fast", slamsFast}
};
CONFIG_OPTION_ENUM_DEFINE_STATIC_MAPS(SLAMaterialSpeed);

static const t_config_enum_values s_keys_map_BrimType = {
    {"no_brim",         btNoBrim},
    {"outer_only",      btOuterOnly},
    {"inner_only",      btInnerOnly},
    {"outer_and_inner", btOuterAndInner},
    {"auto_brim", btAutoBrim},  // BBS
    {"brim_ears", btBrimEars}  // BBS
};
CONFIG_OPTION_ENUM_DEFINE_STATIC_MAPS(BrimType)

// using 0,1 to compatible with old files
static const t_config_enum_values s_keys_map_TimelapseType = {
    {"0",       tlTraditional},
    {"1",       tlSmooth}
};
CONFIG_OPTION_ENUM_DEFINE_STATIC_MAPS(TimelapseType)

static const t_config_enum_values s_keys_map_DraftShield = {
    { "disabled", dsDisabled },
    { "limited",  dsLimited  },
    { "enabled",  dsEnabled  }
};
CONFIG_OPTION_ENUM_DEFINE_STATIC_MAPS(DraftShield)

static const t_config_enum_values s_keys_map_ForwardCompatibilitySubstitutionRule = {
    { "disable",        ForwardCompatibilitySubstitutionRule::Disable },
    { "enable",         ForwardCompatibilitySubstitutionRule::Enable },
    { "enable_silent",  ForwardCompatibilitySubstitutionRule::EnableSilent }
};
CONFIG_OPTION_ENUM_DEFINE_STATIC_MAPS(ForwardCompatibilitySubstitutionRule)

static const t_config_enum_values s_keys_map_OverhangFanThreshold = {
    { "0%",         Overhang_threshold_none },
    { "10%",         Overhang_threshold_1_4  },
    { "25%",        Overhang_threshold_2_4  },
    { "50%",        Overhang_threshold_3_4  },
    { "75%",        Overhang_threshold_4_4  },
    { "95%",        Overhang_threshold_bridge  }
};
CONFIG_OPTION_ENUM_DEFINE_STATIC_MAPS(OverhangFanThreshold)

//BBS
static const t_config_enum_values s_keys_map_OverhangThresholdParticipatingCooling = {
    { "0%",         Overhang_threshold_participating_cooling_none },
    { "10%",        Overhang_threshold_participating_cooling_1_4  },
    { "25%",        Overhang_threshold_participating_cooling_2_4  },
    { "50%",        Overhang_threshold_participating_cooling_3_4  },
    { "75%",        Overhang_threshold_participating_cooling_4_4  },
    { "95%",        Overhang_threshold_participating_cooling_bridge  }
};
CONFIG_OPTION_ENUM_DEFINE_STATIC_MAPS(OverhangThresholdParticipatingCooling)

// BBS
static const t_config_enum_values s_keys_map_BedType = {
    { "Default Plate",      btDefault },
    { "Cool Plate",         btPC },
    { "Engineering Plate",  btEP  },
    { "High Temp Plate",    btPEI  },
    { "Textured PEI Plate", btPTE },
    {"Supertack Plate",     btSuperTack}
};
CONFIG_OPTION_ENUM_DEFINE_STATIC_MAPS(BedType)

// BBS
static const t_config_enum_values s_keys_map_LayerSeq = {
    { "Auto",              flsAuto },
    { "Customize",         flsCutomize },
};
CONFIG_OPTION_ENUM_DEFINE_STATIC_MAPS(LayerSeq)

static t_config_enum_values s_keys_map_NozzleType {
    { "undefine",       int(NozzleType::ntUndefine) },
    { "hardened_steel", int(NozzleType::ntHardenedSteel) },
    { "stainless_steel", int(NozzleType::ntStainlessSteel)},
    { "tungsten_carbide", int(NozzleType::ntTungstenCarbide)},
    { "brass",          int(NozzleType::ntBrass) }
};
CONFIG_OPTION_ENUM_DEFINE_STATIC_MAPS(NozzleType)

static t_config_enum_values s_keys_map_PrinterStructure {
    {"undefine",        int(PrinterStructure::psUndefine)},
    {"corexy",          int(PrinterStructure::psCoreXY)},
    {"i3",              int(PrinterStructure::psI3)},
    {"hbot",            int(PrinterStructure::psHbot)},
    {"delta",           int(PrinterStructure::psDelta)}
};
CONFIG_OPTION_ENUM_DEFINE_STATIC_MAPS(PrinterStructure)

static t_config_enum_values s_keys_map_PerimeterGeneratorType{
    { "classic", int(PerimeterGeneratorType::Classic) },
    { "arachne", int(PerimeterGeneratorType::Arachne) }
};
CONFIG_OPTION_ENUM_DEFINE_STATIC_MAPS(PerimeterGeneratorType)

static const t_config_enum_values s_keys_map_ZHopType = {
    { "Auto Lift",          zhtAuto },
    { "Normal Lift",        zhtNormal },
    { "Slope Lift",         zhtSlope },
    { "Spiral Lift",        zhtSpiral }
};
CONFIG_OPTION_ENUM_DEFINE_STATIC_MAPS(ZHopType)

static const t_config_enum_values s_keys_map_ExtruderType = {
    { "Direct Drive",   etDirectDrive },
    { "Bowden",        etBowden }
};
CONFIG_OPTION_ENUM_DEFINE_STATIC_MAPS(ExtruderType)

static const t_config_enum_values s_keys_map_NozzleVolumeType = {
    { "Standard",  nvtStandard },
    { "High Flow", nvtHighFlow }
};
CONFIG_OPTION_ENUM_DEFINE_STATIC_MAPS(NozzleVolumeType)

static const t_config_enum_values s_keys_map_FilamentMapMode = {
    { "Auto For Flush", fmmAutoForFlush },
    { "Auto For Match", fmmAutoForMatch },
    { "Manual", fmmManual }
};
CONFIG_OPTION_ENUM_DEFINE_STATIC_MAPS(FilamentMapMode)


//BBS
std::string get_extruder_variant_string(ExtruderType extruder_type, NozzleVolumeType nozzle_volume_type)
{
    std::string variant_string;

    if (extruder_type > etMaxExtruderType) {
        BOOST_LOG_TRIVIAL(error) << __FUNCTION__ << boost::format(", unsupported ExtruderType=%1%")%extruder_type;
        //extruder_type = etDirectDrive;
        return variant_string;
    }
    if (nozzle_volume_type > nvtMaxNozzleVolumeType) {
        BOOST_LOG_TRIVIAL(error) << __FUNCTION__ << boost::format(", unsupported NozzleVolumeType=%1%")%nozzle_volume_type;
        //extruder_type = etDirectDrive;
        return variant_string;
    }
    variant_string = s_keys_names_ExtruderType[extruder_type];
    variant_string+= " ";
    variant_string+= s_keys_names_NozzleVolumeType[nozzle_volume_type];
    return variant_string;
}

std::string get_nozzle_volume_type_string(NozzleVolumeType nozzle_volume_type)
{
    if (nozzle_volume_type > nvtMaxNozzleVolumeType) {
        BOOST_LOG_TRIVIAL(error) << __FUNCTION__ << boost::format(", unsupported NozzleVolumeType=%1%") % nozzle_volume_type;
        return "";
    }
    return s_keys_names_NozzleVolumeType[nozzle_volume_type];
}

std::vector<std::map<int, int>> get_extruder_ams_count(const std::vector<std::string>& strs)
{
    std::vector<std::map<int, int>> extruder_ams_counts;
    for (const std::string& str : strs) {
        std::map<int, int> ams_count_info;
        if (str.empty()) {
            extruder_ams_counts.emplace_back(ams_count_info);
            continue;
        }
        std::vector<std::string> ams_infos;
        boost::algorithm::split(ams_infos, str, boost::algorithm::is_any_of("|"));
        for (const std::string& ams_info : ams_infos) {
            std::vector<std::string> numbers;
            boost::algorithm::split(numbers, ams_info, boost::algorithm::is_any_of("#"));
            assert(numbers.size() == 2);
            ams_count_info.insert(std::make_pair(stoi(numbers[0]), stoi(numbers[1])));
        }
        extruder_ams_counts.emplace_back(ams_count_info);
    }
    return extruder_ams_counts;
}

std::vector<std::string> save_extruder_ams_count_to_string(const std::vector<std::map<int, int>> &extruder_ams_count)
{
    std::vector<std::string> extruder_ams_count_str;
    for (size_t i = 0; i < extruder_ams_count.size(); ++i) {
        std::ostringstream oss;
        const auto &item = extruder_ams_count[i];
        for (auto it = item.begin(); it != item.end(); ++it) {
            oss << it->first << "#" << it->second;
            if (std::next(it) != item.end()) {
                oss << "|";
            }
        }
        extruder_ams_count_str.push_back(oss.str());
    }
    return extruder_ams_count_str;
}

static void assign_printer_technology_to_unknown(t_optiondef_map &options, PrinterTechnology printer_technology)
{
    for (std::pair<const t_config_option_key, ConfigOptionDef> &kvp : options)
        if (kvp.second.printer_technology == ptUnknown)
            kvp.second.printer_technology = printer_technology;
}

PrintConfigDef::PrintConfigDef()
{
    this->init_common_params();
    assign_printer_technology_to_unknown(this->options, ptAny);
    this->init_fff_params();
    this->init_extruder_option_keys();
    assign_printer_technology_to_unknown(this->options, ptFFF);
    this->init_sla_params();
    assign_printer_technology_to_unknown(this->options, ptSLA);
}

void PrintConfigDef::init_common_params()
{
    ConfigOptionDef* def;

    def = this->add("printer_technology", coEnum);
    //def->label = L("Printer technology");
    def->label = "Printer technology";
    //def->tooltip = L("Printer technology");
    def->enum_keys_map = &ConfigOptionEnum<PrinterTechnology>::get_enum_values();
    def->enum_values.push_back("FFF");
    def->enum_values.push_back("SLA");
    def->set_default_value(new ConfigOptionEnum<PrinterTechnology>(ptFFF));

    def = this->add("printable_area", coPoints);
    def->label = L("Printable area");
    //BBS
    def->mode = comAdvanced;
    def->gui_type = ConfigOptionDef::GUIType::one_string;
    def->set_default_value(new ConfigOptionPoints{ Vec2d(0, 0), Vec2d(200, 0), Vec2d(200, 200), Vec2d(0, 200) });

    def = this->add("extruder_printable_area", coPointsGroups);
    def->label = L("Extruder printable area");
    def->mode = comAdvanced;
    def->gui_type = ConfigOptionDef::GUIType::one_string;
    def->set_default_value(new ConfigOptionPointsGroups{});

    //BBS: add "bed_exclude_area"
    def = this->add("bed_exclude_area", coPoints);
    def->label = L("Bed exclude area");
    def->tooltip = L("Unprintable area in XY plane. For example, X1 Series printers use the front left corner to cut filament during filament change. "
        "The area is expressed as polygon by points in following format: \"XxY, XxY, ...\"");
    def->mode = comAdvanced;
    def->gui_type = ConfigOptionDef::GUIType::one_string;
    def->set_default_value(new ConfigOptionPoints{ Vec2d(0, 0) });

    def = this->add("bed_custom_texture", coString);
    def->label = L("Bed custom texture");
    def->mode = comAdvanced;
    def->gui_type = ConfigOptionDef::GUIType::one_string;
    def->set_default_value(new ConfigOptionString(""));

    def = this->add("bed_custom_model", coString);
    def->label = L("Bed custom model");
    def->mode = comAdvanced;
    def->gui_type = ConfigOptionDef::GUIType::one_string;
    def->set_default_value(new ConfigOptionString(""));

    def = this->add("elefant_foot_compensation", coFloat);
    def->label = L("Elephant foot compensation");
    def->category = L("Quality");
    def->tooltip = L("Shrink the initial layer on build plate to compensate for elephant foot effect");
    def->sidetext = L("mm");
    def->min = 0;
    def->mode = comAdvanced;
    def->set_default_value(new ConfigOptionFloat(0.));

    def = this->add("layer_height", coFloat);
    def->label = L("Layer height");
    def->category = L("Quality");
    def->tooltip = L("Slicing height for each layer. Smaller layer height means more accurate and more printing time");
    def->sidetext = L("mm");
    def->min = 0;
    def->set_default_value(new ConfigOptionFloat(0.2));

    def = this->add("printable_height", coFloat);
    def->label = L("Printable height");
    def->tooltip = L("Maximum printable height which is limited by mechanism of printer");
    def->sidetext = L("mm");
    def->min = 0;
    def->max = 1000;
    def->mode = comSimple;
    def->set_default_value(new ConfigOptionFloat(100.0));

    def           = this->add("extruder_printable_height", coFloats);
    def->label    = L("Extruder printable height");
    def->tooltip  = L("Maximum printable height of this extruder which is limited by mechanism of printer");
    def->sidetext = L("mm");
    def->min      = 0;
    def->max      = 1000;
    def->mode     = comAdvanced;
    def->set_default_value(new ConfigOptionFloatsNullable{0});

    // Options used by physical printers

    def = this->add("preset_names", coStrings);
    def->label = L("Printer preset names");
    //def->tooltip = L("Names of presets related to the physical printer");
    def->mode = comDevelop;
    def->set_default_value(new ConfigOptionStrings());

    def = this->add("print_host", coString);
    def->label = L("Hostname, IP or URL");
    def->tooltip = L("Slic3r can upload G-code files to a printer host. This field should contain "
        "the hostname, IP address or URL of the printer host instance. "
        "Print host behind HAProxy with basic auth enabled can be accessed by putting the user name and password into the URL "
        "in the following format: https://username:password@your-octopi-address/");
    def->mode = comAdvanced;
    def->cli = ConfigOptionDef::nocli;
    def->set_default_value(new ConfigOptionString(""));

    def = this->add("print_host_webui", coString);
    def->label = L("Device UI");
    def->tooltip = L("Specify the URL of your device user interface if it's not same as print_host");
    def->mode = comAdvanced;
    def->cli = ConfigOptionDef::nocli;
    def->set_default_value(new ConfigOptionString(""));


    def = this->add("printhost_apikey", coString);
    def->label = L("API Key / Password");
    def->tooltip = L("Slic3r can upload G-code files to a printer host. This field should contain "
        "the API Key or the password required for authentication.");
    def->mode = comAdvanced;
    def->cli = ConfigOptionDef::nocli;
    def->set_default_value(new ConfigOptionString(""));

    def = this->add("printhost_port", coString);
    def->label = L("Printer");
    def->tooltip = L("Name of the printer");
    def->gui_type = ConfigOptionDef::GUIType::select_open;
    def->mode = comAdvanced;
    def->cli = ConfigOptionDef::nocli;
    def->set_default_value(new ConfigOptionString(""));

    def = this->add("printhost_cafile", coString);
    def->label = L("HTTPS CA File");
    def->tooltip = L("Custom CA certificate file can be specified for HTTPS OctoPrint connections, in crt/pem format. "
        "If left blank, the default OS CA certificate repository is used.");
    def->mode = comAdvanced;
    def->cli = ConfigOptionDef::nocli;
    def->set_default_value(new ConfigOptionString(""));

    // Options used by physical printers

    def = this->add("printhost_user", coString);
    def->label = L("User");
    //    def->tooltip = "";
    def->mode = comAdvanced;
    def->cli = ConfigOptionDef::nocli;
    def->set_default_value(new ConfigOptionString(""));

    def = this->add("printhost_password", coString);
    def->label = L("Password");
    //    def->tooltip = "";
    def->mode = comAdvanced;
    def->cli = ConfigOptionDef::nocli;
    def->set_default_value(new ConfigOptionString(""));

    // Only available on Windows.
    def = this->add("printhost_ssl_ignore_revoke", coBool);
    def->label = L("Ignore HTTPS certificate revocation checks");
    def->tooltip = L("Ignore HTTPS certificate revocation checks in case of missing or offline distribution points. "
        "One may want to enable this option for self signed certificates if connection fails.");
    def->mode = comAdvanced;
    def->cli = ConfigOptionDef::nocli;
    def->set_default_value(new ConfigOptionBool(false));

    def = this->add("preset_names", coStrings);
    def->label = L("Printer preset names");
    def->tooltip = L("Names of presets related to the physical printer");
    def->mode = comAdvanced;
    def->set_default_value(new ConfigOptionStrings());

    def = this->add("printhost_authorization_type", coEnum);
    def->label = L("Authorization Type");
    //    def->tooltip = "";
    def->enum_keys_map = &ConfigOptionEnum<AuthorizationType>::get_enum_values();
    def->enum_values.push_back("key");
    def->enum_values.push_back("user");
    def->enum_labels.push_back(L("API key"));
    def->enum_labels.push_back(L("HTTP digest"));
    def->mode = comAdvanced;
    def->cli = ConfigOptionDef::nocli;
    def->set_default_value(new ConfigOptionEnum<AuthorizationType>(atKeyPassword));

    // temporary workaround for compatibility with older Slicer
    {
        def = this->add("preset_name", coString);
        def->set_default_value(new ConfigOptionString());
    }
}

void PrintConfigDef::init_fff_params()
{
    ConfigOptionDef* def;

    // Maximum extruder temperature, bumped to 1500 to support printing of glass.
    const int max_temp = 1500;

    def = this->add("reduce_crossing_wall", coBool);
    def->label = L("Avoid crossing wall");
    def->category = L("Quality");
    def->tooltip = L("Detour and avoid traveling across wall which may cause blob on surface");
    def->mode = comAdvanced;
    def->set_default_value(new ConfigOptionBool(false));

    def = this->add("z_direction_outwall_speed_continuous", coBool);
    def->label = L("Smoothing wall speed along Z(experimental)");
    def->category = L("Quality");
    def->tooltip  = L("Smoothing outwall speed in z direction to get better surface quality. Print time will increases. It is not work on spiral vase mode.");
    def->mode     = comAdvanced;
    def->set_default_value(new ConfigOptionBool(false));

    def = this->add("max_travel_detour_distance", coFloatOrPercent);
    def->label = L("Avoid crossing wall - Max detour length");
    def->category = L("Quality");
    def->tooltip = L("Maximum detour distance for avoiding crossing wall. "
                     "Don't detour if the detour distance is larger than this value. "
                     "Detour length could be specified either as an absolute value or as percentage (for example 50%) of a direct travel path. Zero to disable");
    def->sidetext = L("mm or %");
    def->min = 0;
    def->max_literal = 1000;
    def->mode = comAdvanced;
    def->set_default_value(new ConfigOptionFloatOrPercent(0., false));

    // BBS
    def             = this->add("supertack_plate_temp", coInts);
    def->label      = L("Other layers");
    def->tooltip    = L("Bed temperature for layers except the initial one. "
                     "Value 0 means the filament does not support to print on the Cool Plate");
    def->sidetext   = "°C";
    def->full_label = L("Bed temperature");
    def->min        = 0;
    def->max        = 120;
    def->set_default_value(new ConfigOptionInts{35});

    def = this->add("cool_plate_temp", coInts);
    def->label = L("Other layers");
    def->tooltip = L("Bed temperature for layers except the initial one. "
        "Value 0 means the filament does not support to print on the Cool Plate");
    def->sidetext = "°C";
    def->full_label = L("Bed temperature");
    def->min = 0;
    def->max = 120;
    def->set_default_value(new ConfigOptionInts{ 35 });

    def = this->add("eng_plate_temp", coInts);
    def->label = L("Other layers");
    def->tooltip = L("Bed temperature for layers except the initial one. "
        "Value 0 means the filament does not support to print on the Engineering Plate");
    def->sidetext = "°C";
    def->full_label = L("Bed temperature");
    def->min = 0;
    def->max = 120;
    def->set_default_value(new ConfigOptionInts{ 45 });

    def = this->add("hot_plate_temp", coInts);
    def->label = L("Other layers");
    def->tooltip = L("Bed temperature for layers except the initial one. "
        "Value 0 means the filament does not support to print on the High Temp Plate");
    def->sidetext = "°C";
    def->full_label = L("Bed temperature");
    def->min = 0;
    def->max = 120;
    def->set_default_value(new ConfigOptionInts{ 45 });

    def             = this->add("textured_plate_temp", coInts);
    def->label      = L("Other layers");
    def->tooltip    = L("Bed temperature for layers except the initial one. "
                     "Value 0 means the filament does not support to print on the Textured PEI Plate");
    def->sidetext   = "°C";
    def->full_label = L("Bed temperature");
    def->min        = 0;
    def->max        = 120;
    def->set_default_value(new ConfigOptionInts{45});

    def = this->add("supertack_plate_temp_initial_layer", coInts);
    def->label = L("Initial layer");
    def->full_label = L("Initial layer bed temperature");
    def->tooltip = L("Bed temperature of the initial layer. "
        "Value 0 means the filament does not support to print on the Bambu Cool Plate SuperTack");
    def->sidetext = "°C";
    def->min = 0;
    def->max = 120;
    def->set_default_value(new ConfigOptionInts{ 35 });

    def = this->add("cool_plate_temp_initial_layer", coInts);
    def->label = L("Initial layer");
    def->full_label = L("Initial layer bed temperature");
    def->tooltip = L("Bed temperature of the initial layer. "
        "Value 0 means the filament does not support to print on the Cool Plate");
    def->sidetext = "°C";
    def->min = 0;
    def->max = 120;
    def->set_default_value(new ConfigOptionInts{ 35 });

    def = this->add("eng_plate_temp_initial_layer", coInts);
    def->label = L("Initial layer");
    def->full_label = L("Initial layer bed temperature");
    def->tooltip = L("Bed temperature of the initial layer. "
        "Value 0 means the filament does not support to print on the Engineering Plate");
    def->sidetext = "°C";
    def->min = 0;
    def->max = 120;
    def->set_default_value(new ConfigOptionInts{ 45 });

    def = this->add("hot_plate_temp_initial_layer", coInts);
    def->label = L("Initial layer");
    def->full_label = L("Initial layer bed temperature");
    def->tooltip = L("Bed temperature of the initial layer. "
        "Value 0 means the filament does not support to print on the High Temp Plate");
    def->sidetext = "°C";
    def->min = 0;
    def->max = 120;
    def->set_default_value(new ConfigOptionInts{ 45 });

    def             = this->add("textured_plate_temp_initial_layer", coInts);
    def->label      = L("Initial layer");
    def->full_label = L("Initial layer bed temperature");
    def->tooltip    = L("Bed temperature of the initial layer. "
                     "Value 0 means the filament does not support to print on the Textured PEI Plate");
    def->sidetext   = "°C";
    def->min        = 0;
    def->max        = 120;
    def->set_default_value(new ConfigOptionInts{45});

    def = this->add("curr_bed_type", coEnum);
    def->label = L("Bed type");
    def->tooltip = L("Bed types supported by the printer");
    def->mode = comSimple;
    def->enum_keys_map = &s_keys_map_BedType;
    def->enum_values.emplace_back("Cool Plate");
    def->enum_values.emplace_back("Engineering Plate");
    def->enum_values.emplace_back("High Temp Plate");
    def->enum_values.emplace_back("Textured PEI Plate");
    def->enum_values.emplace_back("Supertack Plate");
    def->enum_labels.emplace_back(L("Cool Plate"));
    def->enum_labels.emplace_back(L("Engineering Plate"));
    def->enum_labels.emplace_back(L("Smooth PEI Plate / High Temp Plate"));
    def->enum_labels.emplace_back(L("Textured PEI Plate"));
    def->enum_labels.emplace_back(L("Bambu Cool Plate SuperTack"));
    def->set_default_value(new ConfigOptionEnum<BedType>(btPC));

    // BBS
    def             = this->add("first_layer_print_sequence", coInts);
    def->label      = L("First layer print sequence");
    def->min        = 0;
    def->max        = 16;
    def->set_default_value(new ConfigOptionInts{0});

    def        = this->add("other_layers_print_sequence", coInts);
    def->label = L("Other layers print sequence");
    def->min   = 0;
    def->max   = 16;
    def->set_default_value(new ConfigOptionInts{0});

    def        = this->add("other_layers_print_sequence_nums", coInt);
    def->label = L("The number of other layers print sequence");
    def->set_default_value(new ConfigOptionInt{0});

    def = this->add("first_layer_sequence_choice", coEnum);
    def->category = L("Quality");
    def->label = L("First layer filament sequence");
    def->enum_keys_map = &ConfigOptionEnum<LayerSeq>::get_enum_values();
    def->enum_values.push_back("Auto");
    def->enum_values.push_back("Customize");
    def->enum_labels.push_back(L("Auto"));
    def->enum_labels.push_back(L("Customize"));
    def->mode = comSimple;
    def->set_default_value(new ConfigOptionEnum<LayerSeq>(flsAuto));

    def = this->add("other_layers_sequence_choice", coEnum);
    def->category = L("Quality");
    def->label = L("Other layers filament sequence");
    def->enum_keys_map = &ConfigOptionEnum<LayerSeq>::get_enum_values();
    def->enum_values.push_back("Auto");
    def->enum_values.push_back("Customize");
    def->enum_labels.push_back(L("Auto"));
    def->enum_labels.push_back(L("Customize"));
    def->mode = comSimple;
    def->set_default_value(new ConfigOptionEnum<LayerSeq>(flsAuto));

    def = this->add("before_layer_change_gcode", coString);
    def->label = L("Before layer change G-code");
    def->tooltip = L("This G-code is inserted at every layer change before lifting z");
    def->multiline = true;
    def->full_width = true;
    def->height = 5;
    def->mode = comDevelop;
    def->set_default_value(new ConfigOptionString(""));

    def = this->add("bottom_shell_layers", coInt);
    def->label = L("Bottom shell layers");
    def->category = L("Strength");
    def->tooltip =  L("This is the number of solid layers of bottom shell, including the bottom "
                      "surface layer. When the thickness calculated by this value is thinner "
                      "than bottom shell thickness, the bottom shell layers will be increased");
    def->full_label = L("Bottom shell layers");
    def->min = 0;
    def->set_default_value(new ConfigOptionInt(3));

    def = this->add("bottom_shell_thickness", coFloat);
    def->label = L("Bottom shell thickness");
    def->category = L("Strength");
    def->tooltip = L("The number of bottom solid layers is increased when slicing if the thickness calculated by bottom shells layers is "
                     "thinner than this value. This can avoid having too thin shell when layer height is small. 0 means that "
                     "this setting is disabled and thickness of bottom shell is absolutely determained by bottom shell layers");
    def->full_label = L("Bottom shell thickness");
    def->sidetext = L("mm");
    def->min = 0;
    def->set_default_value(new ConfigOptionFloat(0.));

    def = this->add("enable_overhang_bridge_fan", coBools);
    def->label = L("Force cooling for overhang and bridge");
    def->tooltip = L("Enable this option to optimize part cooling fan speed for overhang and bridge to get better cooling");
    def->mode = comSimple;
    def->set_default_value(new ConfigOptionBools{ true });

    def = this->add("overhang_fan_speed", coInts);
    def->label = L("Fan speed for overhang");
    def->tooltip = L("Force part cooling fan to be at this speed when printing bridge or overhang wall which has large overhang degree. "
                     "Forcing cooling for overhang and bridge can get better quality for these part");
    def->sidetext = "%";
    def->min = 0;
    def->max = 100;
    def->mode = comAdvanced;
    def->set_default_value(new ConfigOptionInts { 100 });

    def          = this->add("pre_start_fan_time", coFloats);
    def->label   = L("Pre start fan time");
    def->tooltip = L("Force fan start early(0-5 second) when encountering overhangs. "
                     "This is because the fan needs time to physically increase its speed.");
    def->sidetext = L("s");
    def->min      = 0.;
    def->max      = 5.;
    def->mode     = comAdvanced;
    def->set_default_value(new ConfigOptionFloats{0.0});

    def = this->add("overhang_fan_threshold", coEnums);
    def->label = L("Cooling overhang threshold");
    def->tooltip = L("Force cooling fan to be specific speed when overhang degree of printed part exceeds this value. "
                     "Expressed as percentage which indicides how much width of the line without support from lower layer. "
                     "0% means forcing cooling for all outer wall no matter how much overhang degree");
    def->sidetext = "";
    def->enum_keys_map = &ConfigOptionEnum<OverhangFanThreshold>::get_enum_values();
    def->mode = comAdvanced;
    def->enum_values.emplace_back("0%");
    def->enum_values.emplace_back("10%");
    def->enum_values.emplace_back("25%");
    def->enum_values.emplace_back("50%");
    def->enum_values.emplace_back("75%");
    def->enum_values.emplace_back("95%");
    def->enum_labels.emplace_back("0%");
    def->enum_labels.emplace_back("10%");
    def->enum_labels.emplace_back("25%");
    def->enum_labels.emplace_back("50%");
    def->enum_labels.emplace_back("75%");
    def->enum_labels.emplace_back("95%");
    def->set_default_value(new ConfigOptionEnumsGeneric{ (int)Overhang_threshold_bridge });

    def = this->add("overhang_threshold_participating_cooling", coEnums);
    def->label = L("Overhang threshold for participating cooling");
    def->tooltip = L("Decide which overhang part join the cooling function to slow down the speed."
                     "Expressed as percentage which indicides how much width of the line without support from lower layer. "
                     "100% means forcing cooling for all outer wall no matter how much overhang degree");
    def->sidetext = "";
    def->enum_keys_map = &ConfigOptionEnum<OverhangThresholdParticipatingCooling>::get_enum_values();
    def->mode = comAdvanced;
    def->enum_values.emplace_back("0%");
    def->enum_values.emplace_back("10%");
    def->enum_values.emplace_back("25%");
    def->enum_values.emplace_back("50%");
    def->enum_values.emplace_back("75%");
    def->enum_values.emplace_back("100%");
    def->enum_labels.emplace_back("0%");
    def->enum_labels.emplace_back("10%");
    def->enum_labels.emplace_back("25%");
    def->enum_labels.emplace_back("50%");
    def->enum_labels.emplace_back("75%");
    def->enum_labels.emplace_back("100%");
    def->set_default_value(new ConfigOptionEnumsGeneric{(int) Overhang_threshold_participating_cooling_bridge});

    def = this->add("bridge_angle", coFloat);
    def->label = L("Bridge direction");
    def->category = L("Strength");
    def->tooltip = L("Bridging angle override. If left to zero, the bridging angle will be calculated "
        "automatically. Otherwise the provided angle will be used for external bridges. "
        "Use 180°for zero angle.");
    def->sidetext = L("°");
    def->min = 0;
    def->mode = comAdvanced;
    def->set_default_value(new ConfigOptionFloat(0.));

    def = this->add("bridge_flow", coFloat);
    def->label = L("Bridge flow");
    def->category = L("Quality");
    def->tooltip = L("Decrease this value slightly(for example 0.9) to reduce the amount of material for bridge, "
                     "to improve sag");
    def->min = 0;
    def->max = 2.0;
    def->mode = comAdvanced;
    def->set_default_value(new ConfigOptionFloat(1));

    def = this->add("top_solid_infill_flow_ratio", coFloat);
    def->label = L("Top surface flow ratio");
    def->tooltip = L("This factor affects the amount of material for top solid infill. "
                     "You can decrease it slightly to have smooth surface finish");
    def->min = 0;
    def->max = 2;
    def->mode = comDevelop;
    def->set_default_value(new ConfigOptionFloat(1));

    def = this->add("initial_layer_flow_ratio", coFloat);
    def->label = L("Initial layer flow ratio");
    def->tooltip = L("This factor affects the amount of material for the initial layer");
    def->min = 0;
    def->max = 2;
    def->mode = comDevelop;
    def->set_default_value(new ConfigOptionFloat(1));

    def = this->add("top_one_wall_type", coEnum);
    def->label = L("Only one wall on top surfaces");
    def->category = L("Quality");
    def->tooltip = L("Use only one wall on flat top surface, to give more space to the top infill pattern. Could be applied on topmost surface or all top surface.");
    def->enum_keys_map = &ConfigOptionEnum<TopOneWallType>::get_enum_values();
    def->enum_values.push_back("not apply");
    def->enum_values.push_back("all top");
    def->enum_values.push_back("topmost");
    def->enum_labels.push_back(L("Not apply"));
    def->enum_labels.push_back(L("Top surfaces"));
    def->enum_labels.push_back(L("Topmost surface"));
    def->set_default_value(new ConfigOptionEnum<TopOneWallType>(TopOneWallType::Alltop));

    def          = this->add("top_area_threshold", coPercent);
    def->label   = L("Top area threshold");
    def->tooltip = L("The min width of top areas in percentage of perimeter line width.");
    def->sidetext = "%";
    def->min     = 0;
    def->max     = 500;
    def->mode    = comDevelop;
    def->set_default_value(new ConfigOptionPercent(200));

    def           = this->add("only_one_wall_first_layer", coBool);
    def->label    = L("Only one wall on first layer");
    def->category = L("Quality");
    def->tooltip  = L("Use only one wall on the first layer of model");
    def->set_default_value(new ConfigOptionBool(false));

    def = this->add("enable_overhang_speed", coBools);
    def->label = L("Slow down for overhang");
    def->category = L("Speed");
    def->tooltip = L("Enable this option to slow printing down for different overhang degree");
    def->mode = comAdvanced;
    def->nullable = true;
    def->set_default_value(new ConfigOptionBoolsNullable{ true });

    def = this->add("overhang_1_4_speed", coFloats);
    def->label = "10%";
    def->category = L("Speed");
    def->full_label = "10%";
    //def->tooltip = L("Speed for line of wall which has degree of overhang between 10% and 25% line width. "
    //                 "0 means using original wall speed");
    def->sidetext = L("mm/s");
    def->min = 0;
    def->mode = comAdvanced;
    def->nullable = true;
    def->set_default_value(new ConfigOptionFloatsNullable{0});

    def = this->add("overhang_2_4_speed", coFloats);
    def->label = "25%";
    def->category = L("Speed");
    def->full_label = "25%";
    //def->tooltip = L("Speed for line of wall which has degree of overhang between 25% and 50% line width. "
    //                 "0 means using original wall speed");
    def->sidetext = L("mm/s");
    def->min = 0;
    def->mode = comAdvanced;
    def->nullable = true;
    def->set_default_value(new ConfigOptionFloatsNullable{0});

    def = this->add("overhang_3_4_speed", coFloats);
    def->label = "50%";
    def->category = L("Speed");
    def->full_label = "50%";
    //def->tooltip = L("Speed for line of wall which has degree of overhang between 50% and 75% line width. 0 means using original wall speed");
    def->sidetext = L("mm/s");
    def->min = 0;
    def->mode = comAdvanced;
    def->nullable = true;
    def->set_default_value(new ConfigOptionFloatsNullable{0});

    def = this->add("overhang_4_4_speed", coFloats);
    def->label = "75%";
    def->category = L("Speed");
    def->full_label = "75%";
    // def->tooltip = L("Speed for line of wall which has degree of overhang between 75% and 100% line width. 0 means using original wall speed");
    def->sidetext = L("mm/s");
    def->min = 0;
    def->mode = comAdvanced;
    def->nullable = true;
    def->set_default_value(new ConfigOptionFloatsNullable{0});

    def = this->add("overhang_totally_speed", coFloats);
    def->label = L("100%");
    def->category = L("Speed");
    def->full_label = "100%";
    def->tooltip    = L("Speed of 100% overhang wall which has 0 overlap with the lower layer.");
    def->sidetext = L("mm/s");
    def->min = 0;
    def->mode = comAdvanced;
    def->nullable = true;
    def->set_default_value(new ConfigOptionFloatsNullable{ 10 });

    def = this->add("bridge_speed", coFloats);
    def->label = L("Bridge");
    def->category = L("Speed");
    def->tooltip = L("Speed of bridge and completely overhang wall");
    def->sidetext = L("mm/s");
    def->min = 0;
    def->mode = comAdvanced;
    def->nullable = true;
    def->set_default_value(new ConfigOptionFloatsNullable{25});

    def = this->add("brim_width", coFloat);
    def->label = L("Brim width");
    def->category = L("Support");
    def->tooltip = L("Distance from model to the outermost brim line");
    def->sidetext = L("mm");
    def->min = 0;
    def->max = 100;
    def->mode = comSimple;
    def->set_default_value(new ConfigOptionFloat(0.));

    def = this->add("brim_type", coEnum);
    def->label = L("Brim type");
    def->category = L("Support");
    def->tooltip = L("This controls the generation of the brim at outer and/or inner side of models. "
                     "Auto means the brim width is analysed and calculated automatically.");
    def->enum_keys_map = &ConfigOptionEnum<BrimType>::get_enum_values();
    def->enum_values.emplace_back("auto_brim");
    def->enum_values.emplace_back("brim_ears");
    def->enum_values.emplace_back("outer_only");
#if 1 //!BBL_RELEASE_TO_PUBLIC
    // BBS: The following two types are disabled
    def->enum_values.emplace_back("inner_only");
    def->enum_values.emplace_back("outer_and_inner");
#endif
    def->enum_values.emplace_back("no_brim");

    def->enum_labels.emplace_back(L("Auto"));
    def->enum_labels.emplace_back(L("Painted"));
    def->enum_labels.emplace_back(L("Outer brim only"));
#if 1 //!BBL_RELEASE_TO_PUBLIC
    // BBS: The following two types are disabled
    def->enum_labels.emplace_back(L("Inner brim only"));
    def->enum_labels.emplace_back(L("Outer and inner brim"));
#endif
    def->enum_labels.emplace_back(L("No-brim"));

    def->mode = comSimple;
    def->set_default_value(new ConfigOptionEnum<BrimType>(btAutoBrim));

    def = this->add("brim_object_gap", coFloat);
    def->label = L("Brim-object gap");
    def->category = L("Support");
    def->tooltip = L("A gap between innermost brim line and object can make brim be removed more easily");
    def->sidetext = L("mm");
    def->min = 0;
    def->max = 2;
    def->mode = comAdvanced;
    def->set_default_value(new ConfigOptionFloat(0.));


    def = this->add("compatible_printers", coStrings);
    def->label = L("Compatible machine");
    def->mode = comDevelop;
    def->set_default_value(new ConfigOptionStrings());
    def->cli = ConfigOptionDef::nocli;

    //BBS.
    def        = this->add("upward_compatible_machine", coStrings);
    def->label = L("upward compatible machine");
    def->mode  = comDevelop;
    def->set_default_value(new ConfigOptionStrings());
    def->cli   = ConfigOptionDef::nocli;

    def = this->add("compatible_printers_condition", coString);
    def->label = L("Compatible machine condition");
    //def->tooltip = L("A boolean expression using the configuration values of an active printer profile. "
    //               "If this expression evaluates to true, this profile is considered compatible "
    //               "with the active printer profile.");
    def->mode = comDevelop;
    def->set_default_value(new ConfigOptionString());
    def->cli = ConfigOptionDef::nocli;

    def = this->add("compatible_prints", coStrings);
    def->label = L("Compatible process profiles");
    def->mode = comDevelop;
    def->set_default_value(new ConfigOptionStrings());
    def->cli = ConfigOptionDef::nocli;

    def = this->add("compatible_prints_condition", coString);
    def->label = L("Compatible process profiles condition");
    //def->tooltip = L("A boolean expression using the configuration values of an active print profile. "
    //               "If this expression evaluates to true, this profile is considered compatible "
    //               "with the active print profile.");
    def->mode = comDevelop;
    def->set_default_value(new ConfigOptionString());
    def->cli = ConfigOptionDef::nocli;

    // The following value is to be stored into the project file (AMF, 3MF, Config ...)
    // and it contains a sum of "compatible_printers_condition" values over the print and filament profiles.
    def = this->add("compatible_machine_expression_group", coStrings);
    def->set_default_value(new ConfigOptionStrings());
    def->cli = ConfigOptionDef::nocli;
    def = this->add("compatible_process_expression_group", coStrings);
    def->set_default_value(new ConfigOptionStrings());
    def->cli = ConfigOptionDef::nocli;

    //BBS: add logic for checking between different system presets
    def = this->add("different_settings_to_system", coStrings);
    def->set_default_value(new ConfigOptionStrings());
    def->cli = ConfigOptionDef::nocli;

    def = this->add("print_compatible_printers", coStrings);
    def->set_default_value(new ConfigOptionStrings());
    def->cli = ConfigOptionDef::nocli;

    def = this->add("print_sequence", coEnum);
    def->label = L("Print sequence");
    def->tooltip = L("Print sequence, layer by layer or object by object");
    def->enum_keys_map = &ConfigOptionEnum<PrintSequence>::get_enum_values();
    def->enum_values.push_back("by layer");
    def->enum_values.push_back("by object");
    def->enum_labels.push_back(L("By layer"));
    def->enum_labels.push_back(L("By object"));
    def->mode = comSimple;
    def->set_default_value(new ConfigOptionEnum<PrintSequence>(PrintSequence::ByLayer));

    def = this->add("slow_down_for_layer_cooling", coBools);
    def->label = L("Slow printing down for better layer cooling");
    def->tooltip = L("Enable this option to slow printing speed down to make the final layer time not shorter than "
                     "the layer time threshold in \"Max fan speed threshold\", so that layer can be cooled for a longer time. "
                     "This can improve the cooling quality for needle and small details");
    def->set_default_value(new ConfigOptionBools { true });

    def = this->add("default_acceleration", coFloats);
    def->label = L("Normal printing");
    def->tooltip = L("The default acceleration of both normal printing and travel except initial layer");
    def->sidetext = "mm/s²";
    def->min = 0;
    def->mode = comAdvanced;
    def->nullable = true;
    def->set_default_value(new ConfigOptionFloatsNullable{500.0});

    def           = this->add("travel_acceleration", coFloats);
    def->label    = L("Travel");
    def->tooltip  = L("The acceleration of travel except initial layer");
    def->sidetext = "mm/s²";
    def->min      = 0;
    def->mode     = comAdvanced;
    def->nullable = true;
    def->set_default_value(new ConfigOptionFloatsNullable{500.0});

    def           = this->add("initial_layer_travel_acceleration", coFloats);
    def->label    = L("Initial layer travel");
    def->tooltip  = L("The acceleration of travel of initial layer");
    def->sidetext = "mm/s²";
    def->min      = 0;
    def->mode     = comAdvanced;
    def->nullable = true;
    def->set_default_value(new ConfigOptionFloatsNullable{500.0});

    def = this->add("default_filament_profile", coStrings);
    def->label = L("Default filament profile");
    def->tooltip = L("Default filament profile when switch to this machine profile");
    def->set_default_value(new ConfigOptionStrings());
    def->cli = ConfigOptionDef::nocli;

    def = this->add("default_print_profile", coString);
    def->label = L("Default process profile");
    def->tooltip = L("Default process profile when switch to this machine profile");
    def->set_default_value(new ConfigOptionString());
    def->cli = ConfigOptionDef::nocli;

    def = this->add("activate_air_filtration",coBools);
    def->label = L("Activate air filtration");
    def->tooltip = L("Activate for better air filtration");
    def->mode = comSimple;
    def->set_default_value(new ConfigOptionBools{false});

    def = this->add("during_print_exhaust_fan_speed", coInts);
    def->label   = L("Fan speed");
    def->tooltip=L("Speed of exhaust fan during printing.This speed will overwrite the speed in filament custom gcode");
    def->sidetext = "%";
    def->min=0;
    def->max=100;
    def->mode = comSimple;
    def->set_default_value(new ConfigOptionInts{60});

    def = this->add("complete_print_exhaust_fan_speed", coInts);
    def->label = L("Fan speed");
    def->sidetext = "%";
    def->tooltip=L("Speed of exhuast fan after printing completes");
    def->min=0;
    def->max=100;
    def->mode = comSimple;
    def->set_default_value(new ConfigOptionInts{80});

    def = this->add("close_fan_the_first_x_layers", coInts);
    def->label = L("No cooling for the first");
    def->tooltip = L("Close all cooling fan for the first certain layers. Cooling fan of the first layer used to be closed "
                     "to get better build plate adhesion");
    def->sidetext = L("layers");
    def->min = 0;
    def->max = 1000;
    def->mode = comSimple;
    def->set_default_value(new ConfigOptionInts { 1 });

    def = this->add("bridge_no_support", coBool);
    def->label = L("Don't support bridges");
    def->category = L("Support");
    def->tooltip = L("Don't support the whole bridge area which makes support very large. "
                     "Bridge usually can be printing directly without support if not very long");
    def->mode = comAdvanced;
    def->set_default_value(new ConfigOptionBool(false));

    def = this->add("thick_bridges", coBool);
    def->label = L("Thick bridges");
    def->category = L("Quality");
    def->tooltip = L("If enabled, bridges are more reliable, can bridge longer distances, but may look worse. "
        "If disabled, bridges look better but are reliable just for shorter bridged distances.");
    def->mode = comAdvanced;
    def->set_default_value(new ConfigOptionBool(false));

    def = this->add("max_bridge_length", coFloat);
    def->label = L("Max bridge length");
    def->category = L("Support");
    def->tooltip = L("Max length of bridges that don't need support. Set it to 0 if you want all bridges to be supported, and set it to a very large value if you don't want any bridges to be supported.");
    def->sidetext = L("mm");
    def->min = 0;
    def->mode = comAdvanced;
    def->set_default_value(new ConfigOptionFloat(10));

    def = this->add("machine_end_gcode", coString);
    def->label = L("End G-code");
    def->tooltip = L("End G-code when finish the whole printing");
    def->multiline = true;
    def->full_width = true;
    def->height = 12;
    def->mode = comAdvanced;
    def->set_default_value(new ConfigOptionString("M104 S0 ; turn off temperature\nG28 X0  ; home X axis\nM84     ; disable motors\n"));

    def             = this->add("printing_by_object_gcode", coString);
    def->label      = L("Between Object Gcode");
    def->tooltip    = L("Insert Gcode between objects. This parameter will only come into effect when you print your models object by object");
    def->multiline  = true;
    def->full_width = true;
    def->height     = 12;
    def->mode       = comAdvanced;
    def->set_default_value(new ConfigOptionString(""));

    def = this->add("filament_end_gcode", coStrings);
    def->label = L("End G-code");
    def->tooltip = L("End G-code when finish the printing of this filament");
    def->multiline = true;
    def->full_width = true;
    def->height = 120;
    def->mode = comAdvanced;
    def->set_default_value(new ConfigOptionStrings { " " });

    def = this->add("ensure_vertical_shell_thickness", coEnum);
    def->label = L("Ensure vertical shell thickness");
    def->category = L("Strength");
    def->tooltip = L("Add solid infill near sloping surfaces to guarantee the vertical shell thickness "
        "(top+bottom solid layers)");
    def->mode = comAdvanced;
    def->enum_keys_map = &ConfigOptionEnum<EnsureVerticalThicknessLevel>::get_enum_values();
    def->enum_values.push_back("disabled");
    def->enum_values.push_back("partial");
    def->enum_values.push_back("enabled");
    def->enum_labels.push_back(L("Disabled"));
    def->enum_labels.push_back(L("Partial"));
    def->enum_labels.push_back(L("Enabled"));
    def->set_default_value(new ConfigOptionEnum<EnsureVerticalThicknessLevel>(EnsureVerticalThicknessLevel::evtEnabled));

    def = this->add("vertical_shell_speed",coFloatsOrPercents);
    def->label = L("Vertical shell speed");
    def->tooltip = L("Speed for vertical shells with overhang regions. If expressed as percentage (for example: 80%) it will be calculated on"
                     "the internal solid infill speed above");
    def->category = L("Speed");
    def->sidetext   = L("mm/s or %");
    def->ratio_over = "internal_solid_infill_speed";
    def->min = 0;
    def->mode = comAdvanced;
    def->nullable = true;
    def->set_default_value(new ConfigOptionFloatsOrPercentsNullable{FloatOrPercent(80, true)});

    def = this->add("detect_floating_vertical_shell", coBool);
    def->label = L("Detect floating vertical shells");
    def->tooltip = L("Detect overhang paths in vertical shells and slow them by bridge speed.");
    def->mode = comAdvanced;
    def->set_default_value(new ConfigOptionBool{true});

    def = this->add("internal_bridge_support_thickness", coFloat);
    def->label = L("Internal bridge support thickness");
    def->category = L("Strength");
    def->tooltip = L("When sparse infill density is low, the internal solid infill or internal bridge may have no archor at the end of line. "
                     "This causes falling and bad quality when printing internal solid infill. "
                     "When enable this feature, loop paths will be added to the sparse fill of the lower layers for specific thickness, so that better archor can be provided for internal bridge. "
                     "0 means disable this feature");
    def->sidetext = L("mm");
    def->min = 0;
    def->max = 2;
    def->mode = comAdvanced;
    def->set_default_value(new ConfigOptionFloat(0));

    auto def_top_fill_pattern = def = this->add("top_surface_pattern", coEnum);
    def->label = L("Top surface pattern");
    def->category = L("Strength");
    def->tooltip = L("Line pattern of top surface infill");
    def->enum_keys_map = &ConfigOptionEnum<InfillPattern>::get_enum_values();
    def->enum_values.push_back("concentric");
    def->enum_values.push_back("zig-zag");
    def->enum_values.push_back("monotonic");
    def->enum_values.push_back("monotonicline");
    def->enum_values.push_back("alignedrectilinear");
    def->enum_values.push_back("hilbertcurve");
    def->enum_values.push_back("archimedeanchords");
    def->enum_values.push_back("octagramspiral");
    def->enum_labels.push_back(L("Concentric"));
    def->enum_labels.push_back(L("Rectilinear"));
    def->enum_labels.push_back(L("Monotonic"));
    def->enum_labels.push_back(L("Monotonic line"));
    def->enum_labels.push_back(L("Aligned Rectilinear"));
    def->enum_labels.push_back(L("Hilbert Curve"));
    def->enum_labels.push_back(L("Archimedean Chords"));
    def->enum_labels.push_back(L("Octagram Spiral"));
    def->set_default_value(new ConfigOptionEnum<InfillPattern>(ipRectilinear));

    def = this->add("bottom_surface_pattern", coEnum);
    def->label = L("Bottom surface pattern");
    def->category = L("Strength");
    def->tooltip = L("Line pattern of bottom surface infill, not bridge infill");
    def->enum_keys_map = &ConfigOptionEnum<InfillPattern>::get_enum_values();
    def->enum_values = def_top_fill_pattern->enum_values;
    def->enum_labels = def_top_fill_pattern->enum_labels;
    def->set_default_value(new ConfigOptionEnum<InfillPattern>(ipRectilinear));

    def                = this->add("internal_solid_infill_pattern", coEnum);
    def->label         = L("Internal solid infill pattern");
    def->category      = L("Strength");
    def->tooltip       = L("Line pattern of internal solid infill. if the detect narrow internal solid infill be enabled, the concentric pattern will be used for the small area.");
    def->enum_keys_map = &ConfigOptionEnum<InfillPattern>::get_enum_values();
    def->enum_values   = def_top_fill_pattern->enum_values;
    def->enum_labels   = def_top_fill_pattern->enum_labels;
    def->set_default_value(new ConfigOptionEnum<InfillPattern>(ipRectilinear));

    def = this->add("outer_wall_line_width", coFloat);
    def->label = L("Outer wall");
    def->category = L("Quality");
    def->tooltip = L("Line width of outer wall");
    def->sidetext = L("mm");
    def->min = 0;
    def->mode = comAdvanced;
    def->set_default_value(new ConfigOptionFloat(0));

    def = this->add("outer_wall_speed", coFloats);
    def->label = L("Outer wall");
    def->category = L("Speed");
    def->tooltip = L("Speed of outer wall which is outermost and visible. "
                     "It's used to be slower than inner wall speed to get better quality.");
    def->sidetext = L("mm/s");
    def->min = 0;
    def->mode = comAdvanced;
    def->nullable = true;
    def->set_default_value(new ConfigOptionFloatsNullable{60});


    def = this->add("small_perimeter_speed", coFloatsOrPercents);
    def->label = L("Small perimeters");
    def->category = L("Speed");
    def->tooltip  = L("This setting will affect the speed of perimeters having radius <= small perimeter threshold"
                       "(usually holes). If expressed as percentage (for example: 80%) it will be calculated on"
                       "the outer wall speed setting above. Set to zero for auto.");
    def->sidetext   = L("mm/s or %");
    def->ratio_over = "outer_wall_speed";
    def->min = 0;
    def->mode = comAdvanced;
    def->nullable = true;
    def->set_default_value(new ConfigOptionFloatsOrPercentsNullable{FloatOrPercent(50, true)});

    def = this->add("small_perimeter_threshold", coFloats);
    def->label = L("Small perimter threshold");
    def->category = L("Speed");
    def->tooltip = L("This sets the threshold for small perimeter length. Default threshold is 0mm");
    def->sidetext = L("mm");
    def->min = 0;
    def->mode = comAdvanced;
    def->nullable = true;
    def->set_default_value(new ConfigOptionFloatsNullable{0});

    def = this->add("wall_sequence", coEnum);
    def->label = L("Order of walls");
    def->category = L("Quality");
    def->tooltip = L("Print sequence of inner wall and outer wall. ");
    def->enum_keys_map = &ConfigOptionEnum<WallSequence>::get_enum_values();
    def->enum_values.push_back("inner wall/outer wall");
    def->enum_values.push_back("outer wall/inner wall");
    def->enum_values.push_back("inner-outer-inner wall");
    def->enum_labels.push_back(L("inner/outer"));
    def->enum_labels.push_back(L("outer/inner"));
    def->enum_labels.push_back(L("inner wall/outer wall/inner wall"));
    def->mode = comAdvanced;
    def->set_default_value(new ConfigOptionEnum<WallSequence>(WallSequence::InnerOuter));

    def = this->add("is_infill_first",coBool);
    def->label    = L("Print infill first");
    def->tooltip  = L("Order of wall/infill. false means print wall first. ");
    def->category = L("Quality");
    def->mode     = comAdvanced;
    def->set_default_value(new ConfigOptionBool{false});

    def = this->add("extruder", coInt);
    def->gui_type = ConfigOptionDef::GUIType::i_enum_open;
    def->label = L("Extruder");
    def->category = L("Extruders");
    //def->tooltip = L("The extruder to use (unless more specific extruder settings are specified). "
    //               "This value overrides perimeter and infill extruders, but not the support extruders.");
    def->min = 0;  // 0 = inherit defaults
    def->enum_labels.push_back(L("default"));  // override label for item 0
    def->enum_labels.push_back("1");
    def->enum_labels.push_back("2");
    def->enum_labels.push_back("3");
    def->enum_labels.push_back("4");
    def->enum_labels.push_back("5");
    def->mode = comDevelop;

    def = this->add("extruder_clearance_height_to_rod", coFloat);
    def->label = L("Height to rod");
    def->tooltip = L("Distance of the nozzle tip to the lower rod. "
        "Used for collision avoidance in by-object printing.");
    def->sidetext = L("mm");
    def->min = 0;
    def->mode = comAdvanced;
    def->set_default_value(new ConfigOptionFloat(40));

    // BBS
    def = this->add("extruder_clearance_height_to_lid", coFloat);
    def->label = L("Height to lid");
    def->tooltip = L("Distance of the nozzle tip to the lid. "
        "Used for collision avoidance in by-object printing.");
    def->sidetext = L("mm");
    def->min = 0;
    def->mode = comAdvanced;
    def->set_default_value(new ConfigOptionFloat(120));

    def = this->add("extruder_clearance_dist_to_rod", coFloat);
    def->label = L("Distance to rod");
    def->tooltip = L("Horizontal distance of the nozzle tip to the rod's farther edge. Used for collision avoidance in by-object printing.");
    def->sidetext = L("mm");
    def->min = 0;
    def->mode = comAdvanced;
    def->set_default_value(new ConfigOptionFloat(40));

    def = this->add("nozzle_height", coFloat);
    def->label = L("Nozzle height");
    def->tooltip = L("The height of nozzle tip.");
    def->sidetext = L("mm");
    def->min = 0;
    def->mode = comDevelop;
    def->set_default_value(new ConfigOptionFloat(4));

    def           = this->add("extruder_clearance_max_radius", coFloat);
    def->label    = L("Max Radius");
    def->tooltip  = L("Max clearance radius around extruder. Used for collision avoidance in by-object printing.");
    def->sidetext = L("mm");
    def->min      = 0;
    def->mode     = comAdvanced;
    def->set_default_value(new ConfigOptionFloat(68));

    def = this->add("grab_length",coFloats);
    def->label = L("Grab length");
    def->sidetext = L("mm");
    def->min = 0;
    def->mode = comDevelop;
    def->set_default_value(new ConfigOptionFloats({0}));

    def = this->add("extruder_colour", coStrings);
    def->label = L("Extruder Color");
    def->tooltip = L("Only used as a visual help on UI");
    def->gui_type = ConfigOptionDef::GUIType::color;
    // Empty string means no color assigned yet.
    def->mode = comDevelop;
    def->set_default_value(new ConfigOptionStrings { "" });

    def = this->add("extruder_offset", coPoints);
    def->label = L("Extruder offset");
    //def->tooltip = L("If your firmware doesn't handle the extruder displacement you need the G-code "
    //               "to take it into account. This option lets you specify the displacement of each extruder "
    //               "with respect to the first one. It expects positive coordinates (they will be subtracted "
    //               "from the XY coordinate).");
    def->sidetext = L("mm");
    def->mode = comAdvanced;
    def->min = -5;
    def->max = 5;
    def->set_default_value(new ConfigOptionPoints { Vec2d(0,0) });

    def = this->add("filament_flow_ratio", coFloats);
    def->label = L("Flow ratio");
    def->tooltip = L("The material may have volumetric change after switching between molten state and crystalline state. "
                     "This setting changes all extrusion flow of this filament in gcode proportionally. "
                     "Recommended value range is between 0.95 and 1.05. "
                     "Maybe you can tune this value to get nice flat surface when there has slight overflow or underflow");
    def->max = 2;
    def->mode = comAdvanced;
    def->nullable = true;
    def->set_default_value(new ConfigOptionFloatsNullable { 1. });

    def          = this->add("print_flow_ratio", coFloat);
    def->label   = L("Object flow ratio");
    def->tooltip = L("The flow ratio set by object, the meaning is the same as flow ratio.");
    def->mode    = comDevelop;
    def->max     = 2;
    def->min     = 0.01;
    def->set_default_value(new ConfigOptionFloat(1));

    def = this->add("enable_pressure_advance", coBools);
    def->label = L("Enable pressure advance");
    def->tooltip = L("Enable pressure advance, auto calibration result will be overwriten once enabled. Useless for Bambu Printer");
    def->set_default_value(new ConfigOptionBools{ false });

    def = this->add("pressure_advance", coFloats);
    def->label = L("Pressure advance");
    def->tooltip = L("Pressure advance(Klipper) AKA Linear advance factor(Marlin). Useless for Bambu Printer");
    def->max = 2;
    def->mode = comAdvanced;
    def->set_default_value(new ConfigOptionFloats{ 0.02 });


    def = this->add("filament_notes",coString);
    def->label= L("Filament notes");
    def->tooltip = L("You can put your notes regarding the filament here.");
    def->multiline = true;
    def->full_width = true;
    def->height = 13;
    def->mode =comAdvanced;
    def->set_default_value(new ConfigOptionString(""));

    def = this->add("process_notes",coString);
    def->label= L("Process notes");
    def->tooltip = L("You can put your notes regarding the process here.");
    def->multiline =true;
    def->full_width = true;
    def->height = 13;
    def->mode = comAdvanced;
    def->set_default_value(new ConfigOptionString(""));

    def = this->add("printer_notes",coString);
    def->label = L("Printer notes");
    def->tooltip = L("You can put your notes regarding the printer here.");
    def->multiline = true;
    def->full_width=true;
    def->height = 13;
    def->mode=comAdvanced;
    def->set_default_value(new ConfigOptionString(""));

    def = this->add("line_width", coFloat);
    def->label = L("Default");
    def->category = L("Quality");
    def->tooltip = L("Default line width if some line width is set to be zero");
    def->sidetext = L("mm");
    def->min = 0;
    def->max = 10;
    def->mode = comAdvanced;
    def->set_default_value(new ConfigOptionFloat(0.4));

    def = this->add("reduce_fan_stop_start_freq", coBools);
    def->label = L("Keep fan always on");
    def->tooltip = L("If enable this setting, part cooling fan will never be stopped and will run at least "
                     "at minimum speed to reduce the frequency of starting and stopping");
    def->set_default_value(new ConfigOptionBools { false });

    def = this->add("fan_cooling_layer_time", coInts);
    def->label = L("Layer time");
    def->tooltip = L("Part cooling fan will be enabled for layers of which estimated time is shorter than this value. "
                     "Fan speed is interpolated between the minimum and maximum fan speeds according to layer printing time");
    def->sidetext = L("s");
    def->min = 0;
    def->max = 1000;
    def->mode = comSimple;
    def->set_default_value(new ConfigOptionInts { 60 });

    def           = this->add("default_filament_colour", coStrings);
    def->label    = L("Default color");
    def->tooltip  = L("Default filament color");
    def->gui_type = ConfigOptionDef::GUIType::color;
    def->mode     = comAdvanced;
    def->set_default_value(new ConfigOptionStrings{""});

    def = this->add("filament_colour", coStrings);
    def->label = L("Color");
    def->tooltip = L("Only used as a visual help on UI");
    def->gui_type = ConfigOptionDef::GUIType::color;
    def->mode = comDevelop;
    def->set_default_value(new ConfigOptionStrings{ "#00AE42" });

    //bbs
    def          = this->add("required_nozzle_HRC", coInts);
    def->label   = L("Required nozzle HRC");
    def->tooltip = L("Minimum HRC of nozzle required to print the filament. Zero means no checking of nozzle's HRC.");
    def->min     = 0;
    def->max     = 500;
    def->mode    = comDevelop;
    def->set_default_value(new ConfigOptionInts{0});

    def = this->add("filament_map", coInts);
    def->label = L("Filament map to extruder");
    def->tooltip = L("Filament map to extruder");
    def->mode = comDevelop;
    def->set_default_value(new ConfigOptionInts{1});

    def = this->add("physical_extruder_map",coInts);
    def->label = "Map the logical extruder to physical extruder";
    def->tooltip = "Map the logical extruder to physical extruder";
    def->mode = comDevelop;
    def->set_default_value(new ConfigOptionInts{0});

    def                = this->add("filament_map_mode", coEnum);
    def->label         = L("filament mapping mode");
    def->tooltip = ("filament mapping mode used as plate param");
    def->enum_keys_map = &ConfigOptionEnum<FilamentMapMode>::get_enum_values();
    def->enum_values.push_back("Auto For Flush");
    def->enum_values.push_back("Auto For Match");
    def->enum_values.push_back("Manual");
    def->enum_values.push_back("Default");
    def->enum_labels.push_back(L("Auto For Flush"));
    def->enum_labels.push_back(L("Auto For Match"));
    def->enum_labels.push_back(L("Manual"));
    def->enum_labels.push_back(L("Default"));
    def->mode = comAdvanced;
    def->set_default_value(new ConfigOptionEnum<FilamentMapMode>(fmmAutoForFlush));

    def = this->add("filament_flush_temp", coInts);
    def->label = L("Flush temperature");
    def->tooltip = L("temperature when flushing filament. 0 indicates the upper bound of the recommended nozzle temperature range");
    def->mode = comAdvanced;
    def->nullable = true;
    def->min = 0;
    def->max = max_temp;
    def->sidetext = "°C";
    def->set_default_value(new ConfigOptionIntsNullable{0});

    def = this->add("filament_flush_volumetric_speed", coFloats);
    def->label = L("Flush volumetric speed");
    def->tooltip = L("Volumetric speed when flushing filament. 0 indicates the max volumetric speed");
    def->mode = comAdvanced;
    def->nullable = true;
    def->min = 0;
    def->max = 200;
    def->sidetext = L("mm³/s");
    def->set_default_value(new ConfigOptionFloatsNullable{ 0 });

    def = this->add("filament_max_volumetric_speed", coFloats);
    def->label = L("Max volumetric speed");
    def->tooltip = L("This setting stands for how much volume of filament can be melted and extruded per second. "
                     "Printing speed is limited by max volumetric speed, in case of too high and unreasonable speed setting. "
                     "Can't be zero");
    def->sidetext = L("mm³/s");
    def->min = 0;
    def->max = 200;
    def->mode = comAdvanced;
    def->nullable = true;
    def->set_default_value(new ConfigOptionFloatsNullable { 2. });

    def           = this->add("filament_ramming_volumetric_speed", coFloats);
    def->label    = L("Ramming volumetric speed");
    def->tooltip  = L("The maximum volumetric speed for ramming, where -1 means using the maximum volumetric speed.");
    def->sidetext = L("mm³/s");
    def->min      = -1;
    def->max      = 200;
    def->mode     = comAdvanced;
    def->nullable = true;
    def->set_default_value(new ConfigOptionFloatsNullable{-1});


    def = this->add("filament_minimal_purge_on_wipe_tower", coFloats);
    def->label = L("Minimal purge on wipe tower");
    //def->tooltip = L("After a tool change, the exact position of the newly loaded filament inside "
    //                 "the nozzle may not be known, and the filament pressure is likely not yet stable. "
    //                 "Before purging the print head into an infill or a sacrificial object, Slic3r will always prime "
    //                 "this amount of material into the wipe tower to produce successive infill or sacrificial object extrusions reliably.");
    def->sidetext = L("mm³");
    def->min = 0;
    def->mode = comDevelop;
    def->set_default_value(new ConfigOptionFloats { 15. });

    def = this->add("machine_load_filament_time", coFloat);
    def->label = L("Filament load time");
    def->tooltip = L("Time to load new filament when switch filament. For statistics only");
    def->sidetext = L("s");
    def->min = 0;
    def->mode = comAdvanced;
    def->set_default_value(new ConfigOptionFloat(0.0));

    def = this->add("machine_unload_filament_time", coFloat);
    def->label = L("Filament unload time");
    def->tooltip = L("Time to unload old filament when switch filament. For statistics only");
    def->sidetext = L("s");
    def->min = 0;
    def->mode = comAdvanced;
    def->set_default_value(new ConfigOptionFloat(0.0));

    def = this->add("machine_switch_extruder_time", coFloat);
    def->label = L("Extruder switch time");
    def->tooltip = L("Time to switch extruder. For statistics only");
    def->min = 0;
    def->sidetext = L("s");
    def->mode = comAdvanced;
    def->set_default_value(new ConfigOptionFloat(5));

    def = this->add("hotend_cooling_rate", coFloats);
    def->nullable = true;
    def->set_default_value(new ConfigOptionFloatsNullable{2});

    def = this->add("hotend_heating_rate", coFloats);
    def->nullable = true;
    def->set_default_value(new ConfigOptionFloatsNullable{2});

    def = this->add("enable_pre_heating", coBool);
    def->set_default_value(new ConfigOptionBool(false));

    def = this->add("bed_temperature_formula", coEnum);
    def->label = L("Bed temperature type");
    def->tooltip = L("This option determines how the bed temperature is set during slicing: based on the temperature of the first filament or the highest temperature of the printed filaments.");
    def->mode = comDevelop;
    def->enum_keys_map = &ConfigOptionEnum<BedTempFormula>::get_enum_values();
    def->enum_values.push_back("by_first_filament");
    def->enum_values.push_back("by_highest_temp");
    def->enum_labels.push_back(L("By First filament"));
    def->enum_labels.push_back(L("By Highest Temp"));
    def->set_default_value(new ConfigOptionEnum<BedTempFormula>(BedTempFormula::btfFirstFilament));

    def = this->add("filament_diameter", coFloats);
    def->label = L("Diameter");
    def->tooltip = L("Filament diameter is used to calculate extrusion in gcode, so it's important and should be accurate");
    def->sidetext = L("mm");
    def->min = 0;
    def->set_default_value(new ConfigOptionFloats { 1.75 });

    def = this->add("filament_shrink", coPercents);
    def->label = L("Shrinkage");
    // xgettext:no-c-format, no-boost-format
    def->tooltip = L("Enter the shrinkage percentage that the filament will get after cooling (94% if you measure 94mm instead of 100mm)."
        " The part will be scaled in xy to compensate."
        " Only the filament used for the perimeter is taken into account."
        "\nBe sure to allow enough space between objects, as this compensation is done after the checks.");
    def->sidetext = L("%");
    def->ratio_over = "";
    def->min = 10;
    def->mode = comAdvanced;
    def->set_default_value(new ConfigOptionPercents{ 100 });

    def           = this->add("filament_adhesiveness_category", coInts);
    def->label    = L("Adhesiveness Category");
    def->tooltip  = L("Filament category");
    def->min      = 0;
    def->mode     = comDevelop;
    def->set_default_value(new ConfigOptionInts{0});

    def = this->add("filament_density", coFloats);
    def->label = L("Density");
    def->tooltip = L("Filament density. For statistics only");
    def->sidetext = L("g/cm³");
    def->min = 0;
    def->mode = comAdvanced;
    def->set_default_value(new ConfigOptionFloats { 0. });

    def = this->add("filament_type", coStrings);
    def->label = L("Type");
    def->tooltip = L("The material type of filament");
    def->gui_type = ConfigOptionDef::GUIType::f_enum_open;
    def->gui_flags = "show_value";
    def->enum_values.push_back("PLA");
    def->enum_values.push_back("ABS");
    def->enum_values.push_back("ASA");
    def->enum_values.push_back("ASA-CF");
    def->enum_values.push_back("PETG");
    def->enum_values.push_back("PCTG");
    def->enum_values.push_back("TPU");
    def->enum_values.push_back("TPU-AMS");
    def->enum_values.push_back("PC");
    def->enum_values.push_back("PA");
    def->enum_values.push_back("PA-CF");
    def->enum_values.push_back("PA-GF");
    def->enum_values.push_back("PA6-CF");
    def->enum_values.push_back("PLA-CF");
    def->enum_values.push_back("PET-CF");
    def->enum_values.push_back("PETG-CF");
    def->enum_values.push_back("PVA");
    def->enum_values.push_back("HIPS");
    def->enum_values.push_back("PLA-AERO");
    def->enum_values.push_back("PPS");
    def->enum_values.push_back("PPS-CF");
    def->enum_values.push_back("PPA-CF");
    def->enum_values.push_back("PPA-GF");
    def->enum_values.push_back("ABS-GF");
    def->enum_values.push_back("ASA-AERO");
    def->enum_values.push_back("PE");
    def->enum_values.push_back("PP");
    def->enum_values.push_back("EVA");
    def->enum_values.push_back("PHA");
    def->enum_values.push_back("BVOH");
    def->enum_values.push_back("PE-CF");
    def->enum_values.push_back("PP-CF");
    def->enum_values.push_back("PP-GF");

    def->mode = comSimple;
    def->set_default_value(new ConfigOptionStrings { "PLA" });

    def = this->add("filament_soluble", coBools);
    def->label = L("Soluble material");
    def->tooltip = L("Soluble material is commonly used to print support and support interface");
    def->mode = comDevelop;
    def->set_default_value(new ConfigOptionBools { false });

    def                = this->add("filament_scarf_seam_type", coEnums);
    def->label         = L("Scarf seam type");
    def->tooltip       = L("Set scarf seam type for this filament. This setting could minimize seam visibiliy.");
    def->enum_keys_map = &ConfigOptionEnum<SeamScarfType>::get_enum_values();
    def->enum_values.push_back("none");
    def->enum_values.push_back("external");
    def->enum_values.push_back("all");
    def->enum_labels.push_back(L("None"));
    def->enum_labels.push_back(L("Contour"));
    def->enum_labels.push_back(L("Contour and hole"));
    def->mode = comAdvanced;
    def->set_default_value(new ConfigOptionEnumsGeneric{0});

    def          = this->add("filament_scarf_height", coFloatsOrPercents);
    def->label   = L("Scarf start height");
    def->tooltip    = L("This amount can be specified in millimeters or as a percentage of the current layer height.");
    def->min        = 0;
    def->ratio_over = "layer_height";
    def->sidetext   = L("mm/%");
    def->mode    = comAdvanced;
    def->set_default_value(new ConfigOptionFloatsOrPercents{FloatOrPercent( 0, 10)});

    def        = this->add("filament_scarf_gap", coFloatsOrPercents);
    def->label = L("Scarf slope gap");
    def->tooltip    = L("In order to reduce the visiblity of the seam in closed loop, the inner wall and outer wall are shortened by a specified amount.");
    def->min   = 0;
    def->ratio_over = "nozzle_diameter";
    def->sidetext   = L("mm/%");
    def->mode  = comAdvanced;
    def->set_default_value(new ConfigOptionFloatsOrPercents{FloatOrPercent(0, 0)});

    def        = this->add("filament_scarf_length", coFloats);
    def->label = L("Scarf length");
    def->tooltip = L("Length of the scarf. Setting this parameter to zero effectively disables the scarf.");
    def->min   = 0;
    def->sidetext = "mm";
    def->mode  = comAdvanced;
    def->set_default_value(new ConfigOptionFloats{10});

    def           = this->add("filament_change_length", coFloats);
    def->label    = L("Filament ramming length");
    def->tooltip  = L("When changing the extruder, it is recommended to extrude a certain length of filament from the original extruder. This helps minimize nozzle oozing.");
    def->sidetext = L("mm");
    def->min      = 0;
    def->mode     = comAdvanced;
    def->set_default_value(new ConfigOptionFloats{10});

    def          = this->add("filament_is_support", coBools);
    def->label   = L("Support material");
    def->tooltip = L("Support material is commonly used to print support and support interface");
    def->mode    = comDevelop;
    def->set_default_value(new ConfigOptionBools{false});

    // defined in bits
    // 0 means cannot support, 1 means support
    // 0 bit: can support in left extruder
    // 1 bit: can support in right extruder
    def          = this->add("filament_printable", coInts);
    def->label   = L("Filament printable");
    def->tooltip = L("The filament is printable in extruder");
    def->mode    = comDevelop;
    def->set_default_value(new ConfigOptionInts{3});

    // BBS
    def = this->add("filament_prime_volume", coFloats);
    def->label = L("Filament prime volume");
    def->tooltip = L("The volume of material to prime extruder on tower.");
    def->sidetext = L("mm³");
    def->min = 1.0;
    def->mode = comSimple;
    def->set_default_value(new ConfigOptionFloats{45.});

    // BBS
    def = this->add("temperature_vitrification", coInts);
    def->label = L("Softening temperature");
    def->tooltip = L("The material softens at this temperature, so when the bed temperature is equal to or greater than it, it's highly recommended to open the front door and/or remove the upper glass to avoid cloggings.");
    def->mode = comSimple;
    def->set_default_value(new ConfigOptionInts{ 100 });

    def          = this->add("filament_ramming_travel_time", coFloats);
    def->label   = L("Travel time after ramming");
    def->tooltip = L("To prevent oozing, the nozzle will perform a reverse travel movement for a certain period after "
                     "the ramming is complete. The setting define the travel time.");
    def->mode    = comAdvanced;
    def->sidetext = "s";
    def->min      = 0;
    def->nullable = true;
    def->set_default_value(new ConfigOptionFloatsNullable{0});

    def           = this->add("filament_pre_cooling_temperature", coInts);
    def->label    = L("Precooling target temperature");
    def->tooltip  = L("To prevent oozing, the nozzle temperature will be cooled during ramming. Therefore, the ramming time must be greater than the cooldown time. 0 means disabled.");
    //def->gui_type = ConfigOptionDef::GUIType::i_enum_open;
    def->mode     = comAdvanced;
    def->sidetext = "°C";
    def->min      = 0;
    //def->enum_values.push_back("-1");
    //def->enum_labels.push_back(L("None"));
    def->nullable = true;
    def->set_default_value(new ConfigOptionIntsNullable{0});

    def = this->add("filament_cost", coFloats);
    def->label = L("Price");
    def->tooltip = L("Filament price. For statistics only");
    def->sidetext = L("money/kg");
    def->min = 0;
    def->mode = comAdvanced;
    def->set_default_value(new ConfigOptionFloats { 0. });

    def = this->add("filament_settings_id", coStrings);
    def->set_default_value(new ConfigOptionStrings { "" });
    //BBS: open this option to command line
    //def->cli = ConfigOptionDef::nocli;

    def = this->add("filament_ids", coStrings);
    def->set_default_value(new ConfigOptionStrings());
    def->cli = ConfigOptionDef::nocli;

    def = this->add("filament_vendor", coStrings);
    def->label = L("Vendor");
    def->tooltip = L("Vendor of filament. For show only");
    def->mode = comAdvanced;
    def->set_default_value(new ConfigOptionStrings{L("(Undefined)")});
    def->cli = ConfigOptionDef::nocli;

    def = this->add("infill_direction", coFloat);
    def->label = L("Infill direction");
    def->category = L("Strength");
    def->tooltip = L("Angle for sparse infill pattern, which controls the start or main direction of line");
    def->sidetext = L("°");
    def->min = 0;
    def->max = 360;
    def->mode = comAdvanced;
    def->set_default_value(new ConfigOptionFloat(45));

    def = this->add("sparse_infill_density", coPercent);
    def->label = L("Sparse infill density");
    def->category = L("Strength");
    def->tooltip = L("Density of internal sparse infill, 100% means solid throughout");
    def->sidetext = "%";
    def->min = 0;
    def->max = 100;
    def->set_default_value(new ConfigOptionPercent(20));

    def = this->add("sparse_infill_pattern", coEnum);
    def->label = L("Sparse infill pattern");
    def->category = L("Strength");
    def->tooltip = L("Line pattern for internal sparse infill");
    def->enum_keys_map = &ConfigOptionEnum<InfillPattern>::get_enum_values();
    def->enum_values.push_back("concentric");
    def->enum_values.push_back("zig-zag");
    def->enum_values.push_back("grid");
    def->enum_values.push_back("line");
    def->enum_values.push_back("cubic");
    def->enum_values.push_back("triangles");
    def->enum_values.push_back("tri-hexagon");
    def->enum_values.push_back("gyroid");
    def->enum_values.push_back("honeycomb");
    def->enum_values.push_back("adaptivecubic");
    def->enum_values.push_back("alignedrectilinear");
    def->enum_values.push_back("3dhoneycomb");
    def->enum_values.push_back("hilbertcurve");
    def->enum_values.push_back("archimedeanchords");
    def->enum_values.push_back("octagramspiral");
    def->enum_values.push_back("supportcubic");
    def->enum_values.push_back("lightning");
    def->enum_values.push_back("crosshatch");
    def->enum_values.push_back("zigzag");
    def->enum_values.push_back("crosszag");
    def->enum_values.push_back("lockedzag");
    def->enum_labels.push_back(L("Concentric"));
    def->enum_labels.push_back(L("Rectilinear"));
    def->enum_labels.push_back(L("Grid"));
    def->enum_labels.push_back(L("Line"));
    def->enum_labels.push_back(L("Cubic"));
    def->enum_labels.push_back(L("Triangles"));
    def->enum_labels.push_back(L("Tri-hexagon"));
    def->enum_labels.push_back(L("Gyroid"));
    def->enum_labels.push_back(L("Honeycomb"));
    def->enum_labels.push_back(L("Adaptive Cubic"));
    def->enum_labels.push_back(L("Aligned Rectilinear"));
    def->enum_labels.push_back(L("3D Honeycomb"));
    def->enum_labels.push_back(L("Hilbert Curve"));
    def->enum_labels.push_back(L("Archimedean Chords"));
    def->enum_labels.push_back(L("Octagram Spiral"));
    def->enum_labels.push_back(L("Support Cubic"));
    def->enum_labels.push_back(L("Lightning"));
    def->enum_labels.push_back(L("Cross Hatch"));
    def->enum_labels.push_back(L("Zig Zag"));
    def->enum_labels.push_back(L("Cross Zag"));
    def->enum_labels.push_back(L("Locked Zag"));
    def->set_default_value(new ConfigOptionEnum<InfillPattern>(ipCubic));

    def = this->add("top_surface_acceleration", coFloats);
    def->label = L("Top surface");
    def->tooltip = L("Acceleration of top surface infill. Using a lower value may improve top surface quality");
    def->sidetext = "mm/s²";
    def->min = 0;
    def->mode = comAdvanced;
    def->nullable = true;
    def->set_default_value(new ConfigOptionFloatsNullable{500});

    def = this->add("outer_wall_acceleration", coFloats);
    def->label = L("Outer wall");
    def->tooltip = L("Acceleration of outer wall. Using a lower value can improve quality");
    def->sidetext = "mm/s²";
    def->min = 0;
    def->mode = comAdvanced;
    def->nullable = true;
    def->set_default_value(new ConfigOptionFloatsNullable{500});

    def = this->add("inner_wall_acceleration", coFloats);
    def->label = L("Inner wall");
    def->tooltip = L("Acceleration of inner walls. 0 means using normal printing acceleration");
    def->sidetext = "mm/s²";
    def->min = 0;
    def->mode = comAdvanced;
    def->nullable = true;
    def->set_default_value(new ConfigOptionFloatsNullable{0});

    def             = this->add("sparse_infill_acceleration", coFloatsOrPercents);
    def->label      = L("Sparse infill");
    def->tooltip    = L("Acceleration of sparse infill. If the value is expressed as a percentage (e.g. 100%), it will be calculated based on the default acceleration.");
    def->sidetext   = L("mm/s² or %");
    def->min        = 0;
    def->mode       = comAdvanced;
    def->ratio_over = "default_acceleration";
    def->nullable = true;
    def->set_default_value(new ConfigOptionFloatsOrPercentsNullable{FloatOrPercent(100, true)});

    def = this->add("initial_layer_acceleration", coFloats);
    def->label = L("Initial layer");
    def->tooltip = L("Acceleration of initial layer. Using a lower value can improve build plate adhensive");
    def->sidetext = "mm/s²";
    def->min = 0;
    def->mode = comAdvanced;
    def->nullable = true;
    def->set_default_value(new ConfigOptionFloatsNullable{300});

    def = this->add("accel_to_decel_enable", coBool);
    def->label = L("Enable accel_to_decel");
    def->tooltip = L("Klipper's max_accel_to_decel will be adjusted automatically");
    def->mode = comAdvanced;
    def->set_default_value(new ConfigOptionBool(false));

    def = this->add("accel_to_decel_factor", coPercent);
    def->label = L("accel_to_decel");
    def->tooltip = L("Klipper's max_accel_to_decel will be adjusted to this percent of acceleration");
    def->sidetext = "%";
    def->min = 1;
    def->max = 100;
    def->mode = comAdvanced;
    def->set_default_value(new ConfigOptionPercent(50));

    def = this->add("default_jerk", coFloat);
    def->label = L("Default");
    def->tooltip = L("Default jerk");
    def->sidetext = L("mm/s");
    def->min = 0;
    def->mode = comAdvanced;
    def->set_default_value(new ConfigOptionFloat(0));

    def = this->add("outer_wall_jerk", coFloat);
    def->label = L("Outer wall");
    def->tooltip = L("Jerk of outer walls");
    def->sidetext = L("mm/s");
    def->min = 0;
    def->mode = comAdvanced;
    def->set_default_value(new ConfigOptionFloat(9));

    def = this->add("inner_wall_jerk", coFloat);
    def->label = L("Inner wall");
    def->tooltip = L("Jerk of inner walls");
    def->sidetext = L("mm/s");
    def->min = 0;
    def->mode = comAdvanced;
    def->set_default_value(new ConfigOptionFloat(9));

    def = this->add("infill_jerk", coFloat);
    def->label = L("Infill");
    def->tooltip = L("Jerk of infill");
    def->sidetext = L("mm/s");
    def->min = 0;
    def->mode = comAdvanced;
    def->set_default_value(new ConfigOptionFloat(9));

    def           = this->add("top_surface_jerk", coFloat);
    def->label    = L("Top surface");
    def->tooltip  = L("Jerk of top surface");
    def->sidetext = L("mm/s");
    def->min      = 0;
    def->mode     = comAdvanced;
    def->set_default_value(new ConfigOptionFloat(9));

    def           = this->add("initial_layer_jerk", coFloat);
    def->label    = L("First layer");
    def->tooltip  = L("Jerk of first layer");
    def->sidetext = L("mm/s");
    def->min      = 0;
    def->mode     = comAdvanced;
    def->set_default_value(new ConfigOptionFloat(9));

    def           = this->add("travel_jerk", coFloat);
    def->label    = L("Travel");
    def->tooltip  = L("Jerk of travel");
    def->sidetext = L("mm/s");
    def->min      = 0;
    def->mode     = comAdvanced;
    def->set_default_value(new ConfigOptionFloat(9));

    def = this->add("initial_layer_line_width", coFloat);
    def->label = L("Initial layer");
    def->category = L("Quality");
    def->tooltip = L("Line width of initial layer");
    def->sidetext = L("mm");
    def->min = 0;
    def->mode = comAdvanced;
    def->set_default_value(new ConfigOptionFloat(0.4));

    def = this->add("initial_layer_print_height", coFloat);
    def->label = L("Initial layer height");
    def->category = L("Quality");
    def->tooltip = L("Height of initial layer. Making initial layer height thick slightly can improve build plate adhension");
    def->sidetext = L("mm");
    def->min = 0;
    def->set_default_value(new ConfigOptionFloat(0.2));

    //def = this->add("adaptive_layer_height", coBool);
    //def->label = L("Adaptive layer height");
    //def->category = L("Quality");
    //def->tooltip = L("Enabling this option means the height of every layer except the first will be automatically calculated "
    //    "during slicing according to the slope of the model’s surface.\n"
    //    "Note that this option only takes effect if no prime tower is generated in current plate.");
    //def->set_default_value(new ConfigOptionBool(0));

    def = this->add("initial_layer_speed", coFloats);
    def->label = L("Initial layer");
    def->tooltip = L("Speed of initial layer except the solid infill part");
    def->sidetext = L("mm/s");
    def->min = 0;
    def->mode = comAdvanced;
    def->nullable = true;
    def->set_default_value(new ConfigOptionFloatsNullable{30});

    def = this->add("initial_layer_infill_speed", coFloats);
    def->label = L("Initial layer infill");
    def->tooltip = L("Speed of solid infill part of initial layer");
    def->sidetext = L("mm/s");
    def->min = 1;
    def->mode = comAdvanced;
    def->nullable = true;
    def->set_default_value(new ConfigOptionFloatsNullable{60.0});

    def = this->add("nozzle_temperature_initial_layer", coInts);
    def->label = L("Initial layer");
    def->full_label = L("Initial layer nozzle temperature");
    def->tooltip = L("Nozzle temperature to print initial layer when using this filament");
    def->sidetext = "°C";
    def->min = 0;
    def->max = max_temp;
    def->nullable = true;
    def->set_default_value(new ConfigOptionIntsNullable { 200 });

    def = this->add("full_fan_speed_layer", coInts);
    def->label = L("Full fan speed at layer");
    //def->tooltip = L("Fan speed will be ramped up linearly from zero at layer \"close_fan_the_first_x_layers\" "
    //               "to maximum at layer \"full_fan_speed_layer\". "
    //               "\"full_fan_speed_layer\" will be ignored if lower than \"close_fan_the_first_x_layers\", in which case "
    //               "the fan will be running at maximum allowed speed at layer \"close_fan_the_first_x_layers\" + 1.");
    def->min = 0;
    def->max = 1000;
    def->mode = comDevelop;
    def->set_default_value(new ConfigOptionInts { 0 });

    def = this->add("fuzzy_skin", coEnum);
    def->label = L("Fuzzy Skin");
    def->category = L("Others");
    def->tooltip = L("Randomly jitter while printing the wall, so that the surface has a rough look. This setting controls "
                     "the fuzzy position");
    def->enum_keys_map = &ConfigOptionEnum<FuzzySkinType>::get_enum_values();
    def->enum_values.push_back("none");
    def->enum_values.push_back("external");
    def->enum_values.push_back("all");
    def->enum_values.push_back("allwalls");
    def->enum_labels.push_back(L("None"));
    def->enum_labels.push_back(L("Contour"));
    def->enum_labels.push_back(L("Contour and hole"));
    def->enum_labels.push_back(L("All walls"));
    def->mode = comSimple;
    def->set_default_value(new ConfigOptionEnum<FuzzySkinType>(FuzzySkinType::None));

    def = this->add("fuzzy_skin_thickness", coFloat);
    def->label = L("Fuzzy skin thickness");
    def->category = L("Others");
    def->tooltip = L("The width within which to jitter. It's adversed to be below outer wall line width");
    def->sidetext = L("mm");
    def->min = 0;
    def->max = 1;
    def->mode = comSimple;
    def->set_default_value(new ConfigOptionFloat(0.3));

    def = this->add("fuzzy_skin_point_distance", coFloat);
    def->label = L("Fuzzy skin point distance");
    def->category = L("Others");
    def->tooltip = L("The average distance between the random points introduced on each line segment");
    def->sidetext = L("mm");
    def->min = 0;
    def->max = 5;
    def->mode = comSimple;
    def->set_default_value(new ConfigOptionFloat(0.8));

    def           = this->add("filter_out_gap_fill", coFloat);
    def->label    = L("Filter out tiny gaps");
    def->tooltip  = L("Filter out gaps smaller than the threshold specified. This setting won't affact top/bottom layers");
    def->mode     = comDevelop;
    def->set_default_value(new ConfigOptionFloat(0));

    def           = this->add("precise_outer_wall", coBool);
    def->label    = L("Precise wall");
    def->category = L("Quality");
    def->tooltip  = L("Improve shell precision by adjusting outer wall spacing. This also improves layer consistency.");
    def->mode = comDevelop;
    def->set_default_value(new ConfigOptionBool{false});

    def = this->add("gap_infill_speed", coFloats);
    def->label = L("Gap infill");
    def->category = L("Speed");
    def->tooltip = L("Speed of gap infill. Gap usually has irregular line width and should be printed more slowly");
    def->sidetext = L("mm/s");
    def->min = 0;
    def->mode = comAdvanced;
    def->nullable = true;
    def->set_default_value(new ConfigOptionFloatsNullable{30});

    // BBS
    def          = this->add("precise_z_height", coBool);
    def->label   = L("Precise Z height");
    def->tooltip = L("Enable this to get precise z height of object after slicing. "
                     "It will get the precise object height by fine-tuning the layer heights of the last few layers. "
                     "Note that this is an experimental parameter.");
    def->mode    = comAdvanced;
    def->set_default_value(new ConfigOptionBool(0));

    // BBS
    def = this->add("enable_arc_fitting", coBool);
    def->label = L("Arc fitting");
    def->tooltip = L("Enable this to get a G-code file which has G2 and G3 moves. "
                     "And the fitting tolerance is the same as resolution");
    def->mode = comAdvanced;
    def->set_default_value(new ConfigOptionBool(0));
    // BBS
    def = this->add("gcode_add_line_number", coBool);
    def->label = L("Add line number");
    def->tooltip = L("Enable this to add line number(Nx) at the beginning of each G-Code line");
    def->mode = comDevelop;
    def->set_default_value(new ConfigOptionBool(0));

    // BBS
    def = this->add("scan_first_layer", coBool);
    def->label = L("Scan first layer");
    def->tooltip = L("Enable this to enable the camera on printer to check the quality of first layer");
    def->mode = comDevelop;
    def->set_default_value(new ConfigOptionBool(false));

    // BBS
    def = this->add("thumbnail_size", coPoints);
    def->label = L("Thumbnail size");
    def->tooltip = L("Decides the size of thumbnail stored in gcode files");
    def->mode = comDevelop;
    def->gui_type = ConfigOptionDef::GUIType::one_string;
    def->set_default_value(new ConfigOptionPoints{ Vec2d(50,50) });

    //BBS
    // def = this->add("spaghetti_detector", coBool);
    // def->label = L("Enable spaghetti detector");
    // def->tooltip = L("Enable the camera on printer to check spaghetti");
    // def->mode = comSimple;
    // def->set_default_value(new ConfigOptionBool(false));

    def = this->add("nozzle_type", coEnums);
    def->label = L("Nozzle type");
    def->tooltip = L("The metallic material of nozzle. This determines the abrasive resistance of nozzle, and "
                     "what kind of filament can be printed");
    def->enum_keys_map = &ConfigOptionEnum<NozzleType>::get_enum_values();
    def->enum_values.push_back("undefine");
    def->enum_values.push_back("hardened_steel");
    def->enum_values.push_back("stainless_steel");
    def->enum_values.push_back("tungsten_carbide");
    def->enum_values.push_back("brass");
    def->enum_labels.push_back(L("Undefine"));
    def->enum_labels.push_back(L("Hardened steel"));
    def->enum_labels.push_back(L("Stainless steel"));
    def->enum_labels.push_back(L("Tungsten carbide"));
    def->enum_labels.push_back(L("Brass"));
    def->mode = comDevelop;
    def->nullable = true;
    def->set_default_value(new ConfigOptionEnumsGenericNullable({ ntUndefine }));

    def = this->add("printer_structure", coEnum);
    def->label = L("Printer structure");
    def->tooltip = L("The physical arrangement and components of a printing device");
    def->enum_keys_map = &ConfigOptionEnum<PrinterStructure>::get_enum_values();
    def->enum_values.push_back("undefine");
    def->enum_values.push_back("corexy");
    def->enum_values.push_back("i3");
    def->enum_values.push_back("hbot");
    def->enum_values.push_back("delta");
    def->enum_labels.push_back(L("Undefine"));
    def->enum_labels.push_back("CoreXY");
    def->enum_labels.push_back("I3");
    def->enum_labels.push_back("Hbot");
    def->enum_labels.push_back("Delta");
    def->mode = comDevelop;
    def->set_default_value(new ConfigOptionEnum<PrinterStructure>(psUndefine));

    def = this->add("best_object_pos", coPoint);
    def->label = L("Best object position");
    def->tooltip = L("Best auto arranging position in range [0,1] w.r.t. bed shape.");
    def->mode = comAdvanced;
    def->set_default_value(new ConfigOptionPoint(Vec2d(0.5, 0.5)));

    def = this->add("auxiliary_fan", coBool);
    def->label = L("Auxiliary part cooling fan");
    def->tooltip = L("Enable this option if machine has auxiliary part cooling fan");
    def->mode = comDevelop;
    def->set_default_value(new ConfigOptionBool(false));

    def =this->add("support_chamber_temp_control",coBool);
    def->label=L("Support control chamber temperature");
    def->tooltip=L("This option is enabled if machine support controlling chamber temperature");
    def->mode=comDevelop;
    def->set_default_value(new ConfigOptionBool(false));
    def->readonly=false;

    def =this->add("support_air_filtration",coBool);
    def->label=L("Air filtration enhancement");
    def->tooltip=L("Enable this if printer support air filtration enhancement.");
    def->mode = comAdvanced;
    def->set_default_value(new ConfigOptionBool(false));

    def = this->add("gcode_flavor", coEnum);
    def->label = L("G-code flavor");
    def->tooltip = L("What kind of gcode the printer is compatible with");
    def->enum_keys_map = &ConfigOptionEnum<GCodeFlavor>::get_enum_values();
    def->enum_values.push_back("marlin");
    def->enum_values.push_back("klipper");
    //def->enum_values.push_back("reprap");
    //def->enum_values.push_back("reprapfirmware");
    //def->enum_values.push_back("repetier");
    //def->enum_values.push_back("teacup");
    //def->enum_values.push_back("makerware");
    //def->enum_values.push_back("marlin2");
    //def->enum_values.push_back("sailfish");
    //def->enum_values.push_back("mach3");
    //def->enum_values.push_back("machinekit");
    //def->enum_values.push_back("smoothie");
    //def->enum_values.push_back("no-extrusion");
    def->enum_labels.push_back("Marlin(legacy)");
    def->enum_labels.push_back("Klipper");
    //def->enum_labels.push_back("RepRap/Sprinter");
    //def->enum_labels.push_back("RepRapFirmware");
    //def->enum_labels.push_back("Repetier");
    //def->enum_labels.push_back("Teacup");
    //def->enum_labels.push_back("MakerWare (MakerBot)");
    //def->enum_labels.push_back("Marlin 2");
    //def->enum_labels.push_back("Sailfish (MakerBot)");
    //def->enum_labels.push_back("Mach3/LinuxCNC");
    //def->enum_labels.push_back("Machinekit");
    //def->enum_labels.push_back("Smoothie");
    //def->enum_labels.push_back(L("No extrusion"));
    def->mode = comAdvanced;
    def->readonly = false;
    def->set_default_value(new ConfigOptionEnum<GCodeFlavor>(gcfMarlinLegacy));

    //OrcaSlicer
    def = this->add("exclude_object", coBool);
    def->label = L("Exclude objects");
    def->tooltip = L("Enable this option to add EXCLUDE OBJECT command in g-code for klipper firmware printer");
    def->mode = comAdvanced;
    def->set_default_value(new ConfigOptionBool(1));

    //BBS
    def = this->add("infill_combination", coBool);
    def->label = L("Infill combination");
    def->category = L("Strength");
    def->tooltip = L("Automatically Combine sparse infill of several layers to print together to reduce time. Wall is still printed "
                     "with original layer height.");
    def->mode = comAdvanced;
    def->set_default_value(new ConfigOptionBool(false));

    def           = this->add("infill_shift_step", coFloat);
    def->label    = L("Infill shift step");
    def->category = L("Strength");
    def->tooltip  = L("This parameter adds a slight displacement to each layer of infill to create a cross texture.");
    def->sidetext = L("mm");
    def->min      = 0;
    def->max      = 10;
    def->mode     = comAdvanced;
    def->set_default_value(new ConfigOptionFloat(0.4));

    def           = this->add("infill_rotate_step", coFloat);
    def->label    = L("Infill rotate step");
    def->category = L("Strength");
    def->tooltip  = L("This parameter adds a slight rotation to each layer of infill to create a cross texture.");
    def->sidetext = L("°");
    def->min      = 0;
    def->max      = 360;
    def->mode     = comAdvanced;
    def->set_default_value(new ConfigOptionFloat(0));

    def           = this->add("skeleton_infill_density", coPercent);
    def->label    = L("Skeleton infill density");
    def->category = L("Strength");
    def->tooltip  = L("The remaining part of the model contour after removing a certain depth from the surface is called the skeleton. This parameter is used to adjust the density of this section."
                      "When two regions have the same sparse infill settings but different skeleton densities, their skeleton areas will develop overlapping sections."
                      "default is as same as infill density.");
    def->sidetext = "%";
    def->min      = 0;
    def->max      = 100;
    def->mode     = comAdvanced;
    def->set_default_value(new ConfigOptionPercent(15));

    def           = this->add("skin_infill_density", coPercent);
    def->label    = L("Skin infill density");
    def->category = L("Strength");
    def->tooltip  = L("The portion of the model's outer surface within a certain depth range is called the skin. This parameter is used to adjust the density of this section."
                      "When two regions have the same sparse infill settings but different skin densities, This area will not be split into two separate regions."
                     "default is as same as infill density.");
    def->sidetext = "%";
    def->min  = 0;
    def->max  = 100;
    def->mode = comAdvanced;
    def->set_default_value(new ConfigOptionPercent(15));

    def           = this->add("skin_infill_depth", coFloat);
    def->label    = L("Skin infill depth");
    def->category = L("Strength");
    def->tooltip  = L("The parameter sets the depth of skin.");
    def->sidetext = L("mm");
    def->min      = 0;
    def->max      = 100;
    def->mode     = comAdvanced;
    def->set_default_value(new ConfigOptionFloat(2.0));

    def           = this->add("infill_lock_depth", coFloat);
    def->label    = L("Infill lock depth");
    def->category = L("Strength");
    def->tooltip  = L("The parameter sets the overlapping depth between the interior and skin.");
    def->sidetext = L("mm");
    def->min      = 0;
    def->max      = 100;
    def->mode     = comAdvanced;
    def->set_default_value(new ConfigOptionFloat(1.0));

    def           = this->add("skin_infill_line_width", coFloat);
    def->label    = L("Skin line width");
    def->category = L("Strength");
    def->tooltip  = L("Adjust the line width of the selected skin paths.");
    def->sidetext = L("mm");
    def->min      = 0;
    def->mode     = comAdvanced;
    def->set_default_value(new ConfigOptionFloat(0.4));

    def           = this->add("skeleton_infill_line_width", coFloat);
    def->label    = L("Skeleton line width");
    def->category = L("Strength");
    def->tooltip  = L("Adjust the line width of the selected skeleton paths.");
    def->sidetext = L("mm");
    def->min      = 0;
    def->mode     = comAdvanced;
    def->set_default_value(new ConfigOptionFloat(0.4));

    def           = this->add("symmetric_infill_y_axis", coBool);
    def->label    = L("Symmetric infill y axis");
    def->category = L("Strength");
    def->tooltip  = L("If the model has two parts that are symmetric about the y-axis,"
                      " and you want these parts to have symmetric textures, please click this option on one of the parts.");
    def->mode     = comAdvanced;
    def->set_default_value(new ConfigOptionBool(false));

    auto def_infill_anchor_min = def = this->add("sparse_infill_anchor", coFloatOrPercent);
    def->label = L("Length of sparse infill anchor");
    def->category = L("Strength");
    def->tooltip = L("Connect a sparse infill line to an internal perimeter with a short segment of an additional perimeter. "
        "If expressed as percentage (example: 15%) it is calculated over sparse infill line width. "
        "Slicer tries to connect two close infill lines to a short perimeter segment. If no such perimeter segment "
        "shorter than infill_anchor_max is found, the infill line is connected to a perimeter segment at just one side "
        "and the length of the perimeter segment taken is limited to this parameter, but no longer than anchor_length_max. "
        "Set this parameter to zero to disable anchoring perimeters connected to a single infill line.");
    def->sidetext = L("mm or %");
    def->ratio_over = "sparse_infill_line_width";
    def->max_literal = 1000;
    def->gui_type = ConfigOptionDef::GUIType::f_enum_open;
    def->enum_values.push_back("0");
    def->enum_values.push_back("1");
    def->enum_values.push_back("2");
    def->enum_values.push_back("5");
    def->enum_values.push_back("10");
    def->enum_values.push_back("1000");
    def->enum_labels.push_back(L("0 (no open anchors)"));
    def->enum_labels.push_back("1 mm");
    def->enum_labels.push_back("2 mm");
    def->enum_labels.push_back("5 mm");
    def->enum_labels.push_back("10 mm");
    def->enum_labels.push_back(L("1000 (unlimited)"));
    def->mode = comAdvanced;
    def->set_default_value(new ConfigOptionFloatOrPercent(400, true));

    def = this->add("sparse_infill_anchor_max", coFloatOrPercent);
    def->label = L("Maximum length of sparse infill anchor");
    def->category = def_infill_anchor_min->category;
    def->tooltip = L("Connect a sparse infill line to an internal perimeter with a short segment of an additional perimeter. "
        "If expressed as percentage (example: 15%) it is calculated over sparse infill line width. "
        "Slicer tries to connect two close infill lines to a short perimeter segment. If no such perimeter segment "
        "shorter than this parameter is found, the infill line is connected to a perimeter segment at just one side "
        "and the length of the perimeter segment taken is limited to infill_anchor, but no longer than this parameter. "
        "Set this parameter to zero to disable anchoring.");
    def->sidetext = def_infill_anchor_min->sidetext;
    def->ratio_over = def_infill_anchor_min->ratio_over;
    def->max_literal = def_infill_anchor_min->max_literal;
    def->gui_type = def_infill_anchor_min->gui_type;
    def->enum_values.push_back("0");
    def->enum_values.push_back("1");
    def->enum_values.push_back("2");
    def->enum_values.push_back("5");
    def->enum_values.push_back("10");
    def->enum_values.push_back("1000");
    def->enum_labels.push_back(L("0 (not anchored)"));
    def->enum_labels.push_back("1 mm");
    def->enum_labels.push_back("2 mm");
    def->enum_labels.push_back("5 mm");
    def->enum_labels.push_back("10 mm");
    def->enum_labels.push_back(L("1000 (unlimited)"));
    def->mode = def_infill_anchor_min->mode;
    def->set_default_value(new ConfigOptionFloatOrPercent(20, false));

    def = this->add("sparse_infill_filament", coInt);
    def->label = L("Infill");
    def->category = L("Extruders");
    def->tooltip = L("Filament to print internal sparse infill.");
    def->min = 1;
    def->mode = comAdvanced;
    def->set_default_value(new ConfigOptionInt(1));

    def = this->add("sparse_infill_line_width", coFloat);
    def->label = L("Sparse infill");
    def->category = L("Quality");
    def->tooltip = L("Line width of internal sparse infill");
    def->sidetext = L("mm");
    def->min = 0;
    def->mode = comAdvanced;
    def->set_default_value(new ConfigOptionFloat(0.4));

    def = this->add("infill_wall_overlap", coPercent);
    def->label = L("Infill/Wall overlap");
    def->category = L("Strength");
    def->tooltip = L("Infill area is enlarged slightly to overlap with wall for better bonding. The percentage value is relative to line width of sparse infill");
    def->sidetext = "%";
    def->ratio_over = "inner_wall_line_width";
    def->mode = comAdvanced;
    def->set_default_value(new ConfigOptionPercent(15));

    def = this->add("sparse_infill_speed", coFloats);
    def->label = L("Sparse infill");
    def->category = L("Speed");
    def->tooltip = L("Speed of internal sparse infill");
    def->sidetext = L("mm/s");
    def->min = 0;
    def->mode = comAdvanced;
    def->nullable = true;
    def->set_default_value(new ConfigOptionFloatsNullable{100});

    def = this->add("inherits", coString);
    //def->label = L("Inherits profile");
    def->label = "Inherits profile";
    //def->tooltip = L("Name of parent profile");
    def->tooltip = "Name of parent profile";
    def->full_width = true;
    def->height = 5;
    def->set_default_value(new ConfigOptionString());
    def->cli = ConfigOptionDef::nocli;

    // The following value is to be stored into the project file (AMF, 3MF, Config ...)
    // and it contains a sum of "inherits" values over the print and filament profiles.
    def = this->add("inherits_group", coStrings);
    def->set_default_value(new ConfigOptionStrings());
    def->cli = ConfigOptionDef::nocli;

    def = this->add("interface_shells", coBool);
    //def->label = L("Interface shells");
    def->label = L("Interface shells");
    def->tooltip = L("Force the generation of solid shells between adjacent materials/volumes. "
                  "Useful for multi-extruder prints with translucent materials or manual soluble "
                  "support material");
    def->category = L("Quality");
    def->mode = comDevelop;
    def->set_default_value(new ConfigOptionBool(false));

    def           = this->add("mmu_segmented_region_max_width", coFloat);
    def->label    = L("Maximum width of a segmented region");
    def->tooltip  = L("Maximum width of a segmented region. Zero disables this feature.");
    def->sidetext = L("mm");
    def->min      = 0;
    def->category = L("Advanced");
    def->mode     = comAdvanced;
    def->set_default_value(new ConfigOptionFloat(0.));

    def           = this->add("mmu_segmented_region_interlocking_depth", coFloat);
    def->label    = L("Interlocking depth of a segmented region");
    //def->tooltip  = L("Interlocking depth of a segmented region. It will be ignored if "
    //                 "\"mmu_segmented_region_max_width\" is zero or if \"mmu_segmented_region_interlocking_depth\""
    //                 "is bigger then \"mmu_segmented_region_max_width\". Zero disables this feature.");
    def->tooltip  = L("Interlocking depth of a segmented region. Zero disables this feature.");
    def->sidetext = L("mm"); //(zero to disable)
    def->min      = 0;
    def->category = L("Advanced");
    def->mode     = comAdvanced;
    def->set_default_value(new ConfigOptionFloat(0.));

    def           = this->add("interlocking_beam", coBool);
    def->label    = L("Use beam interlocking");
    def->tooltip  = L("Generate interlocking beam structure at the locations where different filaments touch. This improves the adhesion between filaments, especially models printed in different materials.");
    def->category = L("Advanced");
    def->mode     = comAdvanced;
    def->set_default_value(new ConfigOptionBool(false));

    def           = this->add("interlocking_beam_width", coFloat);
    def->label    = L("Interlocking beam width");
    def->tooltip  = L("The width of the interlocking structure beams.");
    def->sidetext = L("mm");
    def->min      = 0.01;
    def->category = L("Advanced");
    def->mode     = comAdvanced;
    def->set_default_value(new ConfigOptionFloat(0.8));

    def           = this->add("interlocking_orientation", coFloat);
    def->label    = L("Interlocking direction");
    def->tooltip  = L("Orientation of interlock beams.");
    def->sidetext = L("°");
    def->min      = 0;
    def->max      = 360;
    def->category = L("Advanced");
    def->mode     = comAdvanced;
    def->set_default_value(new ConfigOptionFloat(22.5));

    def           = this->add("interlocking_beam_layer_count", coInt);
    def->label    = L("Interlocking beam layers");
    def->tooltip  = L("The height of the beams of the interlocking structure, measured in number of layers. Less layers is stronger, but more prone to defects.");
    def->min      = 1;
    def->category = L("Advanced");
    def->mode     = comAdvanced;
    def->set_default_value(new ConfigOptionInt(2));

    def           = this->add("interlocking_depth", coInt);
    def->label    = L("Interlocking depth");
    def->tooltip  = L("The distance from the boundary between filaments to generate interlocking structure, measured in cells. Too few cells will result in poor adhesion.");
    def->min      = 1;
    def->category = L("Advanced");
    def->mode     = comAdvanced;
    def->set_default_value(new ConfigOptionInt(2));

    def           = this->add("interlocking_boundary_avoidance", coInt);
    def->label    = L("Interlocking boundary avoidance");
    def->tooltip  = L("The distance from the outside of a model where interlocking structures will not be generated, measured in cells.");
    def->min      = 0;
    def->category = L("Advanced");
    def->mode     = comAdvanced;
    def->set_default_value(new ConfigOptionInt(2));

    def = this->add("ironing_type", coEnum);
    def->label = L("Ironing Type");
    def->category = L("Quality");
    def->tooltip = L("Ironing is using small flow to print on same height of surface again to make flat surface more smooth. "
                     "This setting controls which layer being ironed");
    def->enum_keys_map = &ConfigOptionEnum<IroningType>::get_enum_values();
    def->enum_values.push_back("no ironing");
    def->enum_values.push_back("top");
    def->enum_values.push_back("topmost");
    def->enum_values.push_back("solid");
    def->enum_labels.push_back(L("No ironing"));
    def->enum_labels.push_back(L("Top surfaces"));
    def->enum_labels.push_back(L("Topmost surface"));
    def->enum_labels.push_back(L("All solid layer"));
    def->mode = comAdvanced;
    def->set_default_value(new ConfigOptionEnum<IroningType>(IroningType::NoIroning));

    def                = this->add("ironing_pattern", coEnum);
    def->label         = L("Ironing Pattern");
    def->category      = L("Quality");
    def->enum_keys_map = &ConfigOptionEnum<InfillPattern>::get_enum_values();
    def->enum_values.push_back("concentric");
    def->enum_values.push_back("zig-zag");
    def->enum_labels.push_back(L("Concentric"));
    def->enum_labels.push_back(L("Rectilinear"));
    def->mode = comAdvanced;
    def->set_default_value(new ConfigOptionEnum<InfillPattern>(ipRectilinear));

    def = this->add("ironing_flow", coPercent);
    def->label = L("Ironing flow");
    def->category = L("Quality");
    def->tooltip = L("The amount of material to extrude during ironing. Relative to flow of normal layer height. "
                     "Too high value results in overextrusion on the surface");
    def->sidetext = "%";
    def->ratio_over = "layer_height";
    def->min = 0;
    def->max = 100;
    def->mode = comAdvanced;
    def->set_default_value(new ConfigOptionPercent(10));

    def = this->add("ironing_spacing", coFloat);
    def->label = L("Ironing line spacing");
    def->category = L("Quality");
    def->tooltip = L("The distance between the lines of ironing");
    def->sidetext = L("mm");
    def->min = 0;
    def->max = 1;
    def->mode = comAdvanced;
    def->set_default_value(new ConfigOptionFloat(0.1));

    def           = this->add("ironing_inset", coFloat);
    def->label    = L("Ironing inset");
    def->category = L("Quality");
    def->tooltip  = L("The distance to keep the from the edges of ironing line. 0 means not apply.");
    def->sidetext = L("mm");
    def->min      = 0;
    def->max      = 100;
    def->mode     = comAdvanced;
    def->set_default_value(new ConfigOptionFloat(0));

    def = this->add("ironing_speed", coFloat);
    def->label = L("Ironing speed");
    def->category = L("Quality");
    def->tooltip = L("Print speed of ironing lines");
    def->sidetext = L("mm/s");
    def->min = 0;
    def->mode = comAdvanced;
    def->set_default_value(new ConfigOptionFloat(20));

    def           = this->add("ironing_direction", coFloat);
    def->label    = L("ironing direction");
    def->category = L("Quality");
    def->tooltip  = L("Angle for ironing, which controls the relative angle between the top surface and ironing");
    def->sidetext = L("°");
    def->min      = 0;
    def->max      = 360;
    def->mode     = comDevelop;
    def->set_default_value(new ConfigOptionFloat(45));

    def = this->add("layer_change_gcode", coString);
    def->label = L("Layer change G-code");
    def->tooltip = L("This gcode part is inserted at every layer change after lift z");
    def->multiline = true;
    def->full_width = true;
    def->height = 5;
    def->mode = comAdvanced;
    def->set_default_value(new ConfigOptionString(""));

    def = this->add("time_lapse_gcode",coString);
    def->label = L("Time lapse G-code");
    def->multiline = true;
    def->full_width = true;
    def->height =5;
    def->mode = comAdvanced;
    def->set_default_value(new ConfigOptionString(""));

    def = this->add("silent_mode", coBool);
    def->label = L("Supports silent mode");
    def->tooltip = L("Whether the machine supports silent mode in which machine use lower acceleration to print");
    def->mode = comDevelop;
    def->set_default_value(new ConfigOptionBool(false));

    def = this->add("machine_pause_gcode", coString);
    def->label = L("Pause G-code");
    def->tooltip = L("This G-code will be used as a code for the pause print. User can insert pause G-code in gcode viewer");
    def->multiline = true;
    def->full_width = true;
    def->height = 12;
    def->mode = comAdvanced;
    def->set_default_value(new ConfigOptionString(""));

    def = this->add("template_custom_gcode", coString);
    def->label = L("Custom G-code");
    def->tooltip = L("This G-code will be used as a custom code");
    def->multiline = true;
    def->full_width = true;
    def->height = 12;
    def->mode = comAdvanced;
    def->set_default_value(new ConfigOptionString(""));

    def = this->add("has_scarf_joint_seam", coBool);
    def->mode = comAdvanced;
    def->set_default_value(new ConfigOptionBool(false));

    {
        struct AxisDefault {
            std::string         name;
            std::vector<double> max_feedrate;
            std::vector<double> max_acceleration;
            std::vector<double> max_jerk;
        };
        std::vector<AxisDefault> axes {
            // name, max_feedrate,  max_acceleration, max_jerk
            { "x", { 500., 200. }, {  1000., 1000. }, { 10. , 10.  } },
            { "y", { 500., 200. }, {  1000., 1000. }, { 10. , 10.  } },
            { "z", {  12.,  12. }, {   500.,  200. }, {  0.2,  0.4 } },
            { "e", { 120., 120. }, {  5000., 5000. }, {  2.5,  2.5 } }
        };
        for (const AxisDefault &axis : axes) {
            std::string axis_upper = boost::to_upper_copy<std::string>(axis.name);
            // Add the machine feedrate limits for XYZE axes. (M203)
            def = this->add("machine_max_speed_" + axis.name, coFloats);
            def->full_label = (boost::format("Maximum speed %1%") % axis_upper).str();
            (void)L("Maximum speed X");
            (void)L("Maximum speed Y");
            (void)L("Maximum speed Z");
            (void)L("Maximum speed E");
            def->category = L("Machine limits");
            def->readonly = false;
            def->tooltip  = (boost::format("Maximum speed of %1% axis") % axis_upper).str();
            (void)L("Maximum X speed");
            (void)L("Maximum Y speed");
            (void)L("Maximum Z speed");
            (void)L("Maximum E speed");
            def->sidetext = L("mm/s");
            def->min = 0;
            def->mode = comSimple;
            def->nullable = true;
            def->set_default_value(new ConfigOptionFloatsNullable(axis.max_feedrate));
            // Add the machine acceleration limits for XYZE axes (M201)
            def = this->add("machine_max_acceleration_" + axis.name, coFloats);
            def->full_label = (boost::format("Maximum acceleration %1%") % axis_upper).str();
            (void)L("Maximum acceleration X");
            (void)L("Maximum acceleration Y");
            (void)L("Maximum acceleration Z");
            (void)L("Maximum acceleration E");
            def->category = L("Machine limits");
            def->readonly = false;
            def->tooltip  = (boost::format("Maximum acceleration of the %1% axis") % axis_upper).str();
            (void)L("Maximum acceleration of the X axis");
            (void)L("Maximum acceleration of the Y axis");
            (void)L("Maximum acceleration of the Z axis");
            (void)L("Maximum acceleration of the E axis");
            def->sidetext = "mm/s²";
            def->min = 0;
            def->mode = comSimple;
            def->nullable = true;
            def->set_default_value(new ConfigOptionFloatsNullable(axis.max_acceleration));
            // Add the machine jerk limits for XYZE axes (M205)
            def = this->add("machine_max_jerk_" + axis.name, coFloats);
            def->full_label = (boost::format("Maximum jerk %1%") % axis_upper).str();
            (void)L("Maximum jerk X");
            (void)L("Maximum jerk Y");
            (void)L("Maximum jerk Z");
            (void)L("Maximum jerk E");
            def->category = L("Machine limits");
            def->readonly = false;
            def->tooltip  = (boost::format("Maximum jerk of the %1% axis") % axis_upper).str();
            (void)L("Maximum jerk of the X axis");
            (void)L("Maximum jerk of the Y axis");
            (void)L("Maximum jerk of the Z axis");
            (void)L("Maximum jerk of the E axis");
            def->sidetext = L("mm/s");
            def->min = 0;
            def->mode = comSimple;
            def->nullable = true;
            def->set_default_value(new ConfigOptionFloatsNullable(axis.max_jerk));
        }
    }

    // M205 S... [mm/sec]
    def = this->add("machine_min_extruding_rate", coFloats);
    def->full_label = L("Minimum speed for extruding");
    def->category = L("Machine limits");
    def->tooltip = L("Minimum speed for extruding (M205 S)");
    def->sidetext = L("mm/s");
    def->min = 0;
    def->mode = comDevelop;
    def->set_default_value(new ConfigOptionFloatsNullable{ 0., 0. });

    // M205 T... [mm/sec]
    def = this->add("machine_min_travel_rate", coFloats);
    def->full_label = L("Minimum travel speed");
    def->category = L("Machine limits");
    def->tooltip = L("Minimum travel speed (M205 T)");
    def->sidetext = L("mm/s");
    def->min = 0;
    def->mode = comDevelop;
    def->set_default_value(new ConfigOptionFloatsNullable{ 0., 0. });

    // M204 P... [mm/sec^2]
    def = this->add("machine_max_acceleration_extruding", coFloats);
    def->full_label = L("Maximum acceleration for extruding");
    def->category = L("Machine limits");
    def->tooltip = L("Maximum acceleration for extruding (M204 P)");
    //                 "Marlin (legacy) firmware flavor will use this also "
    //                 "as travel acceleration (M204 T).");
    def->sidetext = "mm/s²";
    def->min = 0;
    def->readonly = false;
    def->mode = comSimple;
    def->nullable = true;
    def->set_default_value(new ConfigOptionFloatsNullable{ 1500., 1250. });


    // M204 R... [mm/sec^2]
    def = this->add("machine_max_acceleration_retracting", coFloats);
    def->full_label = L("Maximum acceleration for retracting");
    def->category = L("Machine limits");
    def->tooltip = L("Maximum acceleration for retracting (M204 R)");
    def->sidetext = "mm/s²";
    def->min = 0;
    def->readonly = false;
    def->mode = comSimple;
    def->nullable = true;
    def->set_default_value(new ConfigOptionFloatsNullable{ 1500., 1250. });

    // M204 T... [mm/sec^2]
    def = this->add("machine_max_acceleration_travel", coFloats);
    def->full_label = L("Maximum acceleration for travel");
    def->category = L("Machine limits");
    def->tooltip = L("Maximum acceleration for travel (M204 T)");
    def->sidetext = "mm/s²";
    def->min = 0;
    def->readonly = true;
    def->mode = comDevelop;
    def->nullable = true;
    def->set_default_value(new ConfigOptionFloatsNullable{ 1500., 1250. });

    def = this->add("fan_max_speed", coInts);
    def->label = L("Fan speed");
    def->tooltip = L("Part cooling fan speed may be increased when auto cooling is enabled. "
                     "This is the maximum speed limitation of part cooling fan");
    def->sidetext = "%";
    def->min = 0;
    def->max = 100;
    def->mode = comSimple;
    def->set_default_value(new ConfigOptionInts { 100 });

    def = this->add("max_layer_height", coFloats);
    def->label = L("Max");
    def->tooltip = L("The largest printable layer height for extruder. Used to limit "
                     "the maximum layer height when enable adaptive layer height");
    def->sidetext = L("mm");
    def->min = 0;
    def->mode = comAdvanced;
    def->nullable = true;
    def->set_default_value(new ConfigOptionFloatsNullable { 0. });

#ifdef HAS_PRESSURE_EQUALIZER
    //def = this->add("max_volumetric_extrusion_rate_slope_positive", coFloat);
    //def->label = L("Max volumetric slope positive");
    //def->tooltip = L("This experimental setting is used to limit the speed of change in extrusion rate. "
    //               "A value of 1.8 mm³/s² ensures, that a change from the extrusion rate "
    //               "of 1.8 mm³/s (0.45mm extrusion width, 0.2mm extrusion height, feedrate 20 mm/s) "
    //               "to 5.4 mm³/s (feedrate 60 mm/s) will take at least 2 seconds.");
    //def->sidetext = L("mm³/s²");
    //def->min = 0;
    //def->mode = comAdvanced;
    //def->set_default_value(new ConfigOptionFloat(0));

    //def = this->add("max_volumetric_extrusion_rate_slope_negative", coFloat);
    //def->label = L("Max volumetric slope negative");
    //def->tooltip = L("This experimental setting is used to limit the speed of change in extrusion rate. "
    //               "A value of 1.8 mm³/s² ensures, that a change from the extrusion rate "
    //               "of 1.8 mm³/s (0.45mm extrusion width, 0.2mm extrusion height, feedrate 20 mm/s) "
    //               "to 5.4 mm³/s (feedrate 60 mm/s) will take at least 2 seconds.");
    //def->sidetext = L("mm³/s²");
    //def->min = 0;
    //def->mode = comAdvanced;
    //def->set_default_value(new ConfigOptionFloat(0));
#endif /* HAS_PRESSURE_EQUALIZER */

    def = this->add("fan_min_speed", coInts);
    def->label = L("Fan speed");
    def->tooltip = L("Minimum speed for part cooling fan");
    def->sidetext = "%";
    def->min = 0;
    def->max = 100;
    def->mode = comSimple;
    def->set_default_value(new ConfigOptionInts { 20 });

    def = this->add("additional_cooling_fan_speed", coInts);
    def->label = L("Fan speed");
    def->tooltip = L("Speed of auxiliary part cooling fan. Auxiliary fan will run at this speed during printing except the first several layers "
                     "which are defined by no cooling layers");
    def->sidetext = "%";
    def->min = 0;
    def->max = 100;
    def->mode = comSimple;
    def->set_default_value(new ConfigOptionInts { 0 });

    def = this->add("min_layer_height", coFloats);
    def->label = L("Min");
    def->tooltip = L("The lowest printable layer height for extruder. Used to limit "
                     "the minimum layer height when enable adaptive layer height");
    def->sidetext = L("mm");
    def->min = 0;
    def->mode = comAdvanced;
    def->nullable = true;
    def->set_default_value(new ConfigOptionFloatsNullable { 0.07 });

    def = this->add("slow_down_min_speed", coFloats);
    def->label = L("Min print speed");
    def->tooltip = L("The minimum printing speed when slow down for cooling");
    def->sidetext = L("mm/s");
    def->min = 0;
    def->mode = comAdvanced;
    def->set_default_value(new ConfigOptionFloats { 10. });

    def = this->add("nozzle_diameter", coFloats);
    def->label = L("Nozzle diameter");
    def->tooltip = L("Diameter of nozzle");
    def->sidetext = L("mm");
    def->mode = comAdvanced;
    def->max = 1.0;
    def->nullable = true;
    def->set_default_value(new ConfigOptionFloatsNullable { 0.4 });

    def = this->add("host_type", coEnum);
    def->label = L("Host Type");
    def->tooltip = L("Slic3r can upload G-code files to a printer host. This field must contain "
        "the kind of the host.");
    def->enum_keys_map = &ConfigOptionEnum<PrintHostType>::get_enum_values();
    def->enum_values.push_back("prusalink");
    def->enum_values.push_back("octoprint");
    def->enum_values.push_back("duet");
    def->enum_values.push_back("flashair");
    def->enum_values.push_back("astrobox");
    def->enum_values.push_back("repetier");
    def->enum_values.push_back("mks");
    def->enum_labels.push_back("PrusaLink");
    def->enum_labels.push_back("OctoPrint");
    def->enum_labels.push_back("Duet");
    def->enum_labels.push_back("FlashAir");
    def->enum_labels.push_back("AstroBox");
    def->enum_labels.push_back("Repetier");
    def->enum_labels.push_back("MKS");
    def->mode = comAdvanced;
    def->cli = ConfigOptionDef::nocli;
    def->set_default_value(new ConfigOptionEnum<PrintHostType>(htOctoPrint));

    def = this->add("nozzle_volume", coFloats);
    def->label = L("Nozzle volume");
    def->tooltip = L("Volume of nozzle between the cutter and the end of nozzle");
    def->sidetext = L("mm³");
    def->mode     = comAdvanced;
    def->readonly = true;
    def->nullable = true;
    def->set_default_value(new ConfigOptionFloatsNullable { {0.0} });

    def = this->add("start_end_points", coPoints);
    def->label = L("Start end points");
    def->tooltip  = L("The start and end points which are from cutter area to garbage can.");
    def->mode     = comDevelop;
    def->readonly = true;
    // start and end point is from the change_filament_gcode
    def->set_default_value(new ConfigOptionPoints{Vec2d(30, -3), Vec2d(54, 245)});

    def = this->add("reduce_infill_retraction", coBool);
    def->label = L("Reduce infill retraction");
    def->tooltip = L("Don't retract when the travel is in infill area absolutely. That means the oozing can't been seen. "
                     "This can reduce times of retraction for complex model and save printing time, but make slicing and "
                     "G-code generating slower");
    def->mode = comAdvanced;
    def->set_default_value(new ConfigOptionBool(false));

    def = this->add("ooze_prevention", coBool);
    def->label = L("Enable");
    //def->tooltip = L("This option will drop the temperature of the inactive extruders to prevent oozing. "
    //               "It will enable a tall skirt automatically and move extruders outside such "
    //               "skirt when changing temperatures.");
    def->mode = comDevelop;
    def->set_default_value(new ConfigOptionBool(false));

    def = this->add("filename_format", coString);
    def->label = L("Filename format");
    def->tooltip = L("User can self-define the project file name when export");
    def->full_width = true;
    def->mode = comDevelop;
    def->set_default_value(new ConfigOptionString("[input_filename_base].gcode"));

    def = this->add("detect_overhang_wall", coBool);
    def->label = L("Detect overhang wall");
    def->category = L("Quality");
    def->tooltip = L("Detect the overhang percentage relative to line width and use different speed to print. "
                     "For 100 percent overhang, bridge speed is used.");
    def->mode = comDevelop;
    def->set_default_value(new ConfigOptionBool(true));

    def = this->add("smooth_speed_discontinuity_area", coBool);
    def->label = L("Smooth speed discontinuity area");
    def->category = L("Quality");
    def->tooltip  = L("Add the speed transition between discontinuity area.");
    def->mode     = comAdvanced;
    def->set_default_value(new ConfigOptionBool(true));

    def           = this->add("smooth_coefficient", coFloat);
    def->label    = L("Smooth coefficient");
    def->category = L("Quality");
    def->tooltip  = L("The smaller the number, the longer the speed transition path. 0 means not apply.");
    def->mode     = comAdvanced;
    def->min      = 0;
    def->set_default_value(new ConfigOptionFloat(80));

    def = this->add("wall_filament", coInt);
    //def->label = L("Walls");
    //def->category = L("Extruders");
    //def->tooltip = L("Filament to print walls");
    def->label = "Walls";
    def->category = "Extruders";
    def->tooltip = "Filament to print walls";
    def->min = 1;
    def->mode = comDevelop;
    def->set_default_value(new ConfigOptionInt(1));

    def = this->add("inner_wall_line_width", coFloat);
    def->label = L("Inner wall");
    def->category = L("Quality");
    def->tooltip = L("Line width of inner wall");
    def->sidetext = L("mm");
    def->min = 0;
    def->mode = comAdvanced;
    def->set_default_value(new ConfigOptionFloat(0.4));

    def = this->add("inner_wall_speed", coFloats);
    def->label = L("Inner wall");
    def->category = L("Speed");
    def->tooltip = L("Speed of inner wall");
    def->sidetext = L("mm/s");
    def->aliases = { "perimeter_feed_rate" };
    def->min = 0;
    def->mode = comAdvanced;
    def->nullable = true;
    def->set_default_value(new ConfigOptionFloatsNullable{60});

    def = this->add("wall_loops", coInt);
    def->label = L("Wall loops");
    def->category = L("Strength");
    def->tooltip = L("Number of walls of every layer");
    def->min = 0;
    def->max = 1000;
    def->set_default_value(new ConfigOptionInt(2));

    def = this->add("post_process", coStrings);
    def->label = L("Post-processing Scripts");
    def->tooltip = L("If you want to process the output G-code through custom scripts, "
        "just list their absolute paths here. Separate multiple scripts with a semicolon. "
        "Scripts will be passed the absolute path to the G-code file as the first argument, "
        "and variables of settings also can be read");
    def->gui_flags = "serialized";
    def->multiline = true;
    def->full_width = true;
    def->height = 6;
    def->mode = comAdvanced;
    def->set_default_value(new ConfigOptionStrings());

    def = this->add("printer_model", coString);
    //def->label = L("Printer type");
    //def->tooltip = L("Type of the printer");
    def->label = "Printer type";
    def->tooltip = "Type of the printer";
    def->set_default_value(new ConfigOptionString());
    def->cli = ConfigOptionDef::nocli;

    def = this->add("printer_variant", coString);
    //def->label = L("Printer variant");
    def->label = "Printer variant";
    //def->tooltip = L("Name of the printer variant. For example, the printer variants may be differentiated by a nozzle diameter.");
    def->set_default_value(new ConfigOptionString());
    def->cli = ConfigOptionDef::nocli;

    def = this->add("print_settings_id", coString);
    def->set_default_value(new ConfigOptionString(""));
    //BBS: open this option to command line
    //def->cli = ConfigOptionDef::nocli;

    def = this->add("printer_settings_id", coString);
    def->set_default_value(new ConfigOptionString(""));
    //BBS: open this option to command line
    //def->cli = ConfigOptionDef::nocli;

    def = this->add("raft_contact_distance", coFloat);
    def->label = L("Raft contact Z distance");
    def->category = L("Support");
    def->tooltip = L("Z gap between object and raft. Ignored for soluble interface");
    def->sidetext = L("mm");
    def->min = 0;
    def->mode = comAdvanced;
    def->set_default_value(new ConfigOptionFloat(0.1));

    def = this->add("raft_expansion", coFloat);
    def->label = L("Raft expansion");
    def->category = L("Support");
    def->tooltip = L("Expand all raft layers in XY plane");
    def->sidetext = L("mm");
    def->min = 0;
    def->mode = comAdvanced;
    def->set_default_value(new ConfigOptionFloat(1.5));

    def = this->add("raft_first_layer_density", coPercent);
    def->label = L("Initial layer density");
    def->category = L("Support");
    def->tooltip = L("Density of the first raft or support layer");
    def->sidetext = "%";
    def->min = 10;
    def->max = 100;
    def->mode = comAdvanced;
    def->set_default_value(new ConfigOptionPercent(90));

    def = this->add("raft_first_layer_expansion", coFloat);
    def->label = L("Initial layer expansion");
    def->category = L("Support");
    def->tooltip = L("Expand the first raft or support layer to improve bed plate adhesion");
    def->sidetext = L("mm");
    def->min = 0;
    def->mode = comAdvanced;
    //BBS: change from 3.0 to 2.0
    def->set_default_value(new ConfigOptionFloat(2.0));

    def = this->add("raft_layers", coInt);
    def->label = L("Raft layers");
    def->category = L("Support");
    def->tooltip = L("Object will be raised by this number of support layers. "
                     "Use this function to avoid warping when print ABS");
    def->sidetext = L("layers");
    def->min = 0;
    def->max = 100;
    def->mode = comAdvanced;
    def->set_default_value(new ConfigOptionInt(0));

    def = this->add("resolution", coFloat);
    def->label = L("Resolution");
    def->tooltip = L("G-code path is generated after simplifying the contour of model to avoid too many points and gcode lines "
                     "in the gcode file. Smaller value means higher resolution and more time to slice");
    def->sidetext = L("mm");
    def->min = 0;
    def->mode = comAdvanced;
    def->set_default_value(new ConfigOptionFloat(0.01));

    def = this->add("retraction_minimum_travel", coFloats);
    def->label = L("Travel distance threshold");
    def->tooltip = L("Only trigger retraction when the travel distance is longer than this threshold");
    def->sidetext = L("mm");
    def->mode = comAdvanced;
    def->nullable = true;
    def->set_default_value(new ConfigOptionFloatsNullable { 2. });

    def = this->add("retract_before_wipe", coPercents);
    def->label = L("Retract amount before wipe");
    def->tooltip = L("The length of fast retraction before wipe, relative to retraction length");
    def->sidetext = "%";
    def->mode = comAdvanced;
    def->nullable = true;
    def->set_default_value(new ConfigOptionPercentsNullable { 100 });

    def = this->add("retract_when_changing_layer", coBools);
    def->label = L("Retract when change layer");
    def->tooltip = L("Force a retraction when changes layer");
    def->mode = comAdvanced;
    def->nullable = true;
    def->set_default_value(new ConfigOptionBoolsNullable { false });

    def = this->add("retraction_length", coFloats);
    def->label = L("Length");
    def->full_label = L("Retraction Length");
    def->tooltip = L("Some amount of material in extruder is pulled back to avoid ooze during long travel. "
                     "Set zero to disable retraction");
    def->sidetext = L("mm");
    def->mode = comSimple;
    def->nullable = true;
    def->set_default_value(new ConfigOptionFloatsNullable { 0.8 });

    def = this->add("enable_long_retraction_when_cut",coInt);
    def->mode = comDevelop;
    def->set_default_value(new ConfigOptionInt {0});

    def = this->add("long_retractions_when_cut", coBools);
    def->label = L("Long retraction when cut(experimental)");
    def->tooltip = L("Experimental feature.Retracting and cutting off the filament at a longer distance during changes to minimize purge."
                     "While this reduces flush significantly, it may also raise the risk of nozzle clogs or other printing problems.");
    def->mode = comDevelop;
    def->nullable = true;
    def->set_default_value(new ConfigOptionBoolsNullable {false});

    def = this->add("retraction_distances_when_cut",coFloats);
    def->label = L("Retraction distance when cut");
    def->tooltip = L("Experimental feature.Retraction length before cutting off during filament change");
    def->mode = comDevelop;
    def->min = 10;
    def->max = 18;
    def->nullable = true;
    def->set_default_value(new ConfigOptionFloatsNullable {18});

    def = this->add("long_retractions_when_ec", coBools);
    def->label = L("Long retraction when extruder change");
    def->mode = comAdvanced;
    def->nullable = true;
    def->set_default_value(new ConfigOptionBoolsNullable {false});

    def = this->add("retraction_distances_when_ec", coFloats);
    def->label = L("Retraction distance when extruder change");
    def->mode = comAdvanced;
    def->nullable = true;
    def->min = 0;
    def->max = 10;
    def->sidetext = L("mm");
    def->set_default_value(new ConfigOptionFloatsNullable{10});

    def = this->add("retract_length_toolchange", coFloats);
    def->label = L("Length");
    //def->full_label = L("Retraction Length (Toolchange)");
    def->full_label = "Retraction Length (Toolchange)";
    //def->tooltip = L("When retraction is triggered before changing tool, filament is pulled back "
    //               "by the specified amount (the length is measured on raw filament, before it enters "
    //               "the extruder).");
    def->sidetext = L("mm");
    def->mode = comDevelop;
    def->nullable = true;
    def->set_default_value(new ConfigOptionFloatsNullable { 10. });

    def = this->add("z_hop", coFloats);
    def->label = L("Z hop when retract");
    def->tooltip = L("Whenever the retraction is done, the nozzle is lifted a little to create clearance between nozzle and the print. "
                     "It prevents nozzle from hitting the print when travel moves. "
                     "Using spiral line to lift z can prevent stringing");
    def->sidetext = L("mm");
    def->mode = comSimple;
    def->min = 0;
    def->max = 5;
    def->nullable = true;
    def->set_default_value(new ConfigOptionFloatsNullable { 0.4 });

    def             = this->add("retract_lift_above", coFloats);
    def->label      = L("Z hop lower boundary");
    def->tooltip    = L("Z hop will only come into effect when Z is above this value and is below the parameter: \"Z hop upper boundary\"");
    def->sidetext   = L("mm");
    def->mode       = comAdvanced;
    def->min        = 0;
    def->nullable = true;
    def->set_default_value(new ConfigOptionFloatsNullable{0.});

    def             = this->add("retract_lift_below", coFloats);
    def->label      = L("Z hop upper boundary");
    def->tooltip    = L("If this value is positive, Z hop will only come into effect when Z is above the parameter: \"Z hop lower boundary\" and is below this value");
    def->sidetext   = L("mm");
    def->mode       = comAdvanced;
    def->min        = 0;
    def->nullable = true;
    def->set_default_value(new ConfigOptionFloatsNullable{0.});


    def = this->add("z_hop_types", coEnums);
    def->label = L("Z Hop Type");
    def->tooltip = L("");
    def->enum_keys_map = &ConfigOptionEnum<ZHopType>::get_enum_values();
    def->enum_values.push_back("Auto Lift");
    def->enum_values.push_back("Normal Lift");
    def->enum_values.push_back("Slope Lift");
    def->enum_values.push_back("Spiral Lift");
    def->enum_labels.push_back(L("Auto"));
    def->enum_labels.push_back(L("Normal"));
    def->enum_labels.push_back(L("Slope"));
    def->enum_labels.push_back(L("Spiral"));
    def->mode = comAdvanced;
    def->nullable = true;
    def->set_default_value(new ConfigOptionEnumsGenericNullable{ ZHopType::zhtSpiral });

    def = this->add("extruder_type", coEnums);
    def->label = L("Type");
    def->tooltip = ("This setting is only used for initial value of manual calibration of pressure advance. Bowden extruder usually has larger pa value. This setting doesn't influence normal slicing");
    def->enum_keys_map = &ConfigOptionEnum<ExtruderType>::get_enum_values();
    def->enum_values.push_back("Direct Drive");
    def->enum_values.push_back("Bowden");
    def->enum_labels.push_back(L("Direct Drive"));
    def->enum_labels.push_back(L("Bowden"));
    def->mode = comAdvanced;
    def->set_default_value(new ConfigOptionEnumsGeneric{ ExtruderType::etDirectDrive });

    //BBS
    def = this->add("nozzle_volume_type", coEnums);
    def->label = L("Nozzle Volume Type");
    def->tooltip = ("Nozzle volume type");
    def->enum_keys_map = &ConfigOptionEnum<NozzleVolumeType>::get_enum_values();
    def->enum_values.push_back(L("Standard"));
    def->enum_values.push_back(L("High Flow"));
    def->enum_labels.push_back(L("Standard"));
    def->enum_labels.push_back(L("High Flow"));
    def->mode = comSimple;
    def->set_default_value(new ConfigOptionEnumsGeneric{ NozzleVolumeType::nvtStandard });

    def = this->add("default_nozzle_volume_type", coEnums);
    def->label = L("Default Nozzle Volume Type");
    def->tooltip = ("Default Nozzle volume type for extruders in this printer");
    def->enum_keys_map = &ConfigOptionEnum<NozzleVolumeType>::get_enum_values();
    def->enum_values.push_back(L("Standard"));
    def->enum_values.push_back(L("High Flow"));
    def->enum_labels.push_back(L("Standard"));
    def->enum_labels.push_back(L("High Flow"));
    def->mode = comDevelop;
    def->set_default_value(new ConfigOptionEnumsGeneric{ NozzleVolumeType::nvtStandard });

    def = this->add("extruder_variant_list", coStrings);
    def->label = "Extruder variant list";
    def->tooltip = "Extruder variant list";
    def->set_default_value(new ConfigOptionStrings { "Direct Drive Standard" });
    def->cli = ConfigOptionDef::nocli;

    def = this->add("extruder_ams_count", coStrings);
    def->label = "Extruder ams count";
    def->tooltip = "Ams counts of per extruder";
    def->set_default_value(new ConfigOptionStrings { });

    def = this->add("printer_extruder_id", coInts);
    def->label = "Printer extruder id";
    def->tooltip = "Printer extruder id";
    def->set_default_value(new ConfigOptionInts { 1 });
    def->cli = ConfigOptionDef::nocli;

    def = this->add("printer_extruder_variant", coStrings);
    def->label = "Printer's extruder variant";
    def->tooltip = "Printer's extruder variant";
    def->set_default_value(new ConfigOptionStrings { "Direct Drive Standard" });
    def->cli = ConfigOptionDef::nocli;

    def = this->add("master_extruder_id", coInt);
    def->label = "Master extruder id";
    def->tooltip = "Default extruder id to place filament";
    def->set_default_value(new ConfigOptionInt{ 1 });

    def = this->add("print_extruder_id", coInts);
    def->label = "Print extruder id";
    def->tooltip = "Print extruder id";
    def->set_default_value(new ConfigOptionInts { 1 });
    def->cli = ConfigOptionDef::nocli;

    def = this->add("print_extruder_variant", coStrings);
    def->label = "Print's extruder variant";
    def->tooltip = "Print's extruder variant";
    def->set_default_value(new ConfigOptionStrings { "Direct Drive Standard" });
    def->cli = ConfigOptionDef::nocli;

    /*def = this->add("filament_extruder_id", coInts);
    def->label = "Filament extruder id";
    def->tooltip = "Filament extruder id";
    def->set_default_value(new ConfigOptionInts { 1 });
    def->cli = ConfigOptionDef::nocli;*/

    def = this->add("filament_extruder_variant", coStrings);
    def->label = "Filament's extruder variant";
    def->tooltip = "Filament's extruder variant";
    def->set_default_value(new ConfigOptionStrings { "Direct Drive Standard" });
    def->cli = ConfigOptionDef::nocli;

    def = this->add("filament_self_index", coInts);
    def->label = "Filament self index";
    def->tooltip = "Filament self index";
    def->set_default_value(new ConfigOptionInts { 1 });
    def->cli = ConfigOptionDef::nocli;

    def = this->add("retract_restart_extra", coFloats);
    def->label = L("Extra length on restart");
    //def->label = "Extra length on restart";
    //def->tooltip = L("When the retraction is compensated after the travel move, the extruder will push "
    //               "this additional amount of filament. This setting is rarely needed.");
    def->sidetext = L("mm");
    def->mode = comDevelop;
    def->set_default_value(new ConfigOptionFloatsNullable { 0. });

    def = this->add("retract_restart_extra_toolchange", coFloats);
    def->label = L("Extra length on restart");
    //def->label = "Extra length on restart";
    //def->tooltip = L("When the retraction is compensated after changing tool, the extruder will push "
    //               "this additional amount of filament.");
    def->sidetext = L("mm");
    def->mode = comDevelop;
    def->nullable = true;
    def->set_default_value(new ConfigOptionFloatsNullable { 0. });

    def = this->add("retraction_speed", coFloats);
    def->label = L("Retraction Speed");
    def->full_label = L("Retraction Speed");
    def->tooltip = L("Speed of retractions");
    def->sidetext = L("mm/s");
    def->mode = comAdvanced;
    def->nullable = true;
    def->set_default_value(new ConfigOptionFloatsNullable { 30. });

    def = this->add("deretraction_speed", coFloats);
    def->label = L("Deretraction Speed");
    def->full_label = L("Deretraction Speed");
    def->tooltip = L("Speed for reloading filament into extruder. Zero means the same speed as retraction");
    def->sidetext = L("mm/s");
    def->mode = comAdvanced;
    def->nullable = true;
    def->set_default_value(new ConfigOptionFloatsNullable { 0. });

    def = this->add("seam_position", coEnum);
    def->label = L("Seam position");
    def->category = L("Quality");
    def->tooltip = L("The start position to print each part of outer wall");
    def->enum_keys_map = &ConfigOptionEnum<SeamPosition>::get_enum_values();
    def->enum_values.push_back("nearest");
    def->enum_values.push_back("aligned");
    def->enum_values.push_back("back");
    def->enum_values.push_back("random");
    def->enum_labels.push_back(L("Nearest"));
    def->enum_labels.push_back(L("Aligned"));
    def->enum_labels.push_back(L("Back"));
    def->enum_labels.push_back(L("Random"));
    def->mode = comSimple;
    def->set_default_value(new ConfigOptionEnum<SeamPosition>(spAligned));

    def = this->add("seam_gap", coPercent);
    def->label = L("Seam gap");
    def->tooltip = L("In order to reduce the visibility of the seam in a closed loop extrusion, the loop is interrupted and shortened by a specified amount.\n" "This amount as a percentage of the current extruder diameter. The default value for this parameter is 15");
    def->sidetext = "%";
    def->min = 0;
    def->mode = comDevelop;
    def->set_default_value(new ConfigOptionPercent(15));

    def          = this->add("seam_slope_conditional", coBool);
    def->label   = L("Smart scarf seam application");
    def->tooltip = L("Apply scarf joints only to smooth perimeters where traditional seams do not conceal the seams at sharp corners effectively.");
    def->mode    = comAdvanced;
    def->set_default_value(new ConfigOptionBool(true));

    def           = this->add("scarf_angle_threshold", coInt);
    def->label    = L("Scarf application angle threshold");
    def->tooltip  = L("This option sets the threshold angle for applying a conditional scarf joint seam.\nIf the seam angle within the perimeter loop " "exceeds this value (indicating the absence of sharp corners), a scarf joint seam will be used. The default value is 155°.");
    def->mode     = comAdvanced;
    def->sidetext = L("°");
    def->min      = 0;
    def->max      = 180;
    def->set_default_value(new ConfigOptionInt(155));

    def          = this->add("seam_slope_entire_loop", coBool);
    def->label   = L("Scarf around entire wall");
    def->tooltip = L("The scarf extends to the entire length of the wall.");
    def->mode    = comAdvanced;
    def->set_default_value(new ConfigOptionBool(false));

    def          = this->add("seam_slope_steps", coInt);
    def->label   = L("Scarf steps");
    def->tooltip = L("Minimum number of segments of each scarf.");
    def->min     = 1;
    def->mode    = comAdvanced;
    def->set_default_value(new ConfigOptionInt(10));

    def          = this->add("seam_slope_inner_walls", coBool);
    def->label   = L("Scarf joint for inner walls");
    def->tooltip = L("Use scarf joint for inner walls as well.");
    def->mode    = comAdvanced;
    def->set_default_value(new ConfigOptionBool(true));

    def = this->add("wipe_speed", coPercent);
    def->label = L("Wipe speed");
    def->tooltip = L("The wipe speed is determined by the speed setting specified in this configuration." "If the value is expressed as a percentage (e.g. 80%), it will be calculated based on the travel speed setting above." "The default value for this parameter is 80%");
    def->sidetext = "%";
    def->min = 0.01;
    def->mode = comDevelop;
    def->set_default_value(new ConfigOptionPercent(80));

    def          = this->add("role_base_wipe_speed", coBool);
    def->label   = L("Role-based wipe speed");
    def->tooltip = L("The wipe speed is determined by speed of current extrusion role. " "e.g if a wipe action is executed immediately following an outer wall extrusion, the speed of the outer wall extrusion will be utilized for the wipe action.");
    def->mode    = comAdvanced;
    def->set_default_value(new ConfigOptionBool(true));

    def = this->add("skirt_distance", coFloat);
    def->label = L("Skirt distance");
    def->tooltip = L("Distance from skirt to brim or object");
    def->sidetext = L("mm");
    def->min = 0;
    def->max = 55;
    def->mode = comDevelop;
    def->set_default_value(new ConfigOptionFloat(2));

    def = this->add("skirt_height", coInt);
    def->label = L("Skirt height");
    //def->label = "Skirt height";
    def->tooltip = L("How many layers of skirt. Usually only one layer");
    def->sidetext = L("layers");
    def->mode = comSimple;
    def->max = 10000;
    def->set_default_value(new ConfigOptionInt(1));

    def = this->add("draft_shield", coEnum);
    //def->label = L("Draft shield");
    def->label = "Draft shield";
    //def->tooltip = L("With draft shield active, the skirt will be printed skirt_distance from the object, possibly intersecting brim.\n"
    //                 "Enabled = skirt is as tall as the highest printed object.\n"
    //                "Limited = skirt is as tall as specified by skirt_height.\n"
    //				 "This is useful to protect an ABS or ASA print from warping and detaching from print bed due to wind draft.");
    def->enum_keys_map = &ConfigOptionEnum<DraftShield>::get_enum_values();
    def->enum_values.push_back("disabled");
    def->enum_values.push_back("limited");
    def->enum_values.push_back("enabled");
    def->enum_labels.push_back("Disabled");
    def->enum_labels.push_back("Limited");
    def->enum_labels.push_back("Enabled");
    def->mode = comDevelop;
    def->set_default_value(new ConfigOptionEnum<DraftShield>(dsDisabled));

    def = this->add("skirt_loops", coInt);
    def->label = L("Skirt loops");
    def->full_label = L("Skirt loops");
    def->tooltip = L("Number of loops for the skirt. Zero means disabling skirt");
    def->min = 0;
    def->max = 10;
    def->mode = comSimple;
    def->set_default_value(new ConfigOptionInt(1));

    def = this->add("slow_down_layer_time", coInts);
    def->label = L("Layer time");
    def->tooltip = L("The printing speed in exported gcode will be slowed down, when the estimated layer time is shorter than this value, to "
                     "get better cooling for these layers");
    def->sidetext = L("s");
    def->min = 0;
    def->max = 1000;
    def->mode = comSimple;
    def->set_default_value(new ConfigOptionInts { 5 });

    def = this->add("minimum_sparse_infill_area", coFloat);
    def->label = L("Minimum sparse infill threshold");
    def->category = L("Strength");
    def->tooltip = L("Sparse infill area which is smaller than threshold value is replaced by internal solid infill");
    def->sidetext = L("mm²");
    def->min = 0;
    def->mode = comAdvanced;
    def->set_default_value(new ConfigOptionFloat(15));

    def = this->add("solid_infill_filament", coInt);
    //def->label = L("Solid infill");
    //def->category = L("Extruders");
    //def->tooltip = L("Filament to print solid infill");
    def->label = "Solid infill";
    def->category = "Extruders";
    def->tooltip = "Filament to print solid infill";
    def->min = 1;
    def->mode = comDevelop;
    def->set_default_value(new ConfigOptionInt(1));

    def = this->add("internal_solid_infill_line_width", coFloat);
    def->label = L("Internal solid infill");
    def->category = L("Quality");
    def->tooltip = L("Line width of internal solid infill");
    def->sidetext = L("mm");
    def->min = 0;
    def->mode = comAdvanced;
    def->set_default_value(new ConfigOptionFloat(0.4));

    def = this->add("internal_solid_infill_speed", coFloats);
    def->label = L("Internal solid infill");
    def->category = L("Speed");
    def->tooltip = L("Speed of internal solid infill, not the top and bottom surface");
    def->sidetext = L("mm/s");
    def->min = 0;
    def->mode = comAdvanced;
    def->nullable = true;
    def->set_default_value(new ConfigOptionFloatsNullable{100});

    def = this->add("spiral_mode", coBool);
    def->label = L("Spiral vase");
    def->tooltip = L("Spiralize smooths out the z moves of the outer contour. "
                     "And turns a solid model into a single walled print with solid bottom layers. "
                     "The final generated model has no seam");
    def->mode = comSimple;
    def->set_default_value(new ConfigOptionBool(false));

    def = this->add("spiral_mode_smooth", coBool);
    def->label = L("Smooth Spiral");
    def->tooltip = L("Smooth Spiral smoothes out X and Y moves as well"
                     "resulting in no visible seam at all, even in the XY directions on walls that are not vertical");
    def->mode = comSimple;
    def->set_default_value(new ConfigOptionBool(false));

    def = this->add("spiral_mode_max_xy_smoothing", coFloatOrPercent);
    def->label = L("Max XY Smoothing");
    def->tooltip = L("Maximum distance to move points in XY to try to achieve a smooth spiral"
                     "If expressed as a %, it will be computed over nozzle diameter");
    def->sidetext = L("mm or %");
    def->ratio_over = "nozzle_diameter";
    def->min = 0;
    def->max = 1000;
    def->max_literal = 10;
    def->mode = comAdvanced;
    def->set_default_value(new ConfigOptionFloatOrPercent(200, true));

    def = this->add("timelapse_type", coEnum);
    def->label = L("Timelapse");
    def->tooltip = L("If smooth or traditional mode is selected, a timelapse video will be generated for each print. "
                     "After each layer is printed, a snapshot is taken with the chamber camera. "
                     "All of these snapshots are composed into a timelapse video when printing completes. "
                     "If smooth mode is selected, the toolhead will move to the excess chute after each layer is printed "
                     "and then take a snapshot. "
                     "Since the melt filament may leak from the nozzle during the process of taking a snapshot, "
                     "prime tower is required for smooth mode to wipe nozzle.");
    def->enum_keys_map = &ConfigOptionEnum<TimelapseType>::get_enum_values();
    def->enum_values.emplace_back("0");
    def->enum_values.emplace_back("1");
    def->enum_labels.emplace_back(L("Traditional"));
    def->enum_labels.emplace_back(L("Smooth"));
    def->mode = comSimple;
    def->set_default_value(new ConfigOptionEnum<TimelapseType>(tlTraditional));

    def = this->add("standby_temperature_delta", coInt);
    def->label = L("Temperature variation");
    //def->tooltip = L("Temperature difference to be applied when an extruder is not active. "
    //               "Enables a full-height \"sacrificial\" skirt on which the nozzles are periodically wiped.");
    def->sidetext = "∆°C";
    def->min = -max_temp;
    def->max = max_temp;
    //BBS
    def->mode = comDevelop;
    def->set_default_value(new ConfigOptionInt(-5));

    def = this->add("machine_start_gcode", coString);
    def->label = L("Start G-code");
    def->tooltip = L("Start G-code when start the whole printing");
    def->multiline = true;
    def->full_width = true;
    def->height = 12;
    def->mode = comAdvanced;
    def->set_default_value(new ConfigOptionString("G28 ; home all axes\nG1 Z5 F5000 ; lift nozzle\n"));

    def = this->add("filament_start_gcode", coStrings);
    def->label = L("Start G-code");
    def->tooltip = L("Start G-code when start the printing of this filament");
    def->multiline = true;
    def->full_width = true;
    def->height = 12;
    def->mode = comAdvanced;
    def->set_default_value(new ConfigOptionStrings { " " });

    def = this->add("single_extruder_multi_material", coBool);
    //def->label = L("Single Extruder Multi Material");
    //def->tooltip = L("Use single nozzle to print multi filament");
    def->mode = comDevelop;
    def->set_default_value(new ConfigOptionBool(false));

    def = this->add("wipe_tower_no_sparse_layers", coBool);
    //def->label = L("No sparse layers (EXPERIMENTAL)");
    //def->tooltip = L("If enabled, the wipe tower will not be printed on layers with no toolchanges. "
    //                 "On layers with a toolchange, extruder will travel downward to print the wipe tower. "
    //                 "User is responsible for ensuring there is no collision with the print.");
    def->mode = comDevelop;
    def->set_default_value(new ConfigOptionBool(false));

    def = this->add("slice_closing_radius", coFloat);
    def->label = L("Slice gap closing radius");
    def->category = L("Quality");
    def->tooltip = L("Cracks smaller than 2x gap closing radius are being filled during the triangle mesh slicing. "
        "The gap closing operation may reduce the final print resolution, therefore it is advisable to keep the value reasonably low.");
    def->sidetext = L("mm");
    def->min = 0;
    def->mode = comAdvanced;
    def->set_default_value(new ConfigOptionFloat(0.049));

    def = this->add("slicing_mode", coEnum);
    def->label = L("Slicing Mode");
    def->category = L("Other");
    def->tooltip = L("Use \"Even-odd\" for 3DLabPrint airplane models. Use \"Close holes\" to close all holes in the model.");
    def->enum_keys_map = &ConfigOptionEnum<SlicingMode>::get_enum_values();
    def->enum_values.push_back("regular");
    def->enum_values.push_back("even_odd");
    def->enum_values.push_back("close_holes");
    def->enum_labels.push_back(L("Regular"));
    def->enum_labels.push_back(L("Even-odd"));
    def->enum_labels.push_back(L("Close holes"));
    def->mode = comAdvanced;
    def->set_default_value(new ConfigOptionEnum<SlicingMode>(SlicingMode::Regular));

    def = this->add("enable_support", coBool);
    //BBS: remove material behind support
    def->label = L("Enable support");
    def->category = L("Support");
    def->tooltip = L("Enable support generation.");
    def->set_default_value(new ConfigOptionBool(false));

    def = this->add("support_type", coEnum);
    def->label = L("Type");
    def->category = L("Support");
    def->tooltip = L("normal(auto) and tree(auto) is used to generate support automatically. "
                     "If normal(manual) or tree(manual) is selected, only support enforcers are generated");
    def->enum_keys_map = &ConfigOptionEnum<SupportType>::get_enum_values();
    def->enum_values.push_back("normal(auto)");
    def->enum_values.push_back("tree(auto)");
    def->enum_values.push_back("normal(manual)");
    def->enum_values.push_back("tree(manual)");
    def->enum_labels.push_back(L("normal(auto)"));
    def->enum_labels.push_back(L("tree(auto)"));
    def->enum_labels.push_back(L("normal(manual)"));
    def->enum_labels.push_back(L("tree(manual)"));
    def->mode = comSimple;
    def->set_default_value(new ConfigOptionEnum<SupportType>(stNormalAuto));

    def = this->add("support_object_xy_distance", coFloat);
    def->label = L("Support/object xy distance");
    def->category = L("Support");
    def->tooltip = L("XY separation between an object and its support");
    def->sidetext = L("mm");
    def->min = 0;
    def->max = 10;
    def->mode = comAdvanced;
    //Support with too small spacing may touch the object and difficult to remove.
    def->set_default_value(new ConfigOptionFloat(0.35));

    def = this->add("support_object_first_layer_gap", coFloat);
    def->label = L("Support/object first layer gap");
    def->category = L("Support");
    def->tooltip = L("XY separation between an object and its support at the first layer.");
    def->sidetext = L("mm");
    def->min = 0;
    def->max = 10;
    def->mode = comAdvanced;
    //Support with too small spacing may touch the object and difficult to remove.
    def->set_default_value(new ConfigOptionFloat(0.2));

    def = this->add("support_angle", coFloat);
    def->label = L("Pattern angle");
    def->category = L("Support");
    def->tooltip = L("Use this setting to rotate the support pattern on the horizontal plane.");
    def->sidetext = L("°");
    def->min = 0;
    def->max = 359;
    def->mode = comAdvanced;
    def->set_default_value(new ConfigOptionFloat(0));

    def = this->add("support_on_build_plate_only", coBool);
    def->label = L("On build plate only");
    def->category = L("Support");
    def->tooltip = L("Don't create support on model surface, only on build plate");
    def->mode = comSimple;
    def->set_default_value(new ConfigOptionBool(false));

    // BBS
    def           = this->add("support_critical_regions_only", coBool);
    def->label    = L("Support critical regions only");
    def->category = L("Support");
    def->tooltip  = L("Only create support for critical regions including sharp tail, cantilever, etc.");
    def->mode     = comAdvanced;
    def->set_default_value(new ConfigOptionBool(false));

    def = this->add("support_remove_small_overhang", coBool);
    def->label = L("Remove small overhangs");
    def->category = L("Support");
    def->tooltip = L("Remove small overhangs that possibly need no supports.");
    def->mode = comAdvanced;
    def->set_default_value(new ConfigOptionBool(true));

    // BBS: change type to common float.
    // It may be rounded to mulitple layer height when independent_support_layer_height is false.
    def = this->add("support_top_z_distance", coFloat);
    //def->gui_type = ConfigOptionDef::GUIType::f_enum_open;
    def->label = L("Top Z distance");
    def->category = L("Support");
    def->tooltip = L("The z gap between the top support interface and object");
    def->sidetext = L("mm");
//    def->min = 0;
#if 0
    //def->enum_values.push_back("0");
    //def->enum_values.push_back("0.1");
    //def->enum_values.push_back("0.2");
    //def->enum_labels.push_back(L("0 (soluble)"));
    //def->enum_labels.push_back(L("0.1 (semi-detachable)"));
    //def->enum_labels.push_back(L("0.2 (detachable)"));
#endif
    def->mode = comAdvanced;
    def->set_default_value(new ConfigOptionFloat(0.2));

    // BBS:MusangKing
    def = this->add("support_bottom_z_distance", coFloat);
    def->label = L("Bottom Z distance");
    def->category = L("Support");
    def->tooltip = L("The z gap between the bottom support interface and object");
    def->sidetext = L("mm");
    def->mode = comAdvanced;
    def->set_default_value(new ConfigOptionFloat(0.2));

    def = this->add("enforce_support_layers", coInt);
    //def->label = L("Enforce support for the first");
    def->category = L("Support");
    //def->tooltip = L("Generate support material for the specified number of layers counting from bottom, "
    //               "regardless of whether normal support material is enabled or not and regardless "
    //               "of any angle threshold. This is useful for getting more adhesion of objects "
    //               "having a very thin or poor footprint on the build plate.");
    def->sidetext = L("layers");
    //def->full_label = L("Enforce support for the first n layers");
    def->min = 0;
    def->max = 5000;
    def->mode = comDevelop;
    def->set_default_value(new ConfigOptionInt(0));

    def = this->add("support_filament", coInt);
    def->gui_type = ConfigOptionDef::GUIType::i_enum_open;
    def->label    = L("Support/raft base");
    def->category = L("Support");
    def->tooltip = L("Filament to print support base and raft. \"Default\" means no specific filament for support and current filament is used");
    def->min = 0;
    def->mode = comSimple;
    def->set_default_value(new ConfigOptionInt(0));

    def = this->add("support_interface_not_for_body",coBool);
    def->label    = L("Avoid interface filament for base");
    def->category = L("Support");
    def->tooltip = L("Avoid using support interface filament to print support base if possible.");
    def->mode = comSimple;
    def->set_default_value(new ConfigOptionBool(true));

    def = this->add("support_line_width", coFloat);
    def->label = L("Support");
    def->category = L("Quality");
    def->tooltip = L("Line width of support");
    def->sidetext = L("mm");
    def->min = 0;
    def->mode = comAdvanced;
    def->set_default_value(new ConfigOptionFloat(0.4));

    def = this->add("support_interface_loop_pattern", coBool);
    def->label = L("Interface use loop pattern");
    def->category = L("Support");
    def->tooltip = L("Cover the top contact layer of the supports with loops. Disabled by default.");
    def->mode = comAdvanced;
    def->set_default_value(new ConfigOptionBool(false));

    def = this->add("support_interface_filament", coInt);
    def->gui_type = ConfigOptionDef::GUIType::i_enum_open;
    def->label    = L("Support/raft interface");
    def->category = L("Support");
    def->tooltip = L("Filament to print support interface. \"Default\" means no specific filament for support interface and current filament is used");
    def->min = 0;
    // BBS
    def->mode = comSimple;
    def->set_default_value(new ConfigOptionInt(0));

    auto support_interface_top_layers = def = this->add("support_interface_top_layers", coInt);
    def->gui_type = ConfigOptionDef::GUIType::i_enum_open;
    def->label = L("Top interface layers");
    def->category = L("Support");
    def->tooltip = L("Number of top interface layers");
    def->sidetext = L("layers");
    def->min = 0;
    def->enum_values.push_back("0");
    def->enum_values.push_back("1");
    def->enum_values.push_back("2");
    def->enum_values.push_back("3");
    def->enum_labels.push_back("0");
    def->enum_labels.push_back("1");
    def->enum_labels.push_back("2");
    def->enum_labels.push_back("3");
    def->mode = comAdvanced;
    def->set_default_value(new ConfigOptionInt(3));

    def = this->add("support_interface_bottom_layers", coInt);
    def->gui_type = ConfigOptionDef::GUIType::i_enum_open;
    def->label = L("Bottom interface layers");
    def->category = L("Support");
    def->tooltip = L("Number of bottom interface layers");
    def->sidetext = L("layers");
    def->min = -1;
    def->enum_values.push_back("-1");
    append(def->enum_values, support_interface_top_layers->enum_values);
    def->enum_labels.push_back(L("Same as top"));
    append(def->enum_labels, support_interface_top_layers->enum_labels);
    def->mode = comAdvanced;
    def->set_default_value(new ConfigOptionInt(0));

    def = this->add("support_interface_spacing", coFloat);
    def->label = L("Top interface spacing");
    def->category = L("Support");
    def->tooltip = L("Spacing of interface lines. Zero means solid interface");
    def->sidetext = L("mm");
    def->min = 0;
    def->mode = comAdvanced;
    def->set_default_value(new ConfigOptionFloat(0.5));

    //BBS
    def = this->add("support_bottom_interface_spacing", coFloat);
    def->label = L("Bottom interface spacing");
    def->category = L("Support");
    def->tooltip = L("Spacing of bottom interface lines. Zero means solid interface");
    def->sidetext = L("mm");
    def->min = 0;
    def->mode = comDevelop;
    def->set_default_value(new ConfigOptionFloat(0.5));

    def = this->add("support_interface_speed", coFloats);
    def->label = L("Support interface");
    def->category = L("Speed");
    def->tooltip = L("Speed of support interface");
    def->sidetext = L("mm/s");
    def->min = 1;
    def->mode = comAdvanced;
    def->nullable = true;
    def->set_default_value(new ConfigOptionFloatsNullable{80});

    def = this->add("support_base_pattern", coEnum);
    def->label = L("Base pattern");
    def->category = L("Support");
    def->tooltip = L("Line pattern of support");
    def->enum_keys_map = &ConfigOptionEnum<SupportMaterialPattern>::get_enum_values();
    def->enum_values.push_back("default");
    def->enum_values.push_back("rectilinear");
    def->enum_values.push_back("rectilinear-grid");
    def->enum_values.push_back("honeycomb");
    def->enum_values.push_back("lightning");
    def->enum_values.push_back("hollow");
    def->enum_labels.push_back(L("Default"));
    def->enum_labels.push_back(L("Rectilinear"));
    def->enum_labels.push_back(L("Rectilinear grid"));
    def->enum_labels.push_back(L("Honeycomb"));
    def->enum_labels.push_back(L("Lightning"));
    def->enum_labels.push_back(L("Hollow"));
    def->mode = comAdvanced;
    def->set_default_value(new ConfigOptionEnum<SupportMaterialPattern>(smpDefault));

    def = this->add("support_interface_pattern", coEnum);
    def->label = L("Interface pattern");
    def->category = L("Support");
    def->tooltip = L("Line pattern of support interface. "
                     "Default pattern for support interface is Rectilinear Interlaced");
    def->enum_keys_map = &ConfigOptionEnum<SupportMaterialInterfacePattern>::get_enum_values();
    def->enum_values.push_back("auto");
    def->enum_values.push_back("rectilinear");
    def->enum_values.push_back("concentric");
    def->enum_values.push_back("rectilinear_interlaced");
    def->enum_values.push_back("grid");
    def->enum_labels.push_back(L("Default"));
    def->enum_labels.push_back(L("Rectilinear"));
    def->enum_labels.push_back(L("Concentric"));
    def->enum_labels.push_back(L("Rectilinear Interlaced"));
    def->enum_labels.push_back(L("Grid"));
    def->mode = comAdvanced;
    def->set_default_value(new ConfigOptionEnum<SupportMaterialInterfacePattern>(smipAuto));

    def = this->add("support_base_pattern_spacing", coFloat);
    def->label = L("Base pattern spacing");
    def->category = L("Support");
    def->tooltip = L("Spacing between support lines");
    def->sidetext = L("mm");
    def->min = 0;
    def->mode = comAdvanced;
    def->set_default_value(new ConfigOptionFloat(2.5));

    def = this->add("support_expansion", coFloat);
    def->label = L("Normal Support expansion");
    def->category = L("Support");
    def->tooltip = L("Expand (+) or shrink (-) the horizontal span of normal support");
    def->sidetext = L("mm");
    def->mode = comAdvanced;
    def->set_default_value(new ConfigOptionFloat(0));

    def = this->add("support_speed", coFloats);
    def->label = L("Support");
    def->category = L("Speed");
    def->tooltip = L("Speed of support");
    def->sidetext = L("mm/s");
    def->min = 0;
    def->mode = comAdvanced;
    def->nullable = true;
    def->set_default_value(new ConfigOptionFloatsNullable{ 80 });

    def = this->add("support_style", coEnum);
    def->label = L("Style");
    def->category = L("Support");
    def->tooltip  = L("Style and shape of the support. For normal support, projecting the supports into a regular grid "
                     "will create more stable supports (default), while snug support towers will save material and reduce "
                     "object scarring.\n"
                     "For tree support, slim style will merge branches more aggressively and save "
                     "a lot of material, strong style will make larger and stronger support structure and use more materials, "
                     "while hybrid style is the combination of slim tree and normal support with normal nodes "
                     "under large flat overhangs. Organic style will produce more organic shaped tree structure and less interfaces which makes it easer to be removed. "
                     "The default style is organic tree for most cases, and hybrid tree if adaptive layer height or soluble interface is enabled.");
    def->enum_keys_map = &ConfigOptionEnum<SupportMaterialStyle>::get_enum_values();
    def->enum_values.push_back("default");
    def->enum_values.push_back("grid");
    def->enum_values.push_back("snug");
    def->enum_values.push_back("tree_slim");
    def->enum_values.push_back("tree_strong");
    def->enum_values.push_back("tree_hybrid");
    def->enum_values.push_back("tree_organic");
    def->enum_labels.push_back(L("Default"));
    def->enum_labels.push_back(L("Grid"));
    def->enum_labels.push_back(L("Snug"));
    def->enum_labels.push_back(L("Tree Slim"));
    def->enum_labels.push_back(L("Tree Strong"));
    def->enum_labels.push_back(L("Tree Hybrid"));
    def->enum_labels.push_back(L("Tree Organic"));
    def->mode = comAdvanced;
    def->set_default_value(new ConfigOptionEnum<SupportMaterialStyle>(smsDefault));

    def = this->add("independent_support_layer_height", coBool);
    def->label = L("Independent support layer height");
    def->category = L("Support");
    def->tooltip = L("Support layer uses layer height independent with object layer. This is to support customizing z-gap and save print time."
                     "This option will be invalid when the prime tower is enabled.");
    def->mode = comAdvanced;
    def->set_default_value(new ConfigOptionBool(true));

    def = this->add("support_threshold_angle", coInt);
    def->label = L("Threshold angle");
    def->category = L("Support");
    def->tooltip = L("Support will be generated for overhangs whose slope angle is below the threshold.");
    def->sidetext = L("°");
    def->min = 1;
    def->max = 90;
    def->mode = comSimple;
    def->set_default_value(new ConfigOptionInt(30));

    def = this->add("tree_support_branch_angle", coFloat);
    def->label = L("Branch angle");
    def->category = L("Support");
    def->tooltip = L("This setting determines the maximum overhang angle that t he branches of tree support allowed to make."
                     "If the angle is increased, the branches can be printed more horizontally, allowing them to reach farther.");
    def->sidetext = L("°");
    def->min = 0;
    def->max = 60;
    def->mode = comAdvanced;
    def->set_default_value(new ConfigOptionFloat(40.));

    def           = this->add("tree_support_branch_distance", coFloat);
    def->label    = L("Branch distance");
    def->category = L("Support");
    def->tooltip  = L("This setting determines the distance between neighboring tree support nodes.");
    def->sidetext = L("mm");
    def->min      = 1.0;
    def->max      = 10;
    def->mode     = comAdvanced;
    def->set_default_value(new ConfigOptionFloat(5.));

    def           = this->add("tree_support_branch_diameter", coFloat);
    def->label    = L("Branch diameter");
    def->category = L("Support");
    def->tooltip  = L("This setting determines the initial diameter of support nodes.");
    def->sidetext = L("mm");
    def->min      = 1.0;
    def->max      = 10;
    def->mode     = comAdvanced;
    def->set_default_value(new ConfigOptionFloat(5.));

    def           = this->add("tree_support_branch_diameter_angle", coFloat);
    def->label    = L("Branch diameter angle");
    def->category = L("Support");
    def->tooltip  = L("The angle of the branches' diameter as they gradually become thicker towards the bottom. "
                       "An angle of 0 will cause the branches to have uniform thickness over their length. "
                       "A bit of an angle can increase stability of the tree support.");
    def->sidetext = L("°");
    def->min      = 0.0;
    def->max      = 15;
    def->mode     = comAdvanced;
    def->set_default_value(new ConfigOptionFloat(5.));

    def = this->add("tree_support_wall_count", coInt);
    def->label = L("Support wall loops");
    def->category = L("Support");
    def->tooltip = L("This setting specifies the count of support walls in the range of [0,2]. 0 means auto.");
    def->min = 0;
    def->max = 2;
    def->mode = comAdvanced;
    def->set_default_value(new ConfigOptionInt(0));

    def = this->add("chamber_temperatures", coInts);
    def->label = L("Chamber temperature");
    def->tooltip = L("Higher chamber temperature can help suppress or reduce warping and potentially lead to higher interlayer bonding strength for high temperature materials like ABS, ASA, PC, PA and so on."
                    "At the same time, the air filtration of ABS and ASA will get worse.While for PLA, PETG, TPU, PVA and other low temperature materials,"
                    "the actual chamber temperature should not be high to avoid cloggings, so 0 which stands for turning off is highly recommended"
                    );
    def->sidetext = "°C";
    def->full_label = L("Chamber temperature");
    def->min = 0;
    def->max = 80;
    def->set_default_value(new ConfigOptionInts{0});

    def = this->add("nozzle_temperature", coInts);
    def->label = L("Other layers");
    def->tooltip = L("Nozzle temperature for layers after the initial one");
    def->sidetext = "°C";
    def->full_label = L("Nozzle temperature");
    def->min = 0;
    def->max = max_temp;
    def->nullable = true;
    def->set_default_value(new ConfigOptionIntsNullable { 200 });

    def = this->add("nozzle_temperature_range_low", coInts);
    def->label = L("Min");
    //def->tooltip = "";
    def->sidetext = "°C";
    def->min = 0;
    def->max = max_temp;
    def->set_default_value(new ConfigOptionInts { 190 });

    def = this->add("nozzle_temperature_range_high", coInts);
    def->label = L("Max");
    //def->tooltip = "";
    def->sidetext = "°C";
    def->min = 0;
    def->max = max_temp;
    def->set_default_value(new ConfigOptionInts { 240 });

    def = this->add("head_wrap_detect_zone", coPoints);
    def->label ="Head wrap detect zone"; //do not need translation
    def->mode = comDevelop;
    def->set_default_value(new ConfigOptionPoints{});

    def = this->add("impact_strength_z", coFloats);
    def->label = L("Impact Strength Z");
    def->mode  = comDevelop;
    def->set_default_value(new ConfigOptionFloats{0});

    def = this->add("detect_thin_wall", coBool);
    def->label = L("Detect thin wall");
    def->category = L("Strength");
    def->tooltip = L("Detect thin wall which can't contain two line width. And use single line to print. "
                     "Maybe printed not very well, because it's not closed loop");
    def->mode = comAdvanced;
    def->set_default_value(new ConfigOptionBool(false));

    def = this->add("change_filament_gcode", coString);
    def->label = L("Change filament G-code");
    def->tooltip = L("This gcode is inserted when change filament, including T command to trigger tool change");
    def->multiline = true;
    def->full_width = true;
    def->height = 5;
    def->mode = comAdvanced;
    def->set_default_value(new ConfigOptionString(""));

    def = this->add("top_surface_line_width", coFloat);
    def->label = L("Top surface");
    def->category = L("Quality");
    def->tooltip = L("Line width for top surfaces");
    def->sidetext = L("mm");
    def->min = 0;
    def->mode = comAdvanced;
    def->set_default_value(new ConfigOptionFloat(0.4));

    def = this->add("top_surface_speed", coFloats);
    def->label = L("Top surface");
    def->category = L("Speed");
    def->tooltip = L("Speed of top surface infill which is solid");
    def->sidetext = L("mm/s");
    def->min = 0;
    def->mode = comAdvanced;
    def->nullable = true;
    def->set_default_value(new ConfigOptionFloatsNullable{100});

    def = this->add("top_shell_layers", coInt);
    def->label = L("Top shell layers");
    def->category = L("Strength");
    def->tooltip = L("This is the number of solid layers of top shell, including the top "
                     "surface layer. When the thickness calculated by this value is thinner "
                     "than top shell thickness, the top shell layers will be increased");
    def->full_label = L("Top solid layers");
    def->min = 0;
    def->set_default_value(new ConfigOptionInt(4));

    def = this->add("top_shell_thickness", coFloat);
    def->label = L("Top shell thickness");
    def->category = L("Strength");
    def->tooltip = L("The number of top solid layers is increased when slicing if the thickness calculated by top shell layers is "
                     "thinner than this value. This can avoid having too thin shell when layer height is small. 0 means that "
                     "this setting is disabled and thickness of top shell is absolutely determained by top shell layers");
    def->full_label = L("Top shell thickness");
    def->sidetext = L("mm");
    def->min = 0;
    def->set_default_value(new ConfigOptionFloat(0.6));

    def             = this->add("top_color_penetration_layers", coInt);
    def->label      = L("Top paint penetration layers");
    def->category   = L("Strength");
    def->tooltip    = L("This is  the number of layers of top paint penetration.");
    def->min        = 1;
    def->set_default_value(new ConfigOptionInt(4));

    def           = this->add("bottom_color_penetration_layers", coInt);
    def->label    = L("Bottom paint penetration layers");
    def->category = L("Strength");
    def->tooltip  = L("This is  the number of layers of top bottom penetration.");
    def->min      = 1;
    def->set_default_value(new ConfigOptionInt(3));

    def = this->add("travel_speed", coFloats);
    def->label = L("Travel");
    def->tooltip = L("Speed of travel which is faster and without extrusion");
    def->sidetext = L("mm/s");
    def->min = 1;
    def->mode = comAdvanced;
    def->nullable = true;
    def->set_default_value(new ConfigOptionFloatsNullable{120});

    def = this->add("travel_speed_z", coFloats);
    //def->label = L("Z travel");
    //def->tooltip = L("Speed of vertical travel along z axis. "
    //                 "This is typically lower because build plate or gantry is hard to be moved. "
    //                 "Zero means using travel speed directly in gcode, but will be limited by printer's ability when run gcode");
    def->sidetext = L("mm/s");
    def->min = 0;
    def->mode = comDevelop;
    def->nullable = true;
    def->set_default_value(new ConfigOptionFloatsNullable{0.});

    def = this->add("use_relative_e_distances", coBool);
    def->label = L("Use relative E distances");
    def->tooltip = L("If your firmware requires relative E values, check this, "
        "otherwise leave it unchecked. Must use relative e distance for Bambu printer");
    def->mode = comAdvanced;
    def->set_default_value(new ConfigOptionBool(true));

    def = this->add("use_firmware_retraction",coBool);
    def->label = L("Use firmware retraction");
    def->tooltip = L("Convert the retraction moves to G10 and G11 gcode");
    def->mode = comAdvanced;
    def->set_default_value(new ConfigOptionBool(false));

    def = this->add("wipe", coBools);
    def->label = L("Wipe while retracting");
    def->tooltip = L("Move nozzle along the last extrusion path when retracting to clean leaked material on nozzle. "
                     "This can minimize blob when printing new part after travel");
    def->mode = comAdvanced;
    def->nullable = true;
    def->set_default_value(new ConfigOptionBoolsNullable { false });

    def = this->add("wipe_distance", coFloats);
    def->label = L("Wipe Distance");
    def->tooltip = L("Describe how long the nozzle will move along the last path when retracting");
    def->sidetext = L("mm");
    def->min = 0;
    def->mode = comAdvanced;
    def->nullable = true;
    def->set_default_value(new ConfigOptionFloatsNullable { 2. });

    def = this->add("enable_prime_tower", coBool);
    def->label = L("Enable");
    def->tooltip = L("The wiping tower can be used to clean up the residue on the nozzle and stabilize the chamber pressure inside the nozzle, "
                    "in order to avoid appearance defects when printing objects.");
    def->mode = comSimple;
    def->set_default_value(new ConfigOptionBool(false));

    def = this->add("prime_tower_enable_framework", coBool);
    def->label = L("Internal ribs");
    def->tooltip = L("Enable internal ribs to increase the stability of the prime tower.");
    def->mode    = comAdvanced;
    def->set_default_value(new ConfigOptionBool(false));

    def          = this->add("enable_circle_compensation", coBool);
    def->label   = L("Auto circle contour-hole compensation");
    def->tooltip = L("Expirment feature to compensate the circle holes and circle contour. "
                     "This feature is used to improve the accuracy of the circle holes and contour within the diameter below 50mm. "
                     "Only support PLA Basic, PLA CF, PET CF, PETG CF and PETG HF.");
    def->mode    = comAdvanced;
    def->set_default_value(new ConfigOptionBool(false));

    def          = this->add("circle_compensation_manual_offset", coFloat);
    def->label   = L("User Customized Offset");
    def->sidetext = L("mm");
    def->tooltip = L("If you want to have tighter or looser assemble, you can set this value. When it is positive, it indicates tightening, otherwise, it indicates loosening");
    def->mode    = comAdvanced;
    def->set_default_value(new ConfigOptionFloat(0.0));

    def          = this->add("apply_scarf_seam_on_circles", coBool);
    def->label   = L("Scarf Seam On Compensation Circles");
    def->tooltip = L("Scarf seam will be applied on circles for better dimensional accuracy.");
    def->mode    = comAdvanced;
    def->set_default_value(new ConfigOptionBool(true));

    def             = this->add("circle_compensation_speed", coFloats);
    def->label      = L("Circle Compensation Speed");
    def->tooltip    = L("circle_compensation_speed");
    def->sidetext   = L("mm/s");
    def->min        = 0;
    def->set_default_value(new ConfigOptionFloats{200});

    def          = this->add("counter_coef_1", coFloats);
    def->label   = L("Counter Coef 1");
    def->tooltip = L("counter_coef_1");
    def->set_default_value(new ConfigOptionFloats{0});

    def          = this->add("counter_coef_2", coFloats);
    def->label   = L("Contour Coef 2");
    def->tooltip = L("counter_coef_2");
    def->set_default_value(new ConfigOptionFloats{0.025});

    def          = this->add("counter_coef_3", coFloats);
    def->label   = L("Contour Coef 3");
    def->tooltip = L("counter_coef_3");
    def->sidetext = L("mm");
    def->set_default_value(new ConfigOptionFloats{-0.11});

    def           = this->add("hole_coef_1", coFloats);
    def->label    = L("Hole Coef 1");
    def->tooltip  = L("hole_coef_1");
    def->set_default_value(new ConfigOptionFloats{0});

    def           = this->add("hole_coef_2", coFloats);
    def->label    = L("Hole Coef 2");
    def->tooltip  = L("hole_coef_2");
    def->set_default_value(new ConfigOptionFloats{-0.025});

    def           = this->add("hole_coef_3", coFloats);
    def->label    = L("Hole Coef 3");
    def->tooltip  = L("hole_coef_3");
    def->sidetext = L("mm");
    def->set_default_value(new ConfigOptionFloats{0.28});

    def           = this->add("counter_limit_min", coFloats);
    def->label    = L("Contour limit min");
    def->tooltip  = L("counter_limit_min");
    def->sidetext = L("mm");
    def->set_default_value(new ConfigOptionFloats{-0.04});

    def           = this->add("counter_limit_max", coFloats);
    def->label    = L("Contour limit max");
    def->tooltip  = L("counter_limit_max");
    def->sidetext = L("mm");
    def->set_default_value(new ConfigOptionFloats{0.05});

    def           = this->add("hole_limit_min", coFloats);
    def->label    = L("Hole limit min");
    def->tooltip  = L("hole_limit_min");
    def->sidetext = L("mm");
    def->set_default_value(new ConfigOptionFloats{0.08});

    def           = this->add("hole_limit_max", coFloats);
    def->label    = L("Hole limit max");
    def->tooltip  = L("hole_limit_max");
    def->sidetext = L("mm");
    def->set_default_value(new ConfigOptionFloats{0.25});

    def           = this->add("diameter_limit", coFloats);
    def->label    = L("Diameter limit");
    def->tooltip  = L("diameter_limit");
    def->sidetext = L("mm");
    def->set_default_value(new ConfigOptionFloats{50});

    def = this->add("flush_volumes_vector", coFloats);
    // BBS: remove _L()w
    def->label = ("Purging volumes - load/unload volumes");
    //def->tooltip = L("This vector saves required volumes to change from/to each tool used on the "
    //                 "wipe tower. These values are used to simplify creation of the full purging "
    //                 "volumes below.");

    // BBS: change 70.f => 140.f
    def->set_default_value(new ConfigOptionFloats { 140.f, 140.f, 140.f, 140.f, 140.f, 140.f, 140.f, 140.f });

    def = this->add("flush_volumes_matrix", coFloats);
    def->label = L("Purging volumes");
    //def->tooltip = L("This matrix describes volumes (in cubic milimetres) required to purge the"
    //                 " new filament on the wipe tower for any given pair of tools.");
    // BBS: change 140.f => 280.f
    def->set_default_value(new ConfigOptionFloats {   0.f, 280.f, 280.f, 280.f,
                                                    280.f,   0.f, 280.f, 280.f,
                                                    280.f, 280.f,   0.f, 280.f,
                                                    280.f, 280.f, 280.f,   0.f });

    def = this->add("flush_multiplier", coFloats);
    def->label = L("Flush multiplier");
    def->tooltip = L("The actual flushing volumes is equal to the flush multiplier multiplied by the flushing volumes in the table.");
    def->sidetext = "";
    def->set_default_value(new ConfigOptionFloats{1.0});

    // // BBS
    // def = this->add("prime_volume", coFloat);
    // def->label = L("Prime volume");
    // def->tooltip = L("The volume of material to prime extruder on tower.");
    // def->sidetext = L("mm³");
    // def->min = 1.0;
    // def->mode = comSimple;
    // def->set_default_value(new ConfigOptionFloat(45.));

    def = this->add("wipe_tower_x", coFloats);
    //def->label = L("Position X");
    //def->tooltip = L("X coordinate of the left front corner of a wipe tower");
    //def->sidetext = L("mm");
    def->mode = comDevelop;
    // BBS: change data type to floats to add partplate logic
    def->set_default_value(new ConfigOptionFloats{ 15. });

    def = this->add("wipe_tower_y", coFloats);
    //def->label = L("Position Y");
    //def->tooltip = L("Y coordinate of the left front corner of a wipe tower");
    //def->sidetext = L("mm");
    def->mode = comDevelop;
    // BBS: change data type to floats to add partplate logic
    def->set_default_value(new ConfigOptionFloats{ 220. });

    def = this->add("prime_tower_width", coFloat);
    def->label = L("Width");
    def->tooltip = L("Width of prime tower");
    def->sidetext = L("mm");
    def->min = 2.0;
    def->mode = comSimple;
    def->set_default_value(new ConfigOptionFloat(35.));

    def = this->add("wipe_tower_rotation_angle", coFloat);
    //def->label = L("Wipe tower rotation angle");
    //def->tooltip = L("Wipe tower rotation angle with respect to x-axis.");
    //def->sidetext = L("°");
    def->mode = comDevelop;
    def->set_default_value(new ConfigOptionFloat(0.));

    def           = this->add("prime_tower_max_speed", coFloat);
    def->label    = L("Max speed");
    def->tooltip  = L("The maximum printing speed on the prime tower excluding ramming.");
    def->sidetext = L("mm/s");
    def->mode     = comAdvanced;
    def->min      = 10;
    def->set_default_value(new ConfigOptionFloat(90.));

    def           = this->add("prime_tower_lift_speed", coFloat);
    def->set_default_value(new ConfigOptionFloat(90.));

    def = this->add("prime_tower_lift_height", coFloat);
    def->set_default_value(new ConfigOptionFloat(-1));

    def = this->add("prime_tower_brim_width", coFloat);
    def->gui_type = ConfigOptionDef::GUIType::f_enum_open;
    def->label = L("Brim width");
    def->tooltip = L("Brim width of prime tower, negative number means auto calculated width based on the height of prime tower.");
    def->sidetext = L("mm");
    def->mode = comAdvanced;
    def->min = -1;
    def->enum_values.push_back("-1");
    def->enum_labels.push_back(L("Auto"));
    def->set_default_value(new ConfigOptionFloat(3.));

    def           = this->add("prime_tower_extra_rib_length", coFloat);
    def->label    = L("Extra rib length");
    def->tooltip  = L("Positive values can increase the size of the rib wall, while negative values can reduce the size."
                       "However, the size of the rib wall can not be smaller than that determined by the cleaning volume.");
    def->sidetext = L("mm");
    def->max      = 300;
    def->mode     = comAdvanced;
    def->set_default_value(new ConfigOptionFloat(0));

    def           = this->add("prime_tower_rib_width", coFloat);
    def->label    = L("Rib width");
    def->tooltip  = L("Rib width");
    def->sidetext = L("mm");
    def->mode     = comAdvanced;
    def->min      = 0;
    def->set_default_value(new ConfigOptionFloat(8));

    def          = this->add("prime_tower_skip_points", coBool);
    def->label   = L("Skip points");
    def->tooltip = L("The wall of prime tower will skip the start points of wipe path");
    def->mode    = comAdvanced;
    def->set_default_value(new ConfigOptionBool(true));

    def      = this->add("prime_tower_flat_ironing", coBool);
    def->mode = comAdvanced;
    def->set_default_value(new ConfigOptionBool(false));

    def          = this->add("prime_tower_rib_wall", coBool);
    def->label   = L("Rib wall");
    def->tooltip = L("The wall of prime tower will add four ribs and make its "
                     "cross-section as close to a square as possible, so the width will be fixed.");
    def->mode    = comSimple;
    def->set_default_value(new ConfigOptionBool(true));

    def          = this->add("prime_tower_fillet_wall", coBool);
    def->label   = L("Fillet wall");
    def->tooltip = L("The wall of prime tower will fillet");
    def->mode    = comAdvanced;
    def->set_default_value(new ConfigOptionBool(true));

    def           = this->add("prime_tower_infill_gap", coPercent);
    def->label    = L("Infill gap");
    def->tooltip  = L("Infill gap");
    def->sidetext = L("%");
    def->mode     = comAdvanced;
    def->min      = 100;
    def->set_default_value(new ConfigOptionPercent(150));

    def = this->add("flush_into_infill", coBool);
    def->category = L("Flush options");
    def->label = L("Flush into objects' infill");
    def->tooltip = L("Purging after filament change will be done inside objects' infills. "
        "This may lower the amount of waste and decrease the print time. "
        "If the walls are printed with transparent filament, the mixed color infill will be seen outside. "
        "It will not take effect, unless the prime tower is enabled.");
    def->set_default_value(new ConfigOptionBool(false));

    def = this->add("flush_into_support", coBool);
    def->category = L("Flush options");
    def->label = L("Flush into objects' support");
    def->tooltip = L("Purging after filament change will be done inside objects' support. "
        "This may lower the amount of waste and decrease the print time. "
        "It will not take effect, unless the prime tower is enabled.");
    def->set_default_value(new ConfigOptionBool(true));

    def = this->add("flush_into_objects", coBool);
    def->category = L("Flush options");
    def->label = L("Flush into this object");
    def->tooltip = L("This object will be used to purge the nozzle after a filament change to save filament and decrease the print time. "
        "Colours of the objects will be mixed as a result. "
        "It will not take effect, unless the prime tower is enabled.");
    def->set_default_value(new ConfigOptionBool(false));

    //BBS
    //def = this->add("wipe_tower_bridging", coFloat);
    //def->label = L("Maximal bridging distance");
    //def->tooltip = L("Maximal distance between supports on sparse infill sections.");
    //def->sidetext = L("mm");
    //def->mode = comAdvanced;
    //def->set_default_value(new ConfigOptionFloat(10.));

    def = this->add("xy_hole_compensation", coFloat);
    def->label = L("X-Y hole compensation");
    def->category = L("Quality");
    def->tooltip = L("Holes of object will be grown or shrunk in XY plane by the configured value. "
                     "Positive value makes holes bigger. Negative value makes holes smaller. "
                     "This function is used to adjust size slightly when the object has assembling issue");
    def->sidetext = L("mm");
    def->mode = comAdvanced;
    def->set_default_value(new ConfigOptionFloat(0));

    def = this->add("xy_contour_compensation", coFloat);
    def->label = L("X-Y contour compensation");
    def->category = L("Quality");
    def->tooltip = L("Contour of object will be grown or shrunk in XY plane by the configured value. "
                     "Positive value makes contour bigger. Negative value makes contour smaller. "
                     "This function is used to adjust size slightly when the object has assembling issue");
    def->sidetext = L("mm");
    def->mode = comAdvanced;
    def->set_default_value(new ConfigOptionFloat(0));

    def = this->add("wall_generator", coEnum);
    def->label = L("Wall generator");
    def->category = L("Quality");
    def->tooltip = L("Classic wall generator produces walls with constant extrusion width and for "
        "very thin areas is used gap-fill. "
        "Arachne engine produces walls with variable extrusion width");
    def->enum_keys_map = &ConfigOptionEnum<PerimeterGeneratorType>::get_enum_values();
    def->enum_values.push_back("classic");
    def->enum_values.push_back("arachne");
    def->enum_labels.push_back(L("Classic"));
    def->enum_labels.push_back(L("Arachne"));
    def->mode = comAdvanced;
    def->set_default_value(new ConfigOptionEnum<PerimeterGeneratorType>(PerimeterGeneratorType::Arachne));

    def = this->add("wall_transition_length", coPercent);
    def->label = L("Wall transition length");
    def->category = L("Quality");
    def->tooltip = L("When transitioning between different numbers of walls as the part becomes "
        "thinner, a certain amount of space is allotted to split or join the wall segments. "
        "It's expressed as a percentage over nozzle diameter");
    def->sidetext = "%";
    def->mode = comAdvanced;
    def->min = 0;
    def->set_default_value(new ConfigOptionPercent(100));

    def = this->add("wall_transition_filter_deviation", coPercent);
    def->label = L("Wall transitioning filter margin");
    def->category = L("Quality");
    def->tooltip = L("Prevent transitioning back and forth between one extra wall and one less. This "
        "margin extends the range of extrusion widths which follow to [Minimum wall width "
        "- margin, 2 * Minimum wall width + margin]. Increasing this margin "
        "reduces the number of transitions, which reduces the number of extrusion "
        "starts/stops and travel time. However, large extrusion width variation can lead to "
        "under- or overextrusion problems. "
        "It's expressed as a percentage over nozzle diameter");
    def->sidetext = "%";
    def->mode = comAdvanced;
    def->min = 0;
    def->set_default_value(new ConfigOptionPercent(25));

    def = this->add("wall_transition_angle", coFloat);
    def->label = L("Wall transitioning threshold angle");
    def->category = L("Quality");
    def->tooltip = L("When to create transitions between even and odd numbers of walls. A wedge shape with"
        " an angle greater than this setting will not have transitions and no walls will be "
        "printed in the center to fill the remaining space. Reducing this setting reduces "
        "the number and length of these center walls, but may leave gaps or overextrude");
    def->sidetext = L("°");
    def->mode = comAdvanced;
    def->min = 1.;
    def->max = 59.;
    def->set_default_value(new ConfigOptionFloat(10.));

    def = this->add("wall_distribution_count", coInt);
    def->label = L("Wall distribution count");
    def->category = L("Quality");
    def->tooltip = L("The number of walls, counted from the center, over which the variation needs to be "
        "spread. Lower values mean that the outer walls don't change in width");
    def->mode = comAdvanced;
    def->min = 1;
    def->set_default_value(new ConfigOptionInt(1));

    def = this->add("min_feature_size", coPercent);
    def->label = L("Minimum feature size");
    def->category = L("Quality");
    def->tooltip = L("Minimum thickness of thin features. Model features that are thinner than this value will "
        "not be printed, while features thicker than the Minimum feature size will be widened to "
        "the Minimum wall width. "
        "It's expressed as a percentage over nozzle diameter");
    def->sidetext = "%";
    def->mode = comAdvanced;
    def->min = 0;
    def->set_default_value(new ConfigOptionPercent(25));

    def = this->add("min_bead_width", coPercent);
    def->label = L("Minimum wall width");
    def->category = L("Quality");
    def->tooltip = L("Width of the wall that will replace thin features (according to the Minimum feature size) "
        "of the model. If the Minimum wall width is thinner than the thickness of the feature,"
        " the wall will become as thick as the feature itself. "
        "It's expressed as a percentage over nozzle diameter");
    def->sidetext = "%";
    def->mode = comAdvanced;
    def->min = 0;
    def->set_default_value(new ConfigOptionPercent(85));

    // Declare retract values for filament profile, overriding the printer's extruder profile.
    for (auto& opt_key : filament_extruder_override_keys) {
        const std::string filament_prefix = "filament_";
        std::string extruder_raw_key = opt_key.substr(opt_key.find(filament_prefix) + filament_prefix.length());
        auto it_opt = options.find(extruder_raw_key);
        assert(it_opt != options.end());
        def = this->add_nullable(opt_key, it_opt->second.type);
        def->label 		= it_opt->second.label;
        def->full_label = it_opt->second.full_label;
        def->tooltip 	= it_opt->second.tooltip;
        def->sidetext   = it_opt->second.sidetext;
        def->enum_keys_map = it_opt->second.enum_keys_map;
        def->enum_labels   = it_opt->second.enum_labels;
        def->enum_values   = it_opt->second.enum_values;
        def->min        = it_opt->second.min;
        def->max        = it_opt->second.max;
        //BBS: shown specific filament retract config because we hide the machine retract into comDevelop mode
        if (opt_key =="filament_retraction_length"||
            opt_key=="filament_z_hop" ||
            opt_key== "filament_long_retractions_when_cut" ||
            opt_key=="filament_retraction_distances_when_cut")
            def->mode       = comSimple;
        else
            def->mode       = comAdvanced;
        switch (def->type) {
        case coFloats: def->set_default_value(new ConfigOptionFloatsNullable(static_cast<const ConfigOptionFloatsNullable*>(it_opt->second.default_value.get())->values)); break;
        case coPercents: def->set_default_value(new ConfigOptionPercentsNullable(static_cast<const ConfigOptionPercentsNullable*>(it_opt->second.default_value.get())->values)); break;
        case coBools: def->set_default_value(new ConfigOptionBoolsNullable(static_cast<const ConfigOptionBools*>(it_opt->second.default_value.get())->values)); break;
        case coEnums: def->set_default_value(new ConfigOptionEnumsGenericNullable(static_cast<const ConfigOptionEnumsGenericNullable*>(it_opt->second.default_value.get())->values)); break;
        default: assert(false);
        }
    }

    def = this->add("detect_narrow_internal_solid_infill", coBool);
    def->label = L("Detect narrow internal solid infill");
    def->category = L("Strength");
    def->tooltip = L("This option will auto detect narrow internal solid infill area."
                   " If enabled, concentric pattern will be used for the area to speed printing up."
                   " Otherwise, rectilinear pattern is used defaultly.");
    def->mode = comAdvanced;
    def->set_default_value(new ConfigOptionBool(true));
}

void PrintConfigDef::init_extruder_option_keys()
{
    // ConfigOptionFloats, ConfigOptionPercents, ConfigOptionBools, ConfigOptionStrings
    m_extruder_option_keys = {
        "extruder_type", "nozzle_diameter", "default_nozzle_volume_type", "min_layer_height", "max_layer_height", "extruder_offset",
        "retraction_length", "z_hop", "z_hop_types", "retraction_speed", "retract_lift_above", "retract_lift_below","deretraction_speed",
        "retract_before_wipe", "retract_restart_extra", "retraction_minimum_travel", "wipe", "wipe_distance",
        "retract_when_changing_layer", "retract_length_toolchange", "retract_restart_extra_toolchange", "extruder_colour",
        "default_filament_profile","retraction_distances_when_cut","long_retractions_when_cut"
    };

    m_extruder_retract_keys = {
        "deretraction_speed",
        "long_retractions_when_cut",
        "retract_before_wipe",
        "retract_lift_above",
        "retract_lift_below",
        "retract_restart_extra",
        "retract_when_changing_layer",
        "retraction_distances_when_cut",
        "retraction_length",
        "retraction_minimum_travel",
        "retraction_speed",
        "wipe",
        "wipe_distance",
        "z_hop",
        "z_hop_types"
    };
    assert(std::is_sorted(m_extruder_retract_keys.begin(), m_extruder_retract_keys.end()));
}

void PrintConfigDef::init_filament_option_keys()
{
    m_filament_option_keys = {
        "filament_diameter", "min_layer_height", "max_layer_height",
        "retraction_length", "z_hop", "z_hop_types", "retraction_speed", "deretraction_speed",
        "retract_before_wipe", "retract_restart_extra", "retraction_minimum_travel", "wipe", "wipe_distance",
        "retract_when_changing_layer", "retract_length_toolchange", "retract_restart_extra_toolchange", "filament_colour",
        "default_filament_profile","retraction_distances_when_cut","long_retractions_when_cut"
    };

    m_filament_retract_keys = {
        "deretraction_speed",
        "long_retractions_when_cut",
        "retract_before_wipe",
        "retract_restart_extra",
        "retract_when_changing_layer",
        "retraction_distances_when_cut",
        "retraction_length",
        "retraction_minimum_travel",
        "retraction_speed",
        "wipe",
        "wipe_distance",
        "z_hop",
        "z_hop_types"
    };
    assert(std::is_sorted(m_filament_retract_keys.begin(), m_filament_retract_keys.end()));
}

void PrintConfigDef::init_sla_params()
{
    ConfigOptionDef* def;

    // SLA Printer settings

    def = this->add("display_width", coFloat);
    def->label = L(" ");
    def->tooltip = L(" ");
    def->min = 1;
    def->set_default_value(new ConfigOptionFloat(120.));

    def = this->add("display_height", coFloat);
    def->label = L(" ");
    def->tooltip = L(" ");
    def->min = 1;
    def->set_default_value(new ConfigOptionFloat(68.));

    def = this->add("display_pixels_x", coInt);
    def->full_label = L(" ");
    def->label = ("X");
    def->tooltip = L(" ");
    def->min = 100;
    def->set_default_value(new ConfigOptionInt(2560));

    def = this->add("display_pixels_y", coInt);
    def->label = ("Y");
    def->tooltip = L(" ");
    def->min = 100;
    def->set_default_value(new ConfigOptionInt(1440));

    def = this->add("display_mirror_x", coBool);
    def->full_label = L(" ");
    def->label = L(" ");
    def->tooltip = L(" ");
    def->mode = comAdvanced;
    def->set_default_value(new ConfigOptionBool(true));

    def = this->add("display_mirror_y", coBool);
    def->full_label = L(" ");
    def->label = L(" ");
    def->tooltip = L(" ");
    def->mode = comAdvanced;
    def->set_default_value(new ConfigOptionBool(false));

    def = this->add("display_orientation", coEnum);
    def->label = L(" ");
    def->tooltip = L(" ");
    def->enum_keys_map = &ConfigOptionEnum<SLADisplayOrientation>::get_enum_values();
    def->enum_values.push_back("landscape");
    def->enum_values.push_back("portrait");
    def->enum_labels.push_back(L(" "));
    def->enum_labels.push_back(L(" "));
    def->mode = comAdvanced;
    def->set_default_value(new ConfigOptionEnum<SLADisplayOrientation>(sladoPortrait));

    def = this->add("fast_tilt_time", coFloat);
    def->label = L(" ");
    def->full_label = L(" ");
    def->tooltip = L(" ");
    def->sidetext = L(" ");
    def->min = 0;
    def->mode = comAdvanced;
    def->set_default_value(new ConfigOptionFloat(5.));

    def = this->add("slow_tilt_time", coFloat);
    def->label = L(" ");
    def->full_label = L(" ");
    def->tooltip = L(" ");
    def->sidetext = L(" ");
    def->min = 0;
    def->mode = comAdvanced;
    def->set_default_value(new ConfigOptionFloat(8.));

    def = this->add("area_fill", coFloat);
    def->label = L(" ");
    def->tooltip = L(" ");
    def->sidetext = L(" ");
    def->min = 0;
    def->mode = comAdvanced;
    def->set_default_value(new ConfigOptionFloat(50.));

    def = this->add("relative_correction", coFloats);
    def->label = L(" ");
    def->full_label = L(" ");
    def->tooltip  = L(" ");
    def->min = 0;
    def->mode = comAdvanced;
    def->set_default_value(new ConfigOptionFloats( { 1., 1.} ));

    def = this->add("relative_correction_x", coFloat);
    def->label = L(" ");
    def->full_label = L(" ");
    def->tooltip  = L(" ");
    def->min = 0;
    def->mode = comAdvanced;
    def->set_default_value(new ConfigOptionFloat(1.));

    def = this->add("relative_correction_y", coFloat);
    def->label = L(" ");
    def->full_label = L(" ");
    def->tooltip  = L(" ");
    def->min = 0;
    def->mode = comAdvanced;
    def->set_default_value(new ConfigOptionFloat(1.));

    def = this->add("relative_correction_z", coFloat);
    def->label = L(" ");
    def->full_label = L(" ");
    def->tooltip  = L(" ");
    def->min = 0;
    def->mode = comAdvanced;
    def->set_default_value(new ConfigOptionFloat(1.));

    def = this->add("absolute_correction", coFloat);
    def->label = L(" ");
    def->full_label = L(" ");
    def->tooltip  = L(" ");
    def->mode = comAdvanced;
    def->set_default_value(new ConfigOptionFloat(0.0));

    def = this->add("elefant_foot_min_width", coFloat);
    def->label = L(" ");
    def->category = L(" ");
    def->tooltip = L(" ");
    def->sidetext = L(" ");
    def->min = 0;
    def->mode = comAdvanced;
    def->set_default_value(new ConfigOptionFloat(0.2));

    def = this->add("gamma_correction", coFloat);
    def->label = L(" ");
    def->full_label = L(" ");
    def->tooltip  = L(" ");
    def->min = 0;
    def->max = 1;
    def->mode = comAdvanced;
    def->set_default_value(new ConfigOptionFloat(1.0));


    // SLA Material settings.

    def = this->add("material_colour", coString);
    def->label = L(" ");
    def->tooltip = L(" ");
    def->gui_type = ConfigOptionDef::GUIType::color;
    def->set_default_value(new ConfigOptionString("#29B2B2"));

    def = this->add("material_type", coString);
    def->label = L(" ");
    def->tooltip = L(" ");
    def->gui_type = ConfigOptionDef::GUIType::f_enum_open;   // TODO: ???
    def->gui_flags = "show_value";
    def->enum_values.push_back("Tough");
    def->enum_values.push_back("Flexible");
    def->enum_values.push_back("Casting");
    def->enum_values.push_back("Dental");
    def->enum_values.push_back("Heat-resistant");
    def->set_default_value(new ConfigOptionString("Tough"));

    def = this->add("initial_layer_height", coFloat);
    def->label = L(" ");
    def->tooltip = L(" ");
    def->sidetext = L(" ");
    def->min = 0;
    def->set_default_value(new ConfigOptionFloat(0.3));

    def = this->add("bottle_volume", coFloat);
    def->label = L(" ");
    def->tooltip = L(" ");
    def->sidetext = L(" ");
    def->min = 50;
    def->set_default_value(new ConfigOptionFloat(1000.0));

    def = this->add("bottle_weight", coFloat);
    def->label = L(" ");
    def->tooltip = L(" ");
    def->sidetext = L(" ");
    def->min = 0;
    def->set_default_value(new ConfigOptionFloat(1.0));

    def = this->add("material_density", coFloat);
    def->label = L(" ");
    def->tooltip = L(" ");
    def->sidetext = L(" ");
    def->min = 0;
    def->set_default_value(new ConfigOptionFloat(1.0));

    def = this->add("bottle_cost", coFloat);
    def->label = L(" ");
    def->tooltip = L(" ");
    def->sidetext = L(" ");
    def->min = 0;
    def->set_default_value(new ConfigOptionFloat(0.0));

    def = this->add("faded_layers", coInt);
    def->label = L(" ");
    def->tooltip = L(" ");
    def->min = 3;
    def->max = 20;
    def->mode = comAdvanced;
    def->set_default_value(new ConfigOptionInt(10));

    def = this->add("min_exposure_time", coFloat);
    def->label = L(" ");
    def->tooltip = L(" ");
    def->sidetext = L(" ");
    def->min = 0;
    def->mode = comAdvanced;
    def->set_default_value(new ConfigOptionFloat(0));

    def = this->add("max_exposure_time", coFloat);
    def->label = L(" ");
    def->tooltip = L(" ");
    def->sidetext = L(" ");
    def->min = 0;
    def->mode = comAdvanced;
    def->set_default_value(new ConfigOptionFloat(100));

    def = this->add("exposure_time", coFloat);
    def->label = L(" ");
    def->tooltip = L(" ");
    def->sidetext = L(" ");
    def->min = 0;
    def->set_default_value(new ConfigOptionFloat(10));

    def = this->add("min_initial_exposure_time", coFloat);
    def->label = L(" ");
    def->tooltip = L(" ");
    def->sidetext = L(" ");
    def->min = 0;
    def->mode = comAdvanced;
    def->set_default_value(new ConfigOptionFloat(0));

    def = this->add("max_initial_exposure_time", coFloat);
    def->label = L(" ");
    def->tooltip = L(" ");
    def->sidetext = L(" ");
    def->min = 0;
    def->mode = comAdvanced;
    def->set_default_value(new ConfigOptionFloat(150));

    def = this->add("initial_exposure_time", coFloat);
    def->label = L(" ");
    def->tooltip = L(" ");
    def->sidetext = L(" ");
    def->min = 0;
    def->set_default_value(new ConfigOptionFloat(15));

    def = this->add("material_correction", coFloats);
    def->full_label = L(" ");
    def->tooltip  = L(" ");
    def->min = 0;
    def->mode = comAdvanced;
    def->set_default_value(new ConfigOptionFloats( { 1., 1., 1. } ));

    def = this->add("material_correction_x", coFloat);
    def->full_label = L(" ");
    def->tooltip  = L(" ");
    def->min = 0;
    def->mode = comAdvanced;
    def->set_default_value(new ConfigOptionFloat(1.));

    def = this->add("material_correction_y", coFloat);
    def->full_label = L(" ");
    def->tooltip  = L(" ");
    def->min = 0;
    def->mode = comAdvanced;
    def->set_default_value(new ConfigOptionFloat(1.));

    def = this->add("material_correction_z", coFloat);
    def->full_label = L(" ");
    def->tooltip  = L(" ");
    def->min = 0;
    def->mode = comAdvanced;
    def->set_default_value(new ConfigOptionFloat(1.));

    def = this->add("material_vendor", coString);
    def->set_default_value(new ConfigOptionString(""));
    def->cli = ConfigOptionDef::nocli;

    def = this->add("default_sla_material_profile", coString);
    def->label = L(" ");
    def->tooltip = L(" ");
    def->set_default_value(new ConfigOptionString());
    def->cli = ConfigOptionDef::nocli;

    def = this->add("sla_material_settings_id", coString);
    def->set_default_value(new ConfigOptionString(""));
    def->cli = ConfigOptionDef::nocli;

    def = this->add("default_sla_print_profile", coString);
    def->label = L(" ");
    def->tooltip = L(" ");
    def->set_default_value(new ConfigOptionString());
    def->cli = ConfigOptionDef::nocli;

    def = this->add("sla_print_settings_id", coString);
    def->set_default_value(new ConfigOptionString(""));
    def->cli = ConfigOptionDef::nocli;

    def = this->add("supports_enable", coBool);
    def->label = L(" ");
    def->category = L(" ");
    def->tooltip = L(" ");
    def->mode = comSimple;
    def->set_default_value(new ConfigOptionBool(true));

    def = this->add("support_head_front_diameter", coFloat);
    def->label = L(" ");
    def->category = L(" ");
    def->tooltip = L(" ");
    def->sidetext = L(" ");
    def->min = 0;
    def->mode = comAdvanced;
    def->set_default_value(new ConfigOptionFloat(0.4));

    def = this->add("support_head_penetration", coFloat);
    def->label = L(" ");
    def->category = L(" ");
    def->tooltip = L(" ");
    def->sidetext = L(" ");
    def->mode = comAdvanced;
    def->min = 0;
    def->set_default_value(new ConfigOptionFloat(0.2));

    def = this->add("support_head_width", coFloat);
    def->label = L(" ");
    def->category = L(" ");
    def->tooltip = L(" ");
    def->sidetext = L(" ");
    def->min = 0;
    def->max = 20;
    def->mode = comAdvanced;
    def->set_default_value(new ConfigOptionFloat(1.0));

    def = this->add("support_pillar_diameter", coFloat);
    def->label = L(" ");
    def->category = L(" ");
    def->tooltip = L(" ");
    def->sidetext = L(" ");
    def->min = 0;
    def->max = 15;
    def->mode = comSimple;
    def->set_default_value(new ConfigOptionFloat(1.0));

    def = this->add("support_small_pillar_diameter_percent", coPercent);
    def->label = L(" ");
    def->category = L(" ");
    def->tooltip = L(" ");
    def->sidetext = L(" ");
    def->min = 1;
    def->max = 100;
    def->mode = comAdvanced;
    def->set_default_value(new ConfigOptionPercent(50));

    def = this->add("support_max_bridges_on_pillar", coInt);
    def->label = L(" ");
    def->tooltip = L(" ");
    def->min = 0;
    def->max = 50;
    def->mode = comAdvanced;
    def->set_default_value(new ConfigOptionInt(3));

    def = this->add("support_pillar_connection_mode", coEnum);
    def->label = L(" ");
    def->tooltip = L(" ");
    def->enum_keys_map = &ConfigOptionEnum<SLAPillarConnectionMode>::get_enum_values();
    def->enum_values.push_back("zigzag");
    def->enum_values.push_back("cross");
    def->enum_values.push_back("dynamic");
    def->enum_labels.push_back(L(" "));
    def->enum_labels.push_back(L(" "));
    def->enum_labels.push_back(L(" "));
    def->mode = comAdvanced;
    def->set_default_value(new ConfigOptionEnum<SLAPillarConnectionMode>(slapcmDynamic));

    def = this->add("support_buildplate_only", coBool);
    def->label = L(" ");
    def->category = L(" ");
    def->tooltip = L(" ");
    def->mode = comSimple;
    def->set_default_value(new ConfigOptionBool(false));

    def = this->add("support_pillar_widening_factor", coFloat);
    def->label = L(" ");
    def->category = L(" ");
    def->tooltip = L(" ");
    def->min = 0;
    def->max = 1;
    def->mode = comAdvanced;
    def->set_default_value(new ConfigOptionFloat(0.0));

    def = this->add("support_base_diameter", coFloat);
    def->label = L(" ");
    def->category = L(" ");
    def->tooltip = L(" ");
    def->sidetext = L(" ");
    def->min = 0;
    def->max = 30;
    def->mode = comAdvanced;
    def->set_default_value(new ConfigOptionFloat(4.0));

    def = this->add("support_base_height", coFloat);
    def->label = L(" ");
    def->category = L(" ");
    def->tooltip = L(" ");
    def->sidetext = L(" ");
    def->min = 0;
    def->mode = comAdvanced;
    def->set_default_value(new ConfigOptionFloat(1.0));

    def = this->add("support_base_safety_distance", coFloat);
    def->label = L(" ");
    def->category = L(" ");
    def->tooltip  = L(" ");
    def->sidetext = L(" ");
    def->min = 0;
    def->max = 10;
    def->mode = comAdvanced;
    def->set_default_value(new ConfigOptionFloat(1));

    def = this->add("support_critical_angle", coFloat);
    def->label = L(" ");
    def->category = L(" ");
    def->tooltip = L(" ");
    def->sidetext = L(" ");
    def->min = 0;
    def->max = 90;
    def->mode = comAdvanced;
    def->set_default_value(new ConfigOptionFloat(45));

    def = this->add("support_max_bridge_length", coFloat);
    def->label = L(" ");
    def->category = L(" ");
    def->tooltip = L(" ");
    def->sidetext = L(" ");
    def->min = 0;
    def->mode = comAdvanced;
    def->set_default_value(new ConfigOptionFloat(15.0));

    def = this->add("support_max_pillar_link_distance", coFloat);
    def->label = L(" ");
    def->category = L(" ");
    def->tooltip = L(" ");
    def->sidetext = L(" ");
    def->min = 0;   // 0 means no linking
    def->mode = comAdvanced;
    def->set_default_value(new ConfigOptionFloat(10.0));

    def = this->add("support_object_elevation", coFloat);
    def->label = L(" ");
    def->category = L(" ");
    def->tooltip = L(" ");
    def->sidetext = L(" ");
    def->min = 0;
    def->max = 150; // This is the max height of print on SL1
    def->mode = comAdvanced;
    def->set_default_value(new ConfigOptionFloat(5.0));

    def = this->add("support_points_density_relative", coInt);
    def->label = L(" ");
    def->category = L(" ");
    def->tooltip = L(" ");
    def->sidetext = L(" ");
    def->min = 0;
    def->set_default_value(new ConfigOptionInt(100));

    def = this->add("support_points_minimal_distance", coFloat);
    def->label = L(" ");
    def->category = L(" ");
    def->tooltip = L(" ");
    def->sidetext = L("mm");
    def->min = 0;
    def->set_default_value(new ConfigOptionFloat(1.));

    def = this->add("pad_enable", coBool);
    def->label = L(" ");
    def->category = L(" ");
    def->tooltip = L(" ");
    def->mode = comSimple;
    def->set_default_value(new ConfigOptionBool(true));

    def = this->add("pad_wall_thickness", coFloat);
    def->label = L(" ");
    def->category = L(" ");
     def->tooltip = L(" ");
    def->sidetext = L(" ");
    def->min = 0;
    def->max = 30;
    def->mode = comSimple;
    def->set_default_value(new ConfigOptionFloat(2.0));

    def = this->add("pad_wall_height", coFloat);
    def->label = L(" ");
    def->tooltip = L(" ");
    def->category = L(" ");
    def->sidetext = L(" ");
    def->min = 0;
    def->max = 30;
    def->mode = comAdvanced;
    def->set_default_value(new ConfigOptionFloat(0.));

    def = this->add("pad_brim_size", coFloat);
    def->label = L(" ");
    def->tooltip = L(" ");
    def->category = L(" ");
    def->sidetext = L(" ");
    def->min = 0;
    def->max = 30;
    def->mode = comAdvanced;
    def->set_default_value(new ConfigOptionFloat(1.6));

    def = this->add("pad_max_merge_distance", coFloat);
    def->label = L(" ");
    def->category = L(" ");
     def->tooltip = L(" ");
    def->sidetext = L(" ");
    def->min = 0;
    def->mode = comAdvanced;
    def->set_default_value(new ConfigOptionFloat(50.0));

    def = this->add("pad_wall_slope", coFloat);
    def->label = L(" ");
    def->category = L(" ");
    def->tooltip = L(" ");
    def->sidetext = L(" ");
    def->min = 45;
    def->max = 90;
    def->mode = comAdvanced;
    def->set_default_value(new ConfigOptionFloat(90.0));

    def = this->add("pad_around_object", coBool);
    def->label = L(" ");
    def->category = L(" ");
    def->tooltip = L(" ");
    def->mode = comSimple;
    def->set_default_value(new ConfigOptionBool(false));

    def = this->add("pad_around_object_everywhere", coBool);
    def->label = L(" ");
    def->category = L(" ");
    def->tooltip = L(" ");
    def->mode = comSimple;
    def->set_default_value(new ConfigOptionBool(false));

    def = this->add("pad_object_gap", coFloat);
    def->label = L(" ");
    def->category = L(" ");
    def->tooltip  = L(" ");
    def->sidetext = L(" ");
    def->min = 0;
    def->max = 10;
    def->mode = comAdvanced;
    def->set_default_value(new ConfigOptionFloat(1));

    def = this->add("pad_object_connector_stride", coFloat);
    def->label = L(" ");
    def->category = L(" ");
    def->tooltip = L(" ");
    def->sidetext = L(" ");
    def->min = 0;
    def->mode = comAdvanced;
    def->set_default_value(new ConfigOptionFloat(10));

    def = this->add("pad_object_connector_width", coFloat);
    def->label = L(" ");
    def->category = L(" ");
    def->tooltip  = L(" ");
    def->sidetext = L(" ");
    def->min = 0;
    def->mode = comAdvanced;
    def->set_default_value(new ConfigOptionFloat(0.5));

    def = this->add("pad_object_connector_penetration", coFloat);
    def->label = L(" ");
    def->category = L(" ");
    def->tooltip  = L(" ");
    def->sidetext = L(" ");
    def->min = 0;
    def->mode = comAdvanced;
    def->set_default_value(new ConfigOptionFloat(0.3));

    def = this->add("hollowing_enable", coBool);
    def->label = L(" ");
    def->category = L(" ");
    def->tooltip = L(" ");
    def->mode = comSimple;
    def->set_default_value(new ConfigOptionBool(false));

    def = this->add("hollowing_min_thickness", coFloat);
    def->label = L(" ");
    def->category = L(" ");
    def->tooltip  = L(" ");
    def->sidetext = L(" ");
    def->min = 1;
    def->max = 10;
    def->mode = comSimple;
    def->set_default_value(new ConfigOptionFloat(3.));

    def = this->add("hollowing_quality", coFloat);
    def->label = L(" ");
    def->category = L(" ");
    def->tooltip  = L(" ");
    def->min = 0;
    def->max = 1;
    def->mode = comAdvanced;
    def->set_default_value(new ConfigOptionFloat(0.5));

    def = this->add("hollowing_closing_distance", coFloat);
    def->label = L(" ");
    def->category = L(" ");
    def->tooltip  = L(" ");
    def->sidetext = L("mm");
    def->min = 0;
    def->max = 10;
    def->mode = comAdvanced;
    def->set_default_value(new ConfigOptionFloat(2.0));

    def = this->add("material_print_speed", coEnum);
    def->label = L(" ");
    def->tooltip = L(" ");
    def->enum_keys_map = &ConfigOptionEnum<SLAMaterialSpeed>::get_enum_values();
    def->enum_values.push_back("slow");
    def->enum_values.push_back("fast");
    def->enum_labels.push_back(L(" "));
    def->enum_labels.push_back(L(" "));
    def->mode = comAdvanced;
    def->set_default_value(new ConfigOptionEnum<SLAMaterialSpeed>(slamsFast));
}

void PrintConfigDef::handle_legacy(t_config_option_key &opt_key, std::string &value)
{
    //BBS: handle legacy options
    if (opt_key == "enable_wipe_tower") {
        opt_key = "enable_prime_tower";
    } else if (opt_key == "wipe_tower_width") {
        opt_key = "prime_tower_width";
    } else if (opt_key == "bottom_solid_infill_flow_ratio") {
        opt_key = "initial_layer_flow_ratio";
    } else if (opt_key == "wiping_volume") {
        opt_key = "filament_prime_volume";
    }
    else if (opt_key == "prime_volume") {
        opt_key = "filament_prime_volume";
    }
    else if (opt_key == "wipe_tower_brim_width") {
        opt_key = "prime_tower_brim_width";
    } else if (opt_key == "tool_change_gcode") {
        opt_key = "change_filament_gcode";
    } else if (opt_key == "bridge_fan_speed") {
        opt_key = "overhang_fan_speed";
    } else if (opt_key == "infill_extruder") {
        opt_key = "sparse_infill_filament";
    }else if (opt_key == "solid_infill_extruder") {
        opt_key = "solid_infill_filament";
    }else if (opt_key == "perimeter_extruder") {
        opt_key = "wall_filament";
    } else if (opt_key == "support_material_extruder") {
        opt_key = "support_filament";
    } else if (opt_key == "support_material_interface_extruder") {
        opt_key = "support_interface_filament";
    } else if (opt_key == "support_material_angle") {
        opt_key = "support_angle";
    } else if (opt_key == "support_material_enforce_layers") {
        opt_key = "enforce_support_layers";
    } else if ((opt_key == "initial_layer_print_height"   ||
                opt_key == "initial_layer_speed"          ||
                opt_key == "internal_solid_infill_speed"  ||
                opt_key == "top_surface_speed"            ||
                opt_key == "support_interface_speed"      ||
                opt_key == "outer_wall_speed"             ||
                opt_key == "support_object_xy_distance")     && value.find("%") != std::string::npos) {
        //BBS: this is old profile in which value is expressed as percentage.
        //But now these key-value must be absolute value.
        //Reset to default value by erasing these key to avoid parsing error.
        opt_key = "";
    } else if (opt_key == "inherits_cummulative") {
        opt_key = "inherits_group";
    } else if (opt_key == "compatible_printers_condition_cummulative") {
        opt_key = "compatible_machine_expression_group";
    } else if (opt_key == "compatible_prints_condition_cummulative") {
        opt_key = "compatible_process_expression_group";
    } else if (opt_key == "cooling") {
        opt_key = "slow_down_for_layer_cooling";
    } else if (opt_key == "timelapse_no_toolhead") {
        opt_key = "timelapse_type";
    } else if (opt_key == "timelapse_type" && value == "2") {
        // old file "0" is None, "2" is Traditional
        // new file "0" is Traditional, erase "2"
        value = "0";
    } else if (opt_key == "support_type" && value == "normal") {
        value = "normal(manual)";
    } else if (opt_key == "support_type" && value == "tree") {
        value = "tree(manual)";
    } else if (opt_key == "support_type" && value == "hybrid(auto)") {
        value = "tree(auto)";
    } else if (opt_key == "support_base_pattern" && value == "none") {
        value = "hollow";
    } else if (opt_key == "infill_anchor") {
        opt_key = "sparse_infill_anchor";
    } else if (opt_key == "infill_anchor_max") {
        opt_key = "sparse_infill_anchor_max";
    } else if (opt_key == "different_settings_to_system") {
        std::string copy_value = value;
        copy_value.erase(std::remove(copy_value.begin(), copy_value.end(), '\"'), copy_value.end()); // remove '"' in string
        std::set<std::string> split_keys = SplitStringAndRemoveDuplicateElement(copy_value, ";");
        for (std::string split_key : split_keys) {
            std::string copy_key = split_key, copy_value = "";
            handle_legacy(copy_key, copy_value);
            if (copy_key != split_key) {
                ReplaceString(value, split_key, copy_key);
            }
        }
    } else if (opt_key == "overhang_fan_threshold" && value == "5%") {
        value = "10%";
    } else if( opt_key == "wall_infill_order" ) {
        if (value == "inner wall/outer wall/infill" || value == "infill/inner wall/outer wall") {
            opt_key = "wall_sequence";
            value = "inner wall/outer wall";
        } else if (value == "outer wall/inner wall/infill" || value == "infill/outer wall/inner wall") {
            opt_key = "wall_sequence";
            value = "outer wall/inner wall";
        } else if (value == "inner-outer-inner wall/infill") {
            opt_key = "wall_sequence";
            value = "inner-outer-inner wall";
        } else {
            opt_key = "wall_sequence";
        }
    } else if (opt_key == "nozzle_volume_type"
        || opt_key == "default_nozzle_volume_type"
        || opt_key == "printer_extruder_variant"
        || opt_key == "print_extruder_variant"
        || opt_key == "filament_extruder_variant"
        || opt_key == "extruder_variant_list") {
        ReplaceString(value, "Normal", "Standard");
        ReplaceString(value, "Big Traffic", "High Flow");
    }
    else if (opt_key == "extruder_type") {
        ReplaceString(value, "DirectDrive", "Direct Drive");
    }
    else if (opt_key == "ensure_vertical_shell_thickness") {
        auto kvmap=ConfigOptionEnum<EnsureVerticalThicknessLevel>::get_enum_names();
        // handle old values
        if (value == "1")
            value = ConfigOptionEnum<EnsureVerticalThicknessLevel>::get_enum_names()[EnsureVerticalThicknessLevel::evtEnabled];
        else if (value == "0")
            value = ConfigOptionEnum<EnsureVerticalThicknessLevel>::get_enum_names()[EnsureVerticalThicknessLevel::evtPartial];
    } else if (opt_key == "filament_map_mode") {
        if (value == "Auto") value = "Auto For Flush";
    }
    else if (opt_key == "filament_type"){
        std::vector<std::string> type_list;
        std::stringstream ss(value);
        std::string token;
        bool rebuild_value = false;
        while (std::getline(ss, token, ';')) {
            if (token.size() >= 2 && token.front() == '"' && token.back() == '"')
                token = token.substr(1, token.size() - 2);
            if (token == "ASA-Aero") {
                token = "ASA-AERO";
                rebuild_value = true;
            }
            type_list.emplace_back(token);
        }
        if (rebuild_value) {
            value.clear();
            for (size_t idx = 0; idx < type_list.size(); ++idx) {
                if (idx != 0)
                    value += ';';
                value += "\"" + type_list[idx] + "\"";
            }
        }
    }

    // Ignore the following obsolete configuration keys:
    static std::set<std::string> ignore = {
        "acceleration", "scale", "rotate", "duplicate", "duplicate_grid",
        "bed_size",
        "print_center", "g0", "wipe_tower_per_color_wipe"
#ifndef HAS_PRESSURE_EQUALIZER
        , "max_volumetric_extrusion_rate_slope_positive", "max_volumetric_extrusion_rate_slope_negative"
#endif /* HAS_PRESSURE_EQUALIZER */
        // BBS
        , "support_sharp_tails","support_remove_small_overhangs", "support_with_sheath",
        "tree_support_collision_resolution", "tree_support_with_infill",
        "tree_support_brim_width",
        "max_volumetric_speed", "max_print_speed",
        "support_closing_radius",
        "remove_freq_sweep", "remove_bed_leveling", "remove_extrusion_calibration",
        "support_transition_line_width", "support_transition_speed", "bed_temperature", "bed_temperature_initial_layer",
        "can_switch_nozzle_type", "can_add_auxiliary_fan", "extra_flush_volume", "spaghetti_detector", "adaptive_layer_height",
        "z_hop_type","nozzle_hrc","chamber_temperature","only_one_wall_top","bed_temperature_difference","long_retraction_when_cut",
        "retraction_distance_when_cut",
        "seam_slope_type","seam_slope_start_height","seam_slope_gap", "seam_slope_min_length",
        "prime_volume"
    };

    if (ignore.find(opt_key) != ignore.end()) {
        opt_key = "";
        return;
    }

    if (! print_config_def.has(opt_key)) {
        opt_key = "";
        return;
    }
}

const PrintConfigDef print_config_def;

//todo
std::set<std::string> print_options_with_variant = {
    "initial_layer_speed",
    "initial_layer_infill_speed",
    "outer_wall_speed",
    "inner_wall_speed",
    "small_perimeter_speed",  //coFloatsOrPercents
    "small_perimeter_threshold",
    "sparse_infill_speed",
    "internal_solid_infill_speed",
    "vertical_shell_speed",
    "top_surface_speed",
    "enable_overhang_speed", //coBools
    "overhang_1_4_speed",
    "overhang_2_4_speed",
    "overhang_3_4_speed",
    "overhang_4_4_speed",
    "overhang_totally_speed",
    "bridge_speed",
    "gap_infill_speed",
    "support_speed",
    "support_interface_speed",
    "travel_speed",
    "travel_speed_z",
    "default_acceleration",
    "travel_acceleration",
    "initial_layer_travel_acceleration",
    "initial_layer_acceleration",
    "outer_wall_acceleration",
    "inner_wall_acceleration",
    "sparse_infill_acceleration", //coFloatsOrPercents
    "top_surface_acceleration",
    "print_extruder_id", //coInts
    "print_extruder_variant" //coStrings
};

std::set<std::string> filament_options_with_variant = {
    "filament_flow_ratio",
    "filament_max_volumetric_speed",
    "filament_ramming_volumetric_speed",
    "filament_pre_cooling_temperature",
    "filament_ramming_travel_time",
    //"filament_extruder_id",
    "filament_extruder_variant",
    "filament_retraction_length",
    "filament_z_hop",
    "filament_z_hop_types",
    "filament_retraction_speed",
    "filament_deretraction_speed",
    "filament_retraction_minimum_travel",
    "filament_retract_when_changing_layer",
     "filament_wipe",
    //BBS
    "filament_wipe_distance",
    "filament_retract_before_wipe",
    "filament_long_retractions_when_cut",
    "filament_retraction_distances_when_cut",
    "long_retractions_when_ec",
    "retraction_distances_when_ec",
    "nozzle_temperature_initial_layer",
    "nozzle_temperature",
    "filament_flush_volumetric_speed",
    "filament_flush_temp"
};

// Parameters that are the same as the number of extruders
std::set<std::string> printer_extruder_options = {
    "extruder_type",
    "nozzle_diameter",
    "default_nozzle_volume_type",
    "extruder_printable_area",
    "extruder_printable_height",
    "min_layer_height",
    "max_layer_height"
};

std::set<std::string> printer_options_with_variant_1 = {
    "nozzle_volume",
    "retraction_length",
    "z_hop",
    "retract_lift_above",
    "retract_lift_below",
    "z_hop_types",
    "retraction_speed",
    "deretraction_speed",
    "retraction_minimum_travel",
    "retract_when_changing_layer",
    "wipe",
    "wipe_distance",
    "retract_before_wipe",
    "retract_length_toolchange",
    "retract_restart_extra",
    "retract_restart_extra_toolchange",
    "long_retractions_when_cut",
    "retraction_distances_when_cut",
    "nozzle_volume",
    "nozzle_type",
    "printer_extruder_id",
    "printer_extruder_variant",
    "hotend_cooling_rate",
    "hotend_heating_rate"
};

//options with silient mode
std::set<std::string> printer_options_with_variant_2 = {
    "machine_max_acceleration_x",
    "machine_max_acceleration_y",
    "machine_max_acceleration_z",
    "machine_max_acceleration_e",
    "machine_max_acceleration_extruding",
    "machine_max_acceleration_retracting",
    "machine_max_acceleration_travel",
    "machine_max_speed_x",
    "machine_max_speed_y",
    "machine_max_speed_z",
    "machine_max_speed_e",
    "machine_max_jerk_x",
    "machine_max_jerk_y",
    "machine_max_jerk_z",
    "machine_max_jerk_e"
};

std::set<std::string> empty_options;

DynamicPrintConfig DynamicPrintConfig::full_print_config()
{
	return DynamicPrintConfig((const PrintRegionConfig&)FullPrintConfig::defaults());
}

DynamicPrintConfig::DynamicPrintConfig(const StaticPrintConfig& rhs) : DynamicConfig(rhs, rhs.keys_ref())
{
}

DynamicPrintConfig* DynamicPrintConfig::new_from_defaults_keys(const std::vector<std::string> &keys)
{
    auto *out = new DynamicPrintConfig();
    out->apply_only(FullPrintConfig::defaults(), keys);
    return out;
}

double min_object_distance(const ConfigBase &cfg)
{
    const ConfigOptionEnum<PrinterTechnology> *opt_printer_technology = cfg.option<ConfigOptionEnum<PrinterTechnology>>("printer_technology");
    auto printer_technology = opt_printer_technology ? opt_printer_technology->value : ptUnknown;

    double ret = 0.;

    if (printer_technology == ptSLA)
        ret = 6.;
    else {
        //BBS: duplicate_distance seam to be useless
        constexpr double duplicate_distance = 6.;
        auto ecr_opt = cfg.option<ConfigOptionFloat>("extruder_clearance_max_radius");
        auto co_opt  = cfg.option<ConfigOptionEnum<PrintSequence>>("print_sequence");

        if (!ecr_opt || !co_opt)
            ret = 0.;
        else {
            // min object distance is max(duplicate_distance, clearance_radius)
            ret = ((co_opt->value == PrintSequence::ByObject) && ecr_opt->value > duplicate_distance) ?
                      ecr_opt->value : duplicate_distance;
        }
    }

    return ret;
}

void DynamicPrintConfig::normalize_fdm(int used_filaments)
{
    if (this->has("extruder")) {
        int extruder = this->option("extruder")->getInt();
        this->erase("extruder");
        if (extruder != 0) {
            if (!this->has("sparse_infill_filament"))
                this->option("sparse_infill_filament", true)->setInt(extruder);
            if (!this->has("wall_filament"))
                this->option("wall_filament", true)->setInt(extruder);
            // Don't propagate the current extruder to support.
            // For non-soluble supports, the default "0" extruder means to use the active extruder,
            // for soluble supports one certainly does not want to set the extruder to non-soluble.
            // if (!this->has("support_filament"))
            //     this->option("support_filament", true)->setInt(extruder);
            // if (!this->has("support_interface_filament"))
            //     this->option("support_interface_filament", true)->setInt(extruder);
        }
    }

    if (!this->has("solid_infill_filament") && this->has("sparse_infill_filament"))
        this->option("solid_infill_filament", true)->setInt(this->option("sparse_infill_filament")->getInt());

    if (this->has("spiral_mode") && this->opt<ConfigOptionBool>("spiral_mode", true)->value) {
        {
            // this should be actually done only on the spiral layers instead of all
            auto* opt = this->opt<ConfigOptionBoolsNullable>("retract_when_changing_layer", true);
            opt->values.assign(opt->values.size(), false);  // set all values to false
            // Disable retract on layer change also for filament overrides.
            auto* opt_n = this->opt<ConfigOptionBoolsNullable>("filament_retract_when_changing_layer", true);
            opt_n->values.assign(opt_n->values.size(), false);  // Set all values to false.
        }
        {
            this->opt<ConfigOptionInt>("wall_loops", true)->value       = 1;
            this->opt<ConfigOptionInt>("top_shell_layers", true)->value = 0;
            this->opt<ConfigOptionPercent>("sparse_infill_density", true)->value = 0;
        }
    }

    if (auto *opt_gcode_resolution = this->opt<ConfigOptionFloat>("resolution", false); opt_gcode_resolution)
        // Resolution will be above 1um.
        opt_gcode_resolution->value = std::max(opt_gcode_resolution->value, 0.001);

    // BBS
    ConfigOptionBool* ept_opt = this->option<ConfigOptionBool>("enable_prime_tower");
    if (used_filaments > 0 && ept_opt != nullptr) {
        ConfigOptionBool* islh_opt = this->option<ConfigOptionBool>("independent_support_layer_height", true);
        //ConfigOptionBool* alh_opt = this->option<ConfigOptionBool>("adaptive_layer_height");
        ConfigOptionEnum<PrintSequence>* ps_opt = this->option<ConfigOptionEnum<PrintSequence>>("print_sequence");

        ConfigOptionEnum<TimelapseType>* timelapse_opt = this->option<ConfigOptionEnum<TimelapseType>>("timelapse_type");
        bool is_smooth_timelapse = timelapse_opt != nullptr && timelapse_opt->value == TimelapseType::tlSmooth;
        if (!is_smooth_timelapse && (used_filaments == 1 || ps_opt->value == PrintSequence::ByObject)) {
            ept_opt->value = false;
        }

        if (ept_opt->value) {
            if (islh_opt)
                islh_opt->value = false;
            //if (alh_opt)
            //    alh_opt->value = false;
        }
        /* BBS: MusangKing - not sure if this is still valid, just comment it out cause "Independent support layer height" is re-opened.
        else {
            if (islh_opt)
                islh_opt->value = true;
        }
        */
    }
}

//BBS:divide normalize_fdm to 2 steps and call them one by one in Print::Apply
void DynamicPrintConfig::normalize_fdm_1()
{
    if (this->has("extruder")) {
        int extruder = this->option("extruder")->getInt();
        this->erase("extruder");
        if (extruder != 0) {
            if (!this->has("sparse_infill_filament"))
                this->option("sparse_infill_filament", true)->setInt(extruder);
            if (!this->has("wall_filament"))
                this->option("wall_filament", true)->setInt(extruder);
            // Don't propagate the current extruder to support.
            // For non-soluble supports, the default "0" extruder means to use the active extruder,
            // for soluble supports one certainly does not want to set the extruder to non-soluble.
            // if (!this->has("support_filament"))
            //     this->option("support_filament", true)->setInt(extruder);
            // if (!this->has("support_interface_filament"))
            //     this->option("support_interface_filament", true)->setInt(extruder);
        }
    }

    if (!this->has("solid_infill_filament") && this->has("sparse_infill_filament"))
        this->option("solid_infill_filament", true)->setInt(this->option("sparse_infill_filament")->getInt());

    if (this->has("spiral_mode") && this->opt<ConfigOptionBool>("spiral_mode", true)->value) {
        {
            // this should be actually done only on the spiral layers instead of all
            auto* opt = this->opt<ConfigOptionBoolsNullable>("retract_when_changing_layer", true);
            opt->values.assign(opt->values.size(), false);  // set all values to false
            // Disable retract on layer change also for filament overrides.
            auto* opt_n = this->opt<ConfigOptionBoolsNullable>("filament_retract_when_changing_layer", true);
            opt_n->values.assign(opt_n->values.size(), false);  // Set all values to false.
        }
        {
            this->opt<ConfigOptionInt>("wall_loops", true)->value       = 1;
            this->opt<ConfigOptionInt>("top_shell_layers", true)->value = 0;
            this->opt<ConfigOptionPercent>("sparse_infill_density", true)->value = 0;
        }
    }

    if (auto *opt_gcode_resolution = this->opt<ConfigOptionFloat>("resolution", false); opt_gcode_resolution)
        // Resolution will be above 1um.
        opt_gcode_resolution->value = std::max(opt_gcode_resolution->value, 0.001);

    return;
}

t_config_option_keys DynamicPrintConfig::normalize_fdm_2(int num_objects, int used_filaments)
{
    t_config_option_keys changed_keys;
    ConfigOptionBool* ept_opt = this->option<ConfigOptionBool>("enable_prime_tower");
    if (used_filaments > 0 && ept_opt != nullptr) {
        ConfigOptionBool* islh_opt = this->option<ConfigOptionBool>("independent_support_layer_height", true);
        //ConfigOptionBool* alh_opt = this->option<ConfigOptionBool>("adaptive_layer_height");
        ConfigOptionEnum<PrintSequence>* ps_opt = this->option<ConfigOptionEnum<PrintSequence>>("print_sequence");

        ConfigOptionEnum<TimelapseType>* timelapse_opt = this->option<ConfigOptionEnum<TimelapseType>>("timelapse_type");
        bool is_smooth_timelapse = timelapse_opt != nullptr && timelapse_opt->value == TimelapseType::tlSmooth;
        if (!is_smooth_timelapse && (used_filaments == 1 || (ps_opt->value == PrintSequence::ByObject && num_objects > 1))) {
            if (ept_opt->value) {
                ept_opt->value = false;
                changed_keys.push_back("enable_prime_tower");
            }
            //ept_opt->value = false;
        }

        if (ept_opt->value) {
            if (islh_opt) {
                if (islh_opt->value) {
                    islh_opt->value = false;
                    changed_keys.push_back("independent_support_layer_height");
                }
                //islh_opt->value = false;
            }
            //if (alh_opt) {
            //    if (alh_opt->value) {
            //        alh_opt->value = false;
            //        changed_keys.push_back("adaptive_layer_height");
            //    }
            //    //alh_opt->value = false;
            //}
        }
        /* BBS：MusangKing - use "global->support->Independent support layer height" widget to replace previous assignment
        else {
            if (islh_opt) {
                if (!islh_opt->value) {
                    islh_opt->value = true;
                    changed_keys.push_back("independent_support_layer_height");
                }
                //islh_opt->value = true;
            }
        }
        */
    }

    return changed_keys;
}

void  handle_legacy_sla(DynamicPrintConfig &config)
{
    for (std::string corr : {"relative_correction", "material_correction"}) {
        if (config.has(corr)) {
            if (std::string corr_x = corr + "_x"; !config.has(corr_x)) {
                auto* opt = config.opt<ConfigOptionFloat>(corr_x, true);
                opt->value = config.opt<ConfigOptionFloats>(corr)->values[0];
            }

            if (std::string corr_y = corr + "_y"; !config.has(corr_y)) {
                auto* opt = config.opt<ConfigOptionFloat>(corr_y, true);
                opt->value = config.opt<ConfigOptionFloats>(corr)->values[0];
            }

            if (std::string corr_z = corr + "_z"; !config.has(corr_z)) {
                auto* opt = config.opt<ConfigOptionFloat>(corr_z, true);
                opt->value = config.opt<ConfigOptionFloats>(corr)->values[1];
            }
        }
    }
}

size_t DynamicPrintConfig::get_parameter_size(const std::string& param_name, size_t extruder_nums)
{
    if (extruder_nums > 1) {
        size_t volume_type_size = 2;
        auto   nozzle_volume_type_opt = dynamic_cast<const ConfigOptionEnumsGeneric *>(this->option("nozzle_volume_type"));
        if (nozzle_volume_type_opt) {
            volume_type_size = nozzle_volume_type_opt->values.size();
        }
        if (printer_options_with_variant_1.count(param_name) > 0) {
            return extruder_nums * volume_type_size;
        }
        else if (printer_options_with_variant_2.count(param_name) > 0) {
            return extruder_nums * volume_type_size * 2;
        }
        else if (filament_options_with_variant.count(param_name) > 0) {
            return extruder_nums * volume_type_size;
        }
        else if (print_options_with_variant.count(param_name) > 0) {
            return extruder_nums * volume_type_size;
        }
        else {
            return extruder_nums;
        }
    }
    return extruder_nums;
}

void DynamicPrintConfig::set_num_extruders(unsigned int num_extruders)
{
    const auto &defaults = FullPrintConfig::defaults();
    for (const std::string &key : print_config_def.extruder_option_keys()) {
        if (key == "default_filament_profile")
            // Don't resize this field, as it is presented to the user at the "Dependencies" page of the Printer profile and we don't want to present
            // empty fields there, if not defined by the system profile.
            continue;
        auto *opt = this->option(key, false);
        assert(opt != nullptr);
        assert(opt->is_vector());
        if (opt != nullptr && opt->is_vector()) {
            static_cast<ConfigOptionVectorBase*>(opt)->resize(get_parameter_size(key, num_extruders), defaults.option(key));
        }
    }
}

// BBS
void DynamicPrintConfig::set_num_filaments(unsigned int num_filaments)
{
    const auto& defaults = FullPrintConfig::defaults();
    for (const std::string& key : print_config_def.filament_option_keys()) {
        if (key == "default_filament_profile")
            // Don't resize this field, as it is presented to the user at the "Dependencies" page of the Printer profile and we don't want to present
            // empty fields there, if not defined by the system profile.
            continue;
        auto* opt = this->option(key, false);
        assert(opt != nullptr);
        assert(opt->is_vector());
        if (opt != nullptr && opt->is_vector())
            static_cast<ConfigOptionVectorBase*>(opt)->resize(num_filaments, defaults.option(key));
    }
}

//BBS: pass map to recording all invalid valies
std::map<std::string, std::string> DynamicPrintConfig::validate(bool under_cli)
{
    // Full print config is initialized from the defaults.
    const ConfigOption *opt = this->option("printer_technology", false);
    auto printer_technology = (opt == nullptr) ? ptFFF : static_cast<PrinterTechnology>(dynamic_cast<const ConfigOptionEnumGeneric*>(opt)->value);
    switch (printer_technology) {
    case ptFFF:
    {
        FullPrintConfig fpc;
        fpc.apply(*this, true);
        // Verify this print options through the FullPrintConfig.
        return Slic3r::validate(fpc, under_cli);
    }
    default:
        //FIXME no validation on SLA data?
        return std::map<std::string, std::string>();
    }
}

std::string DynamicPrintConfig::get_filament_type(std::string &displayed_filament_type, int id)
{
    auto* filament_id = dynamic_cast<const ConfigOptionStrings*>(this->option("filament_id"));
    auto* filament_type = dynamic_cast<const ConfigOptionStrings*>(this->option("filament_type"));
    auto* filament_is_support = dynamic_cast<const ConfigOptionBools*>(this->option("filament_is_support"));

    if (!filament_type)
        return "";

    if (!filament_is_support) {
        if (filament_type) {
            displayed_filament_type = filament_type->get_at(id);
            return filament_type->get_at(id);
        }
        else {
            displayed_filament_type = "";
            return "";
        }
    }
    else {
        bool is_support = filament_is_support ? filament_is_support->get_at(id) : false;
        if (is_support) {
            if (filament_id) {
                if (filament_id->get_at(id) == "GFS00") {
                    displayed_filament_type = "Sup.PLA";
                    return "PLA-S";
                }
                else if (filament_id->get_at(id) == "GFS01") {
                    displayed_filament_type = "Sup.PA";
                    return "PA-S";
                }
                else {
                    if (filament_type->get_at(id) == "PLA") {
                        displayed_filament_type = "Sup.PLA";
                        return "PLA-S";
                    }
                    else if (filament_type->get_at(id) == "PA") {
                        displayed_filament_type = "Sup.PA";
                        return "PA-S";
                    }
                    else {
                        displayed_filament_type = filament_type->get_at(id);
                        return filament_type->get_at(id);
                    }
                }
            }
            else {
                if (filament_type->get_at(id) == "PLA") {
                    displayed_filament_type = "Sup.PLA";
                    return "PLA-S";
                } else if (filament_type->get_at(id) == "PA") {
                    displayed_filament_type = "Sup.PA";
                    return "PA-S";
                }
                else if (filament_type->get_at(id) == "ABS") {
                    displayed_filament_type = "Sup.ABS";
                    return "ABS-S";
                }
                else {
                    displayed_filament_type = filament_type->get_at(id);
                    return filament_type->get_at(id);
                }
            }
        }
        else {
            displayed_filament_type = filament_type->get_at(id);
            return filament_type->get_at(id);
        }
    }
    return "PLA";
}

bool DynamicPrintConfig::is_using_different_extruders()
{
    bool ret = false;

    auto nozzle_diameters_opt = dynamic_cast<const ConfigOptionFloatsNullable*>(this->option("nozzle_diameter"));
    if (nozzle_diameters_opt != nullptr) {
        int size = nozzle_diameters_opt->size();
        if (size > 1) {
            auto extruder_type_opt = dynamic_cast<const ConfigOptionEnumsGeneric*>(this->option("extruder_type"));
            auto nozzle_volume_type_opt = dynamic_cast<const ConfigOptionEnumsGeneric*>(this->option("nozzle_volume_type"));
            if (extruder_type_opt && nozzle_volume_type_opt) {
                ExtruderType extruder_type = (ExtruderType)(extruder_type_opt->get_at(0));
                NozzleVolumeType nozzle_volume_type = (NozzleVolumeType)(nozzle_volume_type_opt->get_at(0));
                for (int index = 1; index < size; index++)
                {
                    ExtruderType extruder_type_1 = (ExtruderType)(extruder_type_opt->get_at(index));
                    NozzleVolumeType nozzle_volume_type_1 = (NozzleVolumeType)(nozzle_volume_type_opt->get_at(index));
                    if ((extruder_type_1 != extruder_type) || (nozzle_volume_type_1 != nozzle_volume_type)) {
                        ret = true;
                        break;
                    }
                }
            }
        }
    }

    return ret;
}

bool DynamicPrintConfig::support_different_extruders(int& extruder_count)
{
    std::set<std::string> variant_set;

    auto nozzle_diameters_opt = dynamic_cast<const ConfigOptionFloatsNullable*>(this->option("nozzle_diameter"));
    if (nozzle_diameters_opt != nullptr) {
        int size = nozzle_diameters_opt->size();
        extruder_count = size;
        auto extruder_variant_opt = dynamic_cast<const ConfigOptionStrings*>(this->option("extruder_variant_list"));
        if (extruder_variant_opt != nullptr) {
            for (int index = 0; index < size; index++) {
                std::string variant = extruder_variant_opt->get_at(index);
                std::vector<std::string> variants_list;
                boost::split(variants_list, variant, boost::is_any_of(","), boost::token_compress_on);
                if (!variants_list.empty())
                    variant_set.insert(variants_list.begin(), variants_list.end());
            }
        }
    }

    return (variant_set.size() > 1);
}

int DynamicPrintConfig::get_index_for_extruder(int extruder_or_filament_id, std::string id_name, ExtruderType extruder_type, NozzleVolumeType nozzle_volume_type, std::string variant_name, unsigned int stride) const
{
    int ret = -1;

    auto variant_opt = dynamic_cast<const ConfigOptionStrings*>(this->option(variant_name));
    const ConfigOptionInts* id_opt = id_name.empty()?nullptr: dynamic_cast<const ConfigOptionInts*>(this->option(id_name));
    if (variant_opt != nullptr) {
        int v_size = variant_opt->values.size();
        //int i_size = id_opt->values.size();
        std::string extruder_variant = get_extruder_variant_string(extruder_type, nozzle_volume_type);
        for (int index = 0; index < v_size; index++)
        {
            const std::string variant = variant_opt->get_at(index);
            if (extruder_variant == variant) {
                if (id_opt) {
                    const int id = id_opt->get_at(index);
                    if (id == extruder_or_filament_id) {
                        ret = index * stride;
                        break;
                    }
                }
                else {
                    ret = index * stride;
                    break;
                }

            }
        }
    }
    return ret;
}

//only used for cli
//update values in single extruder process config to values in multi-extruder process
//limit the new values
int DynamicPrintConfig::update_values_from_single_to_multi(DynamicPrintConfig& multi_config, std::set<std::string>& key_set, std::string id_name, std::string variant_name)
{
    auto print_variant_opt = dynamic_cast<const ConfigOptionStrings*>(multi_config.option(variant_name));
    if (!print_variant_opt) {
        BOOST_LOG_TRIVIAL(error) << boost::format("%1%:%2%, can not get %3% from config")%__FUNCTION__ %__LINE__ % variant_name;
        return -1;
    }
    int variant_count = print_variant_opt->size();

    const ConfigDef  *config_def     = this->def();
    if (!config_def) {
        BOOST_LOG_TRIVIAL(error) << __FUNCTION__ << boost::format(", Line %1%: can not find config define")%__LINE__;
        return -1;
    }
    for (auto& key: key_set)
    {
        const ConfigOptionDef *optdef  = config_def->get(key);
        if (!optdef) {
            BOOST_LOG_TRIVIAL(warning) << __FUNCTION__ << boost::format(", Line %1%: can not find opt define for %2%")%__LINE__%key;
            continue;
        }
        switch (optdef->type) {
            case coStrings:
            {
                ConfigOptionStrings* src_opt = multi_config.option<ConfigOptionStrings>(key);
                if (src_opt) {
                    ConfigOptionStrings* opt = this->option<ConfigOptionStrings>(key, true);

                    opt->values = src_opt->values;
                }
                break;
            }
            case coInts:
            {
                ConfigOptionInts* src_opt = multi_config.option<ConfigOptionInts>(key);
                if (src_opt) {
                    ConfigOptionInts* opt = this->option<ConfigOptionInts>(key, true);

                    opt->values = src_opt->values;
                }
                break;
            }
            case coFloats:
            {
                ConfigOptionFloats * src_opt = multi_config.option<ConfigOptionFloats>(key);
                if (src_opt) {
                    ConfigOptionFloats * opt = this->option<ConfigOptionFloats>(key, true);

                    assert(variant_count == src_opt->size());
                    opt->resize(variant_count, opt);

                    for (int index = 0; index < variant_count; index++)
                    {
                        if (opt->values[index] > src_opt->values[index])
                            opt->values[index] = src_opt->values[index];
                    }
                }
                break;
            }
            case coFloatsOrPercents:
            {
                ConfigOptionFloatsOrPercents * src_opt = multi_config.option<ConfigOptionFloatsOrPercents>(key);
                if (src_opt) {
                    ConfigOptionFloatsOrPercents * opt = this->option<ConfigOptionFloatsOrPercents>(key, true);

                    assert(variant_count == src_opt->size());
                    opt->resize(variant_count, opt);

                    for (int index = 0; index < variant_count; index++)
                    {
                        if (opt->values[index].value > src_opt->values[index].value)
                            opt->values[index] = src_opt->values[index];
                    }
                }
                break;
            }
            case coBools:
            {
                ConfigOptionBools * src_opt = multi_config.option<ConfigOptionBools>(key);
                if (src_opt)
                {
                    ConfigOptionBools * opt = this->option<ConfigOptionBools>(key, true);

                    assert(variant_count == src_opt->size());
                    opt->resize(variant_count, opt);
                }

                break;
            }
            default:
                BOOST_LOG_TRIVIAL(warning) << __FUNCTION__ << boost::format(", Line %1%: unsupported option type for %2%")%__LINE__%key;
                break;
        }
    }

    return 0;
}

//used for object/region config
//duplicate single to multiple
int DynamicPrintConfig::update_values_from_single_to_multi_2(DynamicPrintConfig& multi_config, std::set<std::string>& key_set)
{
    const ConfigDef  *config_def     = this->def();
    if (!config_def) {
        BOOST_LOG_TRIVIAL(error) << __FUNCTION__ << boost::format(", Line %1%: can not find config define")%__LINE__;
        return -1;
    }

    t_config_option_keys keys = this->keys();
    for (auto& key: keys)
    {
        if (key_set.find(key) == key_set.end())
            continue;

        const ConfigOptionDef *optdef  = config_def->get(key);
        if (!optdef) {
            BOOST_LOG_TRIVIAL(warning) << __FUNCTION__ << boost::format(", Line %1%: can not find opt define for %2%")%__LINE__%key;
            continue;
        }
        switch (optdef->type) {
            case coFloats:
            {
                ConfigOptionFloatsNullable * opt = this->option<ConfigOptionFloatsNullable>(key);
                ConfigOptionFloatsNullable* src_opt = multi_config.option<ConfigOptionFloatsNullable>(key);

                if (src_opt && !opt->is_nil(0))
                    opt->values.resize(src_opt->size(), opt->values[0]);
                break;
            }
            case coFloatsOrPercents:
            {
                ConfigOptionFloatsOrPercentsNullable* opt = this->option<ConfigOptionFloatsOrPercentsNullable>(key);
                ConfigOptionFloatsOrPercentsNullable* src_opt = multi_config.option<ConfigOptionFloatsOrPercentsNullable>(key);

                if (src_opt &&!opt->is_nil(0))
                    opt->values.resize(src_opt->size(), opt->values[0]);
                break;
            }
            case coBools:
            {
                ConfigOptionBoolsNullable* opt = this->option<ConfigOptionBoolsNullable>(key);
                ConfigOptionBoolsNullable* src_opt = multi_config.option<ConfigOptionBoolsNullable>(key);

                if (src_opt &&!opt->is_nil(0))
                    opt->values.resize(src_opt->size(), opt->values[0]);

                break;
            }
            default:
                BOOST_LOG_TRIVIAL(warning) << __FUNCTION__ << boost::format(", Line %1%: unsupported option type for %2%")%__LINE__%key;
                break;
        }
    }

    return 0;
}

int DynamicPrintConfig::update_values_from_multi_to_single(DynamicPrintConfig& single_config, std::set<std::string>& key_set, std::string id_name, std::string variant_name, std::vector<std::string>& extruder_variants)
{
    int extruder_count = extruder_variants.size();
    std::vector<int> extruder_index(extruder_count, -1);

    auto print_variant_opt = dynamic_cast<const ConfigOptionStrings*>(this->option(variant_name));
    if (!print_variant_opt) {
        BOOST_LOG_TRIVIAL(error) << boost::format("%1%:%2%, can not get %3% from config")%__FUNCTION__ %__LINE__ % variant_name;
        return -1;
    }
    int variant_count = print_variant_opt->size();

    auto print_id_opt = dynamic_cast<const ConfigOptionInts*>(this->option(id_name));
    if (!print_id_opt) {
        BOOST_LOG_TRIVIAL(error) << boost::format("%1%:%2%, can not get %3% from config")%__FUNCTION__ %__LINE__ % id_name;
        return -1;
    }

    for (int i = 0; i < extruder_count; i++)
    {
        for (int j = 0; j < variant_count; j++)
        {
            if ((i+1 == print_id_opt->values[j]) && (extruder_variants[i] == print_variant_opt->values[j])) {
                extruder_index[i] = j;
                break;
            }
        }
    }

    const ConfigDef* config_def = this->def();
    if (!config_def) {
        BOOST_LOG_TRIVIAL(error) << __FUNCTION__ << boost::format(", Line %1%: can not find config define") % __LINE__;
        return -1;
    }
    for (auto& key : key_set)
    {
        const ConfigOptionDef* optdef = config_def->get(key);
        if (!optdef) {
            BOOST_LOG_TRIVIAL(warning) << __FUNCTION__ << boost::format(", Line %1%: can not find opt define for %2%") % __LINE__ % key;
            continue;
        }
        switch (optdef->type) {
        case coStrings:
        {
            ConfigOptionStrings* src_opt = single_config.option<ConfigOptionStrings>(key);
            if (src_opt) {
                ConfigOptionStrings* opt = this->option<ConfigOptionStrings>(key, true);

                //assert(variant_count == opt->size());
                opt->values = src_opt->values;
            }
            break;
        }
        case coInts:
        {
            ConfigOptionInts* src_opt = single_config.option<ConfigOptionInts>(key);
            if (src_opt) {
                ConfigOptionInts* opt = this->option<ConfigOptionInts>(key, true);

                //assert(variant_count == opt->size());
                opt->values = src_opt->values;
            }
            break;
        }
        case coFloats:
        {
            ConfigOptionFloats* src_opt = single_config.option<ConfigOptionFloats>(key);
            if (src_opt) {
                ConfigOptionFloats* opt = this->option<ConfigOptionFloats>(key, true);

                std::vector<double> old_values = opt->values;
                int old_count = old_values.size();

                //assert(variant_count == opt->size());
                opt->values = src_opt->values;

                for (int i = 0; i < extruder_count; i++)
                {
                    assert(extruder_index[i] != -1);
                    if ((old_count > extruder_index[i]) && (old_values[extruder_index[i]] < opt->values[0]))
                        opt->values[0] = old_values[extruder_index[i]];
                }
            }
            break;
        }
        case coFloatsOrPercents:
        {
            ConfigOptionFloatsOrPercents* src_opt = single_config.option<ConfigOptionFloatsOrPercents>(key);
            if (src_opt) {
                ConfigOptionFloatsOrPercents* opt = this->option<ConfigOptionFloatsOrPercents>(key, true);

                std::vector<FloatOrPercent> old_values = opt->values;
                int old_count = old_values.size();

                //assert(variant_count == opt->size());
                opt->values = src_opt->values;

                for (int i = 0; i < extruder_count; i++)
                {
                    assert(extruder_index[i] != -1);
                    if ((old_count > extruder_index[i]) && (old_values[extruder_index[i]] < opt->values[0]))
                        opt->values[0] = old_values[extruder_index[i]];
                }
            }
            break;
        }
        case coBools:
        {
            ConfigOptionBools* src_opt = single_config.option<ConfigOptionBools>(key);
            if (src_opt) {
                ConfigOptionBools* opt = this->option<ConfigOptionBools>(key, true);

                //assert(variant_count == opt->size());
                opt->values = src_opt->values;
            }

            break;
        }
        default:
            BOOST_LOG_TRIVIAL(warning) << __FUNCTION__ << boost::format(", Line %1%: unsupported option type for %2%") % __LINE__ % key;
            break;
        }
    }

    return 0;
}

//used for object/region config
//use the smallest of multiple to single
int DynamicPrintConfig::update_values_from_multi_to_single_2(std::set<std::string>& key_set)
{
    const ConfigDef  *config_def     = this->def();
    if (!config_def) {
        BOOST_LOG_TRIVIAL(error) << __FUNCTION__ << boost::format(", Line %1%: can not find config define")%__LINE__;
        return -1;
    }

    t_config_option_keys keys = this->keys();
    for (auto& key: keys)
    {
        if (key_set.find(key) == key_set.end())
            continue;

        const ConfigOptionDef *optdef  = config_def->get(key);
        if (!optdef) {
            BOOST_LOG_TRIVIAL(warning) << __FUNCTION__ << boost::format(", Line %1%: can not find opt define for %2%")%__LINE__%key;
            continue;
        }
        switch (optdef->type) {
            case coFloats:
            {
                ConfigOptionFloatsNullable* opt = this->option<ConfigOptionFloatsNullable>(key);
                double min = 9999.0;
                bool has_value = false;

                for (int index = 0; index < opt->values.size(); index++)
                {
                    if (!opt->is_nil(index) && (opt->values[index] < min)) {
                        min = opt->values[index];
                        has_value = true;
                    }
                }

                opt->values.erase(opt->values.begin() + 1, opt->values.end());
                if (has_value)
                    opt->values[0] = min;
                break;
            }
            case coFloatsOrPercents:
            {
                ConfigOptionFloatsOrPercentsNullable * opt = this->option<ConfigOptionFloatsOrPercentsNullable>(key);
                FloatOrPercent min(9999.f, true);
                bool has_value = false;

                for (int index = 0; index < opt->values.size(); index++)
                {
                    if (!opt->is_nil(index) && (opt->values[index].value < min.value)) {
                        min = opt->values[index];
                        has_value = true;
                    }
                }

                opt->values.erase(opt->values.begin() + 1, opt->values.end());
                if (has_value)
                    opt->values[0] = min;
                break;
            }
            case coBools:
            {
                ConfigOptionBoolsNullable* opt = this->option<ConfigOptionBoolsNullable>(key);

                bool min, has_value = false;
                for (int index = 0; index < opt->values.size(); index++)
                {
                    if (!opt->is_nil(index)) {
                        min = opt->values[index];
                        has_value = true;
                        break;
                    }
                }

                opt->values.erase(opt->values.begin() + 1, opt->values.end());
                if (has_value)
                    opt->values[0] = min;
                break;
            }
            default:
                BOOST_LOG_TRIVIAL(warning) << __FUNCTION__ << boost::format(", Line %1%: unsupported option type for %2%")%__LINE__%key;
                break;
        }
    }

    return 0;
}

std::string
DynamicPrintConfig::get_filament_vendor() const
{
    const ConfigOptionStrings* opt = dynamic_cast<const ConfigOptionStrings*> (option("filament_vendor"));
    if (opt && !opt->values.empty())
    {
        return opt->values[0];
    }

    return std::string();
}


std::string
DynamicPrintConfig::get_filament_type() const
{
    const ConfigOptionStrings* opt = dynamic_cast<const ConfigOptionStrings*> (option("filament_type"));
    if (opt && !opt->values.empty())
    {
        return opt->values[0];
    }

    return std::string();
}

std::vector<int> DynamicPrintConfig::update_values_to_printer_extruders(DynamicPrintConfig& printer_config, std::set<std::string>& key_set, std::string id_name, std::string variant_name, unsigned int stride, unsigned int extruder_id)
{
    int extruder_count;
    bool different_extruder = printer_config.support_different_extruders(extruder_count);
    std::vector<int> variant_index;

    if ((extruder_count > 1) || different_extruder)
    {
        BOOST_LOG_TRIVIAL(info) << __FUNCTION__ << boost::format(", Line %1%: different extruders processing")%__LINE__;
        //apply process settings
        //auto opt_nozzle_diameters = this->option<ConfigOptionFloats>("nozzle_diameter");
        //int extruder_count = opt_nozzle_diameters->size();
        auto opt_extruder_type = dynamic_cast<const ConfigOptionEnumsGeneric*>(printer_config.option("extruder_type"));
        auto opt_nozzle_volume_type = dynamic_cast<const ConfigOptionEnumsGeneric*>(printer_config.option("nozzle_volume_type"));


        if (extruder_id > 0 && extruder_id <= static_cast<unsigned> (extruder_count)) {
            variant_index.resize(1);
            ExtruderType extruder_type = (ExtruderType)(opt_extruder_type->get_at(extruder_id - 1));
            NozzleVolumeType nozzle_volume_type = (NozzleVolumeType)(opt_nozzle_volume_type->get_at(extruder_id - 1));

            //variant index
            variant_index[0] = get_index_for_extruder(extruder_id, id_name, extruder_type, nozzle_volume_type, variant_name);

            if (variant_index[0] < 0) {
                BOOST_LOG_TRIVIAL(error) << __FUNCTION__ << boost::format(", Line %1%: could not found extruder_type %2%, nozzle_volume_type %3%, for filament")
                    % __LINE__ % s_keys_names_ExtruderType[extruder_type] % s_keys_names_NozzleVolumeType[nozzle_volume_type];
                assert(false);
            }

            extruder_count = 1;
        }
        else {
            variant_index.resize(extruder_count);

            for (int e_index = 0; e_index < extruder_count; e_index++)
            {
                ExtruderType extruder_type = (ExtruderType)(opt_extruder_type->get_at(e_index));
                NozzleVolumeType nozzle_volume_type = (NozzleVolumeType)(opt_nozzle_volume_type->get_at(e_index));

                //variant index
                variant_index[e_index] = get_index_for_extruder(e_index+1, id_name, extruder_type, nozzle_volume_type, variant_name);
                if (variant_index[e_index] < 0) {
                    BOOST_LOG_TRIVIAL(error) << __FUNCTION__ << boost::format(", Line %1%: could not found extruder_type %2%, nozzle_volume_type %3%, extruder_index %4%")
                        %__LINE__ %s_keys_names_ExtruderType[extruder_type] % s_keys_names_NozzleVolumeType[nozzle_volume_type] % (e_index+1);
                    assert(false);
                    //for some updates happens in a invalid state(caused by popup window)
                    //we need to avoid crash
                    variant_index[e_index] = 0;
                }
            }
        }

        const ConfigDef       *config_def     = this->def();
        if (!config_def) {
            BOOST_LOG_TRIVIAL(error) << __FUNCTION__ << boost::format(", Line %1%: can not find config define")%__LINE__;
            return variant_index;
        }
        for (auto& key: key_set)
        {
            const ConfigOptionDef *optdef  = config_def->get(key);
            if (!optdef) {
                BOOST_LOG_TRIVIAL(warning) << __FUNCTION__ << boost::format(", Line %1%: can not find opt define for %2%")%__LINE__%key;
                continue;
            }
            switch (optdef->type) {
                case coStrings:
                {
                    ConfigOptionStrings * opt = this->option<ConfigOptionStrings>(key);
                    std::vector<std::string> new_values;

                    new_values.resize(extruder_count * stride);
                    for (int e_index = 0; e_index < extruder_count; e_index++)
                    {
                        for (unsigned int i = 0; i < stride; i++)
                            new_values[e_index*stride + i] = opt->get_at(variant_index[e_index]*stride + i);
                    }
                    opt->values = new_values;
                    break;
                }
                case coInts:
                {
                    ConfigOptionInts * opt = this->option<ConfigOptionInts>(key);
                    std::vector<int> new_values;

                    new_values.resize(extruder_count * stride);
                    for (int e_index = 0; e_index < extruder_count; e_index++)
                    {
                        for (unsigned int i = 0; i < stride; i++)
                            new_values[e_index*stride + i] = opt->get_at(variant_index[e_index]*stride + i);
                    }
                    opt->values = new_values;
                    break;
                }
                case coFloats:
                {
                    ConfigOptionFloats * opt = this->option<ConfigOptionFloats>(key);
                    std::vector<double> new_values;

                    new_values.resize(extruder_count * stride);
                    for (int e_index = 0; e_index < extruder_count; e_index++)
                    {
                        for (unsigned int i = 0; i < stride; i++)
                            new_values[e_index*stride + i] = opt->get_at(variant_index[e_index]*stride + i);
                    }
                    opt->values = new_values;
                    break;
                }
                case coPercents:
                {
                    ConfigOptionPercents * opt = this->option<ConfigOptionPercents>(key);
                    std::vector<double> new_values;

                    new_values.resize(extruder_count * stride);
                    for (int e_index = 0; e_index < extruder_count; e_index++)
                    {
                        for (unsigned int i = 0; i < stride; i++)
                            new_values[e_index*stride + i] = opt->get_at(variant_index[e_index]*stride + i);
                    }
                    opt->values = new_values;
                    break;
                }
                case coFloatsOrPercents:
                {
                    ConfigOptionFloatsOrPercents * opt = this->option<ConfigOptionFloatsOrPercents>(key);
                    std::vector<FloatOrPercent> new_values;

                    new_values.resize(extruder_count * stride);
                    for (int e_index = 0; e_index < extruder_count; e_index++)
                    {
                        for (unsigned int i = 0; i < stride; i++)
                            new_values[e_index*stride + i] = opt->get_at(variant_index[e_index]*stride + i);
                    }
                    opt->values = new_values;
                    break;
                }
                case coBools:
                {
                    ConfigOptionBools * opt = this->option<ConfigOptionBools>(key);
                    std::vector<unsigned char> new_values;

                    new_values.resize(extruder_count * stride);
                    for (int e_index = 0; e_index < extruder_count; e_index++)
                    {
                        for (unsigned int i = 0; i < stride; i++)
                            new_values[e_index*stride + i] = opt->get_at(variant_index[e_index]*stride + i);
                    }
                    opt->values = new_values;
                    break;
                }
                case coEnums:
                {
                    ConfigOptionEnumsGeneric * opt = this->option<ConfigOptionEnumsGeneric>(key);
                    std::vector<int> new_values;

                    new_values.resize(extruder_count * stride);
                    for (int e_index = 0; e_index < extruder_count; e_index++)
                    {
                        for (unsigned int i = 0; i < stride; i++)
                            new_values[e_index*stride + i] = opt->get_at(variant_index[e_index]*stride + i);
                    }
                    opt->values = new_values;
                    break;
                }
                default:
                    BOOST_LOG_TRIVIAL(warning) << __FUNCTION__ << boost::format(", Line %1%: unsupported option type for %2%")%__LINE__%key;
                    break;
            }
        }
    }

    return variant_index;
}

void DynamicPrintConfig::update_values_to_printer_extruders_for_multiple_filaments(DynamicPrintConfig& printer_config, std::set<std::string>& key_set, std::string id_name, std::string variant_name)
{
    int extruder_count;
    bool different_extruder = printer_config.support_different_extruders(extruder_count);
    if ((extruder_count > 1) || different_extruder)
    {
        BOOST_LOG_TRIVIAL(info) << __FUNCTION__ << boost::format(", Line %1%:  extruder_count=%2%, different_extruder=%3%")%__LINE__ %extruder_count %different_extruder;
        std::vector<int> filament_maps =  printer_config.option<ConfigOptionInts>("filament_map")->values;
        size_t filament_count = filament_maps.size();
        //apply process settings
        //auto opt_nozzle_diameters = this->option<ConfigOptionFloats>("nozzle_diameter");
        //int extruder_count = opt_nozzle_diameters->size();
        auto opt_extruder_type = dynamic_cast<const ConfigOptionEnumsGeneric*>(printer_config.option("extruder_type"));
        auto opt_nozzle_volume_type = dynamic_cast<const ConfigOptionEnumsGeneric*>(printer_config.option("nozzle_volume_type"));
        std::vector<int> variant_index;


        variant_index.resize(filament_count, -1);

        for (int f_index = 0; f_index < filament_count; f_index++)
        {
            ExtruderType extruder_type = (ExtruderType)(opt_extruder_type->get_at(filament_maps[f_index] - 1));
            NozzleVolumeType nozzle_volume_type = (NozzleVolumeType)(opt_nozzle_volume_type->get_at(filament_maps[f_index] - 1));

            //variant index
            variant_index[f_index] = get_index_for_extruder(f_index+1, id_name, extruder_type, nozzle_volume_type, variant_name);
            if (variant_index[f_index] < 0) {
                BOOST_LOG_TRIVIAL(error) << __FUNCTION__ << boost::format(", Line %1%: could not found extruder_type %2%, nozzle_volume_type %3%, filament_index %4%, extruder index %5%")
                    %__LINE__ %s_keys_names_ExtruderType[extruder_type] % s_keys_names_NozzleVolumeType[nozzle_volume_type] % (f_index+1) %filament_maps[f_index];
                assert(false);
                //for some updates happens in a invalid state(caused by popup window)
                //we need to avoid crash
                variant_index[f_index] = 0;
            }
        }

        const ConfigDef       *config_def     = this->def();
        if (!config_def) {
            BOOST_LOG_TRIVIAL(error) << __FUNCTION__ << boost::format(", Line %1%: can not find config define")%__LINE__;
            return;
        }
        for (auto& key: key_set)
        {
            const ConfigOptionDef *optdef  = config_def->get(key);
            if (!optdef) {
                BOOST_LOG_TRIVIAL(warning) << __FUNCTION__ << boost::format(", Line %1%: can not find opt define for %2%")%__LINE__%key;
                continue;
            }
            switch (optdef->type) {
                case coStrings:
                {
                    ConfigOptionStrings * opt = this->option<ConfigOptionStrings>(key);
                    std::vector<std::string> new_values;

                    new_values.resize(filament_count);
                    for (int f_index = 0; f_index < filament_count; f_index++)
                    {
                        new_values[f_index] = opt->get_at(variant_index[f_index]);
                    }
                    opt->values = new_values;
                    break;
                }
                case coInts:
                {
                    ConfigOptionInts * opt = this->option<ConfigOptionInts>(key);
                    std::vector<int> new_values;

                    new_values.resize(filament_count);
                    for (int f_index = 0; f_index < filament_count; f_index++)
                    {
                        new_values[f_index] = opt->get_at(variant_index[f_index]);
                    }
                    opt->values = new_values;
                    break;
                }
                case coFloats:
                {
                    ConfigOptionFloats * opt = this->option<ConfigOptionFloats>(key);
                    std::vector<double> new_values;

                    new_values.resize(filament_count);
                    for (int f_index = 0; f_index < filament_count; f_index++)
                    {
                        new_values[f_index] = opt->get_at(variant_index[f_index]);
                    }
                    opt->values = new_values;
                    break;
                }
                case coPercents:
                {
                    ConfigOptionPercents * opt = this->option<ConfigOptionPercents>(key);
                    std::vector<double> new_values;

                    new_values.resize(filament_count);
                    for (int f_index = 0; f_index < filament_count; f_index++)
                    {
                        new_values[f_index] = opt->get_at(variant_index[f_index]);
                    }
                    opt->values = new_values;
                    break;
                }
                case coFloatsOrPercents:
                {
                    ConfigOptionFloatsOrPercents * opt = this->option<ConfigOptionFloatsOrPercents>(key);
                    std::vector<FloatOrPercent> new_values;

                    new_values.resize(filament_count);
                    for (int f_index = 0; f_index < filament_count; f_index++)
                    {
                        new_values[f_index] = opt->get_at(variant_index[f_index]);
                    }
                    opt->values = new_values;
                    break;
                }
                case coBools:
                {
                    ConfigOptionBools * opt = this->option<ConfigOptionBools>(key);
                    std::vector<unsigned char> new_values;

                    new_values.resize(filament_count);
                    for (int f_index = 0; f_index < filament_count; f_index++)
                    {
                        new_values[f_index] = opt->get_at(variant_index[f_index]);
                    }
                    opt->values = new_values;
                    break;
                }
                case coEnums:
                {
                    ConfigOptionEnumsGeneric * opt = this->option<ConfigOptionEnumsGeneric>(key);
                    std::vector<int> new_values;

                    new_values.resize(filament_count);
                    for (int f_index = 0; f_index < filament_count; f_index++)
                    {
                        new_values[f_index] = opt->get_at(variant_index[f_index]);
                    }
                    opt->values = new_values;
                    break;
                }
                default:
                    BOOST_LOG_TRIVIAL(warning) << __FUNCTION__ << boost::format(", Line %1%: unsupported option type for %2%")%__LINE__%key;
                    break;
            }
        }
    }
}


void DynamicPrintConfig::update_non_diff_values_to_base_config(DynamicPrintConfig& new_config, const t_config_option_keys& keys, const std::set<std::string>& different_keys,
    std::string extruder_id_name, std::string extruder_variant_name, std::set<std::string>& key_set1, std::set<std::string>& key_set2)
{
    std::vector<int> cur_extruder_ids, target_extruder_ids, variant_index;
    std::vector<std::string> cur_extruder_variants, target_extruder_variants;

    if (!extruder_id_name.empty()) {
        if (this->option(extruder_id_name))
            cur_extruder_ids = this->option<ConfigOptionInts>(extruder_id_name)->values;
        if (new_config.option(extruder_id_name))
            target_extruder_ids = new_config.option<ConfigOptionInts>(extruder_id_name)->values;
    }
    if (this->option(extruder_variant_name))
        cur_extruder_variants = this->option<ConfigOptionStrings>(extruder_variant_name, true)->values;
    if (new_config.option(extruder_variant_name))
        target_extruder_variants = new_config.option<ConfigOptionStrings>(extruder_variant_name, true)->values;

    int cur_variant_count = cur_extruder_variants.size();
    int target_variant_count = target_extruder_variants.size();

    variant_index.resize(target_variant_count, -1);
    if (cur_variant_count == 0) {
        variant_index[0] = 0;
    }
    else if ((cur_extruder_ids.size() > 0) && cur_variant_count != cur_extruder_ids.size()){
        //should not happen
        BOOST_LOG_TRIVIAL(error) << __FUNCTION__ << boost::format(" size of %1% = %2%, not equal to size of %3% = %4%")
             %extruder_variant_name %cur_variant_count %extruder_id_name %cur_extruder_ids.size();
    }
    else if ((target_extruder_ids.size() > 0) && target_variant_count != target_extruder_ids.size()){
        //should not happen
        BOOST_LOG_TRIVIAL(error) << __FUNCTION__ << boost::format(" size of %1% = %2%, not equal to size of %3% = %4%")
             %extruder_variant_name %target_variant_count %extruder_id_name %target_extruder_ids.size();
    }
    else {
        for (int i = 0; i < target_variant_count; i++)
        {
            for (int j = 0; j < cur_variant_count; j++)
            {
                if ((target_extruder_variants[i] == cur_extruder_variants[j])
                    &&(target_extruder_ids.empty() || (target_extruder_ids[i] == cur_extruder_ids[j])))
                {
                    variant_index[i] = j;
                    break;
                }
            }
        }
    }

    for (auto& opt : keys) {
        ConfigOption *opt_src = this->option(opt);
        const ConfigOption *opt_target = new_config.option(opt);
        if (opt_src && opt_target && (*opt_src != *opt_target)) {
            BOOST_LOG_TRIVIAL(debug) << __FUNCTION__ << boost::format(" change key %1% from old_value %2% to inherit's value %3%")
                    %opt %(opt_src->serialize()) %(opt_target->serialize());
            if (different_keys.find(opt) == different_keys.end()) {
                opt_src->set(opt_target);
            }
            else {
                if (opt_target->is_scalar()
                    || ((key_set1.find(opt) == key_set1.end()) && (key_set2.empty() || (key_set2.find(opt) == key_set2.end())))) {
                    //nothing to do, keep the original one
                }
                else {
                    ConfigOptionVectorBase* opt_vec_src = static_cast<ConfigOptionVectorBase*>(opt_src);
                    const ConfigOptionVectorBase* opt_vec_dest = static_cast<const ConfigOptionVectorBase*>(opt_target);
                    int stride = 1;
                    if (key_set2.find(opt) != key_set2.end())
                        stride = 2;
                    opt_vec_src->set_with_restore(opt_vec_dest, variant_index, stride);
                }
            }
        }
    }
    return;
}

void DynamicPrintConfig::update_diff_values_to_child_config(DynamicPrintConfig& new_config, std::string extruder_id_name, std::string extruder_variant_name, std::set<std::string>& key_set1, std::set<std::string>& key_set2)
{
    std::vector<int> cur_extruder_ids, target_extruder_ids, variant_index;
    std::vector<std::string> cur_extruder_variants, target_extruder_variants;

    if (!extruder_id_name.empty()) {
        if (this->option(extruder_id_name))
            cur_extruder_ids = this->option<ConfigOptionInts>(extruder_id_name)->values;
        if (new_config.option(extruder_id_name))
            target_extruder_ids = new_config.option<ConfigOptionInts>(extruder_id_name)->values;
    }
    if (this->option(extruder_variant_name))
        cur_extruder_variants = this->option<ConfigOptionStrings>(extruder_variant_name, true)->values;
    if (new_config.option(extruder_variant_name))
        target_extruder_variants = new_config.option<ConfigOptionStrings>(extruder_variant_name, true)->values;

    int cur_variant_count = cur_extruder_variants.size();
    int target_variant_count = target_extruder_variants.size();

    if (cur_variant_count > 0)
        variant_index.resize(cur_variant_count, -1);
    else
        variant_index.resize(1, 0);

    if (target_variant_count == 0) {
        variant_index[0] = 0;
    }
    else if ((cur_extruder_ids.size() > 0) && cur_variant_count != cur_extruder_ids.size()){
        //should not happen
        BOOST_LOG_TRIVIAL(error) << __FUNCTION__ << boost::format(" size of %1% = %2%, not equal to size of %3% = %4%")
             %extruder_variant_name %cur_variant_count %extruder_id_name %cur_extruder_ids.size();
    }
    else if ((target_extruder_ids.size() > 0) && target_variant_count != target_extruder_ids.size()){
        //should not happen
        BOOST_LOG_TRIVIAL(error) << __FUNCTION__ << boost::format(" size of %1% = %2%, not equal to size of %3% = %4%")
             %extruder_variant_name %target_variant_count %extruder_id_name %target_extruder_ids.size();
    }
    else {
        for (int i = 0; i < cur_variant_count; i++)
        {
            for (int j = 0; j < target_variant_count; j++)
            {
                if ((cur_extruder_variants[i] == target_extruder_variants[j])
                    &&(cur_extruder_ids.empty() || (cur_extruder_ids[i] == target_extruder_ids[j])))
                {
                    variant_index[i] = j;
                    break;
                }
            }
        }
    }

    const t_config_option_keys &keys = new_config.keys();
    for (auto& opt : keys) {
        if ((opt == extruder_id_name) || (opt == extruder_variant_name))
            continue;
        ConfigOption *opt_src = this->option(opt);
        const ConfigOption *opt_target = new_config.option(opt);
        if (opt_src && opt_target && (*opt_src != *opt_target)) {
            BOOST_LOG_TRIVIAL(debug) << __FUNCTION__ << boost::format(" change key %1% from base_value %2% to child's value %3%")
                    %opt %(opt_src->serialize()) %(opt_target->serialize());
            if (opt_target->is_scalar()
                || ((key_set1.find(opt) == key_set1.end()) && (key_set2.empty() || (key_set2.find(opt) == key_set2.end())))) {
                //nothing to do, keep the original one
                opt_src->set(opt_target);
            }
            else {
                ConfigOptionVectorBase* opt_vec_src = static_cast<ConfigOptionVectorBase*>(opt_src);
                const ConfigOptionVectorBase* opt_vec_dest = static_cast<const ConfigOptionVectorBase*>(opt_target);
                int stride = 1;
                if (key_set2.find(opt) != key_set2.end())
                    stride = 2;
                opt_vec_src->set_only_diff(opt_vec_dest, variant_index, stride);
            }
        }
    }
    return;
}

void update_static_print_config_from_dynamic(ConfigBase& config, const DynamicPrintConfig& dest_config, std::vector<int> variant_index, std::set<std::string>& key_set1, int stride)
{
    if (variant_index.size() > 0) {
        const t_config_option_keys &keys = dest_config.keys();
        for (auto& opt : keys) {
            ConfigOption *opt_src = config.option(opt);
            const ConfigOption *opt_dest = dest_config.option(opt);
            if (opt_src && opt_dest && (*opt_src != *opt_dest)) {
                if (opt_dest->is_scalar() || (key_set1.find(opt) == key_set1.end()))
                    opt_src->set(opt_dest);
                else {
                    ConfigOptionVectorBase* opt_vec_src = static_cast<ConfigOptionVectorBase*>(opt_src);
                    const ConfigOptionVectorBase* opt_vec_dest = static_cast<const ConfigOptionVectorBase*>(opt_dest);
                    opt_vec_src->set_to_index(opt_vec_dest, variant_index, stride);
                }
            }
        }
    }
    else
        config.apply(dest_config, true);
}

void compute_filament_override_value(const std::string& opt_key, const ConfigOption *opt_old_machine, const ConfigOption *opt_new_machine, const ConfigOption *opt_new_filament, const DynamicPrintConfig& new_full_config,
    t_config_option_keys& diff_keys, DynamicPrintConfig& filament_overrides, std::vector<int>& f_maps)
{
    bool is_nil = opt_new_filament->is_nil();

    // ugly code, for these params, we should ignore the value in filament params
    ConfigOptionBoolsNullable opt_long_retraction_default;
    if (opt_key == "long_retractions_when_cut" && new_full_config.option<ConfigOptionInt>("enable_long_retraction_when_cut")->value != LongRectrationLevel::EnableFilament) {
        auto ptr = dynamic_cast<const ConfigOptionBoolsNullable*>(opt_new_filament);
        for (size_t idx = 0; idx < ptr->values.size(); ++idx)
            opt_long_retraction_default.values.push_back(ptr->nil_value());
        opt_new_filament = &opt_long_retraction_default;
    }

    ConfigOptionFloatsNullable opt_retraction_distance_default;
    if (opt_key == "retraction_distances_when_cut" && new_full_config.option<ConfigOptionInt>("enable_long_retraction_when_cut")->value != LongRectrationLevel::EnableFilament) {
        auto ptr = dynamic_cast<const ConfigOptionFloatsNullable*>(opt_new_filament);
        for (size_t idx = 0; idx < ptr->values.size(); ++idx)
            opt_long_retraction_default.values.push_back(ptr->nil_value());
        opt_new_filament = &opt_retraction_distance_default;
    }

    auto opt_copy = opt_new_machine->clone();
    opt_copy->apply_override(opt_new_filament, f_maps);
    bool changed = *opt_old_machine != *opt_copy;

    if (changed) {
        diff_keys.emplace_back(opt_key);
        filament_overrides.set_key_value(opt_key, opt_copy);
    }
    else
        delete opt_copy;
}


//BBS: pass map to recording all invalid valies
//FIXME localize this function.
std::map<std::string, std::string> validate(const FullPrintConfig &cfg, bool under_cli)
{
    std::map<std::string, std::string> error_message;
    // --layer-height
    if (cfg.get_abs_value("layer_height") <= 0) {
        error_message.emplace("layer_height", L("invalid value ") + std::to_string(cfg.get_abs_value("layer_height")));
    }
    else if (fabs(fmod(cfg.get_abs_value("layer_height"), SCALING_FACTOR)) > 1e-4) {
        error_message.emplace("layer_height", L("invalid value ") + std::to_string(cfg.get_abs_value("layer_height")));
    }

    // --first-layer-height
    if (cfg.initial_layer_print_height.value <= 0) {
        error_message.emplace("initial_layer_print_height", L("invalid value ") + std::to_string(cfg.initial_layer_print_height.value));
    }

    // --filament-diameter
    for (double fd : cfg.filament_diameter.values)
        if (fd < 1) {
            error_message.emplace("filament_diameter", L("invalid value ") + cfg.filament_diameter.serialize());
            break;
        }

    // --nozzle-diameter
    for (double nd : cfg.nozzle_diameter.values)
        if (nd < 0.005) {
            error_message.emplace("nozzle_diameter", L("invalid value ") + cfg.nozzle_diameter.serialize());
            break;
        }

    // --perimeters
    if (cfg.wall_loops.value < 0) {
        error_message.emplace("wall_loops", L("invalid value ") + std::to_string(cfg.wall_loops.value));
    }

    // --solid-layers
    if (cfg.top_shell_layers < 0) {
        error_message.emplace("top_shell_layers", L("invalid value ") + std::to_string(cfg.top_shell_layers));
    }
    if (cfg.bottom_shell_layers < 0) {
        error_message.emplace("bottom_shell_layers", L("invalid value ") + std::to_string(cfg.bottom_shell_layers));
    }

    std::set<GCodeFlavor>with_firmware_retraction_flavor = {
        gcfSmoothie,
        gcfRepRapSprinter,
        gcfRepRapFirmware,
        gcfMarlinLegacy,
        gcfMarlinFirmware,
        gcfMachinekit,
        gcfRepetier,
        gcfKlipper
    };
    if (cfg.use_firmware_retraction.value && with_firmware_retraction_flavor.count(cfg.gcode_flavor.value)==0)
        error_message.emplace("gcode_flavor",L("--use-firmware-retraction is only supported by Marlin, Klipper, Smoothie, RepRapFirmware, Repetier and Machinekit firmware"));

    if (cfg.use_firmware_retraction.value)
        for (unsigned char wipe : cfg.wipe.values)
            if (wipe)
                error_message.emplace("wipe",L("--use-firmware-retraction is not compatible with --wipe"));

    // --gcode-flavor
    if (! print_config_def.get("gcode_flavor")->has_enum_value(cfg.gcode_flavor.serialize())) {
        error_message.emplace("gcode_flavor", L("invalid value ") + cfg.gcode_flavor.serialize());
    }

    // --fill-pattern
    if (! print_config_def.get("sparse_infill_pattern")->has_enum_value(cfg.sparse_infill_pattern.serialize())) {
        error_message.emplace("sparse_infill_pattern", L("invalid value ") + cfg.sparse_infill_pattern.serialize());
    }

    // --top-fill-pattern
    if (! print_config_def.get("top_surface_pattern")->has_enum_value(cfg.top_surface_pattern.serialize())) {
        error_message.emplace("top_surface_pattern", L("invalid value ") + cfg.top_surface_pattern.serialize());
    }

    // --bottom-fill-pattern
    if (!print_config_def.get("bottom_surface_pattern")->has_enum_value(cfg.bottom_surface_pattern.serialize())) {
        error_message.emplace("bottom_surface_pattern", L("invalid value ") + cfg.bottom_surface_pattern.serialize());
    }

    // --soild-fill-pattern
    if (!print_config_def.get("internal_solid_infill_pattern")->has_enum_value(cfg.internal_solid_infill_pattern.serialize())) {
        error_message.emplace("internal_solid_infill_pattern", L("invalid value ") + cfg.internal_solid_infill_pattern.serialize());
    }

    // --fill-density
    if (fabs(cfg.sparse_infill_density.value - 100.) < EPSILON &&
        ! print_config_def.get("top_surface_pattern")->has_enum_value(cfg.sparse_infill_pattern.serialize())) {
        error_message.emplace("sparse_infill_pattern", cfg.sparse_infill_pattern.serialize() + L(" doesn't work at 100%% density "));
    }

    // --skirt-height
    if (cfg.skirt_height < 0) {
        error_message.emplace("skirt_height", L("invalid value ") + std::to_string(cfg.skirt_height));
    }

    // --bridge-flow-ratio
    if (cfg.bridge_flow <= 0) {
        error_message.emplace("bridge_flow", L("invalid value ") + std::to_string(cfg.bridge_flow));
    }

    // extruder clearance
    if (cfg.extruder_clearance_max_radius <= 0) {
        error_message.emplace("extruder_clearance_max_radius", L("invalid value ") + std::to_string(cfg.extruder_clearance_max_radius));
    }
    if (cfg.extruder_clearance_height_to_rod <= 0) {
        error_message.emplace("extruder_clearance_height_to_rod", L("invalid value ") + std::to_string(cfg.extruder_clearance_height_to_rod));
    }
    if (cfg.extruder_clearance_height_to_lid <= 0) {
        error_message.emplace("extruder_clearance_height_to_lid", L("invalid value ") + std::to_string(cfg.extruder_clearance_height_to_lid));
    }
    if (cfg.nozzle_height <= 0)
        error_message.emplace("nozzle_height", L("invalid value ") + std::to_string(cfg.nozzle_height));

    // --extrusion-multiplier
    for (double em : cfg.filament_flow_ratio.values)
        if (em <= 0) {
            error_message.emplace("filament_flow_ratio", L("invalid value ") + cfg.filament_flow_ratio.serialize());
            break;
        }

    // The following test was commented out after 482841b, see also https://github.com/prusa3d/PrusaSlicer/pull/6743.
    // The backend should now handle this case correctly. I.e., zero default_acceleration behaves as if all others
    // were zero too. This is now consistent with what the UI said would happen.
    // The UI already grays the fields out, there is no more reason to reject it here. This function validates the
    // config before exporting, leaving this check in would mean that config would be rejected before export
    // (although both the UI and the backend handle it).
    // --default-acceleration
    //if ((cfg.outer_wall_acceleration != 0. || cfg.infill_acceleration != 0. || cfg.bridge_acceleration != 0. || cfg.initial_layer_acceleration != 0.) &&
    //    cfg.default_acceleration == 0.)
    //    return "Invalid zero value for --default-acceleration when using other acceleration settings";

    // --spiral-vase
    //for non-cli case, we will popup dialog for spiral mode correction
    if (cfg.spiral_mode && under_cli) {
        // Note that we might want to have more than one perimeter on the bottom
        // solid layers.
        if (cfg.wall_loops != 1) {
            error_message.emplace("wall_loops", L("Invalid value when spiral vase mode is enabled: ") + std::to_string(cfg.wall_loops));
            //return "Can't make more than one perimeter when spiral vase mode is enabled";
            //return "Can't make less than one perimeter when spiral vase mode is enabled";
        }

        if (cfg.sparse_infill_density > 0) {
            error_message.emplace("sparse_infill_density", L("Invalid value when spiral vase mode is enabled: ") + std::to_string(cfg.sparse_infill_density));
            //return "Spiral vase mode can only print hollow objects, so you need to set Fill density to 0";
        }

        if (cfg.top_shell_layers > 0) {
            error_message.emplace("top_shell_layers", L("Invalid value when spiral vase mode is enabled: ") + std::to_string(cfg.top_shell_layers));
            //return "Spiral vase mode is not compatible with top solid layers";
        }

        if (cfg.enable_support ) {
            error_message.emplace("enable_support", L("Invalid value when spiral vase mode is enabled: ") + std::to_string(cfg.enable_support));
            //return "Spiral vase mode is not compatible with support";
        }
        if (cfg.enforce_support_layers > 0) {
            error_message.emplace("enforce_support_layers", L("Invalid value when spiral vase mode is enabled: ") + std::to_string(cfg.enforce_support_layers));
            //return "Spiral vase mode is not compatible with support";
        }
    }

    // extrusion widths
    {
        double max_nozzle_diameter = 0.;
        for (double dmr : cfg.nozzle_diameter.values)
            max_nozzle_diameter = std::max(max_nozzle_diameter, dmr);
        const char *widths[] = {
            "outer_wall_line_width",
            "inner_wall_line_width",
            "sparse_infill_line_width",
            "internal_solid_infill_line_width",
            "top_surface_line_width",
            "support_line_width",
            "initial_layer_line_width",
            "skin_infill_line_width",
            "skeleton_infill_line_width"};
        for (size_t i = 0; i < sizeof(widths) / sizeof(widths[i]); ++ i) {
            std::string key(widths[i]);
            if (cfg.get_abs_value(key) > 2.5 * max_nozzle_diameter) {
                error_message.emplace(key, L("too large line width ") + std::to_string(cfg.get_abs_value(key)));
                //return std::string("Too Large line width: ") + key;
            }
        }
    }

    // Out of range validation of numeric values.
    for (const std::string &opt_key : cfg.keys()) {
        const ConfigOption      *opt    = cfg.optptr(opt_key);
        assert(opt != nullptr);
        const ConfigOptionDef   *optdef = print_config_def.get(opt_key);
        assert(optdef != nullptr);
        bool out_of_range = false;
        switch (opt->type()) {
        case coFloat:
        case coPercent:
        case coFloatOrPercent:
        {
            auto *fopt = static_cast<const ConfigOptionFloat*>(opt);
            out_of_range = fopt->value < optdef->min || fopt->value > optdef->max;
            break;
        }
        case coFloats:
        case coPercents:
            for (double v : static_cast<const ConfigOptionVector<double>*>(opt)->values)
                if (v < optdef->min || v > optdef->max) {
                    out_of_range = true;
                    break;
                }
            break;
        case coInt:
        {
            auto *iopt = static_cast<const ConfigOptionInt*>(opt);
            out_of_range = iopt->value < optdef->min || iopt->value > optdef->max;
            break;
        }
        case coInts:
            for (int v : static_cast<const ConfigOptionVector<int>*>(opt)->values)
                if (v < optdef->min || v > optdef->max) {
                    out_of_range = true;
                    break;
                }
            break;
        default:;
        }
        if (out_of_range) {
            if (error_message.find(opt_key) == error_message.end())
                error_message.emplace(opt_key, opt->serialize() + L(" not in range ") +"[" + std::to_string(optdef->min) + "," + std::to_string(optdef->max) + "]");
            //return std::string("Value out of range: " + opt_key);
        }
    }

    // The configuration is valid.
    return error_message;
}

// Declare and initialize static caches of StaticPrintConfig derived classes.
#define PRINT_CONFIG_CACHE_ELEMENT_DEFINITION(r, data, CLASS_NAME) StaticPrintConfig::StaticCache<class Slic3r::CLASS_NAME> BOOST_PP_CAT(CLASS_NAME::s_cache_, CLASS_NAME);
#define PRINT_CONFIG_CACHE_ELEMENT_INITIALIZATION(r, data, CLASS_NAME) Slic3r::CLASS_NAME::initialize_cache();
#define PRINT_CONFIG_CACHE_INITIALIZE(CLASSES_SEQ) \
    BOOST_PP_SEQ_FOR_EACH(PRINT_CONFIG_CACHE_ELEMENT_DEFINITION, _, BOOST_PP_TUPLE_TO_SEQ(CLASSES_SEQ)) \
    int print_config_static_initializer() { \
        /* Putting a trace here to avoid the compiler to optimize out this function. */ \
        BOOST_LOG_TRIVIAL(trace) << "Initializing StaticPrintConfigs"; \
        BOOST_PP_SEQ_FOR_EACH(PRINT_CONFIG_CACHE_ELEMENT_INITIALIZATION, _, BOOST_PP_TUPLE_TO_SEQ(CLASSES_SEQ)) \
        return 1; \
    }
PRINT_CONFIG_CACHE_INITIALIZE((
    PrintObjectConfig, PrintRegionConfig, MachineEnvelopeConfig, GCodeConfig, PrintConfig, FullPrintConfig,
    SLAMaterialConfig, SLAPrintConfig, SLAPrintObjectConfig, SLAPrinterConfig, SLAFullPrintConfig))
static int print_config_static_initialized = print_config_static_initializer();

//BBS: remove unused command currently
CLIActionsConfigDef::CLIActionsConfigDef()
{
    ConfigOptionDef* def;

    // Actions:
    /*def = this->add("export_obj", coBool);
    def->label = L("Export OBJ");
    def->tooltip = L("Export the model(s) as OBJ.");
    def->set_default_value(new ConfigOptionBool(false));*/

/*
    def = this->add("export_svg", coBool);
    def->label = L("Export SVG");
    def->tooltip = L("Slice the model and export solid slices as SVG.");
    def->set_default_value(new ConfigOptionBool(false));
*/

    /*def = this->add("export_sla", coBool);
    def->label = L("Export SLA");
    def->tooltip = L("Slice the model and export SLA printing layers as PNG.");
    def->cli = "export-sla|sla";
    def->set_default_value(new ConfigOptionBool(false));*/

    def = this->add("export_3mf", coString);
    def->label = "Export 3MF";
    def->tooltip = "Export project as 3MF.";
    def->cli_params = "filename.3mf";
    def->set_default_value(new ConfigOptionString("output.3mf"));

    def = this->add("export_slicedata", coString);
    def->label = "Export slicing data";
    def->tooltip = "Export slicing data to a folder.";
    def->cli_params = "slicing_data_directory";
    def->set_default_value(new ConfigOptionString("cached_data"));

    def = this->add("load_slicedata", coStrings);
    def->label = "Load slicing data";
    def->tooltip = "Load cached slicing data from directory";
    def->cli_params = "slicing_data_directory";
    def->set_default_value(new ConfigOptionString("cached_data"));

    /*def = this->add("export_amf", coBool);
    def->label = L("Export AMF");
    def->tooltip = L("Export the model(s) as AMF.");
    def->set_default_value(new ConfigOptionBool(false));*/

    def = this->add("export_stl", coBool);
    def->label = "Export STL";
    def->tooltip = "Export the objects as single STL.";
    def->set_default_value(new ConfigOptionBool(false));

    def = this->add("export_stls", coString);
    def->label = "Export multiple stls";
    def->tooltip = "Export the objects as multiple stls to directory";
    def->set_default_value(new ConfigOptionString("stl_path"));

    /*def = this->add("export_gcode", coBool);
    def->label = L("Export G-code");
    def->tooltip = L("Slice the model and export toolpaths as G-code.");
    def->cli = "export-gcode|gcode|g";
    def->set_default_value(new ConfigOptionBool(false));*/

    /*def = this->add("gcodeviewer", coBool);
    // BBS: remove _L()
    def->label = ("G-code viewer");
    def->tooltip = ("Visualize an already sliced and saved G-code");
    def->cli = "gcodeviewer";
    def->set_default_value(new ConfigOptionBool(false));*/

    def = this->add("slice", coInt);
    def->label = "Slice";
    def->tooltip = "Slice the plates: 0-all plates, i-plate i, others-invalid";
    def->cli = "slice";
    def->cli_params = "option";
    def->set_default_value(new ConfigOptionInt(0));

    def = this->add("export_png", coInt);
    def->label = "Export png of plate";
    def->tooltip = "Export png of plate: 0-all plates, i-plate i, others-invalid";
    def->cli = "export-png";
    def->cli_params = "option";
    def->set_default_value(new ConfigOptionInt(-1));

    def = this->add("help", coBool);
    def->label = "Help";
    def->tooltip = "Show command help.";
    def->cli = "help|h";
    def->set_default_value(new ConfigOptionBool(false));

    def = this->add("uptodate", coBool);
    def->label = "UpToDate";
    def->tooltip = "Update the configs values of 3mf to latest.";
    def->cli = "uptodate";
    def->set_default_value(new ConfigOptionBool(false));

    def = this->add("load_defaultfila", coBool);
    def->label = "Load default filaments";
    def->tooltip = "Load first filament as default for those not loaded";
    def->cli_params = "option";
    def->set_default_value(new ConfigOptionBool(false));

    def = this->add("min_save", coBool);
    def->label = "Minimum save";
    def->tooltip = "export 3mf with minimum size.";
    def->cli_params = "option";
    def->set_default_value(new ConfigOptionBool(false));

    def = this->add("mtcpp", coInt);
    def->label = "mtcpp";
    def->tooltip = "max triangle count per plate for slicing.";
    def->cli = "mtcpp";
    def->cli_params = "count";
    def->set_default_value(new ConfigOptionInt(1000000));

    def = this->add("mstpp", coInt);
    def->label = "mstpp";
    def->tooltip = "max slicing time per plate in seconds.";
    def->cli = "mstpp";
    def->cli_params = "time";
    def->set_default_value(new ConfigOptionInt(300));

    // must define new params here, otherwise comamnd param check will fail
    def = this->add("no_check", coBool);
    def->label = L("No check");
    def->tooltip = L("Do not run any validity checks, such as gcode path conflicts check.");
    def->set_default_value(new ConfigOptionBool(false));

    def = this->add("normative_check", coBool);
    def->label = "Normative check";
    def->tooltip = "Check the normative items.";
    def->cli_params = "option";
    def->set_default_value(new ConfigOptionBool(true));

    /*def = this->add("help_fff", coBool);
    def->label = L("Help (FFF options)");
    def->tooltip = L("Show the full list of print/G-code configuration options.");
    def->set_default_value(new ConfigOptionBool(false));

    def = this->add("help_sla", coBool);
    def->label = L("Help (SLA options)");
    def->tooltip = L("Show the full list of SLA print configuration options.");
    def->set_default_value(new ConfigOptionBool(false));*/

    def = this->add("info", coBool);
    def->label = "Output Model Info";
    def->tooltip = "Output the model's information.";
    def->set_default_value(new ConfigOptionBool(false));

    def = this->add("export_settings", coString);
    def->label = "Export Settings";
    def->tooltip = "Export settings to a file.";
    def->cli_params = "settings.json";
    def->set_default_value(new ConfigOptionString("output.json"));

    def = this->add("pipe", coString);
    def->label = "Send progress to pipe";
    def->tooltip = "Send progress to pipe.";
    def->cli_params = "pipename";
    def->set_default_value(new ConfigOptionString(""));
}

//BBS: remove unused command currently
CLITransformConfigDef::CLITransformConfigDef()
{
    ConfigOptionDef* def;

    // Transform options:
    /*def = this->add("align_xy", coPoint);
    def->label = L("Align XY");
    def->tooltip = L("Align the model to the given point.");
    def->set_default_value(new ConfigOptionPoint(Vec2d(100,100)));

    def = this->add("cut", coFloat);
    def->label = L("Cut");
    def->tooltip = L("Cut model at the given Z.");
    def->set_default_value(new ConfigOptionFloat(0));*/

/*
    def = this->add("cut_grid", coFloat);
    def->label = L("Cut");
    def->tooltip = L("Cut model in the XY plane into tiles of the specified max size.");
    def->set_default_value(new ConfigOptionPoint());

    def = this->add("cut_x", coFloat);
    def->label = L("Cut");
    def->tooltip = L("Cut model at the given X.");
    def->set_default_value(new ConfigOptionFloat(0));

    def = this->add("cut_y", coFloat);
    def->label = L("Cut");
    def->tooltip = L("Cut model at the given Y.");
    def->set_default_value(new ConfigOptionFloat(0));
*/

    /*def = this->add("center", coPoint);
    def->label = L("Center");
    def->tooltip = L("Center the print around the given center.");
    def->set_default_value(new ConfigOptionPoint(Vec2d(100,100)));*/

    def = this->add("arrange", coInt);
    def->label = "Arrange Options";
    def->tooltip = "Arrange options: 0-disable, 1-enable, others-auto";
    def->cli_params = "option";
    //def->cli = "arrange|a";
    def->set_default_value(new ConfigOptionInt(0));

    def = this->add("repetitions", coInt);
    def->label = "Repetition count";
    def->tooltip = "Repetition count of the whole model";
    def->cli_params = "count";
    def->set_default_value(new ConfigOptionInt(1));

    def = this->add("ensure_on_bed", coBool);
    def->label = "Ensure on bed";
    def->tooltip = "Lift the object above the bed when it is partially below. Disabled by default";
    def->set_default_value(new ConfigOptionBool(false));

    /*def = this->add("copy", coInt);
    def->label = L("Copy");
    def->tooltip =L("Duplicate copies of model");
    def->min = 1;
    def->set_default_value(new ConfigOptionInt(1));*/

    /*def = this->add("duplicate_grid", coPoint);
    def->label = L("Duplicate by grid");
    def->tooltip = L("Multiply copies by creating a grid.");*/

    def = this->add("assemble", coBool);
    def->label = "Assemble";
    def->tooltip = "Arrange the supplied models in a plate and merge them in a single model in order to perform actions once.";
    //def->cli = "merge|m";
    def->set_default_value(new ConfigOptionBool(false));

    def = this->add("convert_unit", coBool);
    def->label = "Convert Unit";
    def->tooltip = "Convert the units of model";
    def->set_default_value(new ConfigOptionBool(false));

    def = this->add("orient", coInt);
    def->label = "Orient Options";
    def->tooltip = "Orient options: 0-disable, 1-enable, others-auto";
    //def->cli = "orient|o";
    def->set_default_value(new ConfigOptionInt(0));

    /*def = this->add("repair", coBool);
    def->label = L("Repair");
    def->tooltip = L("Repair the model's meshes if it is non-manifold mesh");
    def->set_default_value(new ConfigOptionBool(false));*/

    def = this->add("rotate", coFloat);
    def->label = "Rotate";
    def->tooltip = "Rotation angle around the Z axis in degrees.";
    def->set_default_value(new ConfigOptionFloat(0));

    def = this->add("rotate_x", coFloat);
    def->label = "Rotate around X";
    def->tooltip = "Rotation angle around the X axis in degrees.";
    def->set_default_value(new ConfigOptionFloat(0));

    def = this->add("rotate_y", coFloat);
    def->label = "Rotate around Y";
    def->tooltip = "Rotation angle around the Y axis in degrees.";
    def->set_default_value(new ConfigOptionFloat(0));

    def = this->add("scale", coFloat);
    def->label = "Scale";
    def->tooltip = "Scale the model by a float factor";
    def->cli_params = "factor";
    def->set_default_value(new ConfigOptionFloat(1.f));

    /*def = this->add("split", coBool);
    def->label = L("Split");
    def->tooltip = L("Detect unconnected parts in the given model(s) and split them into separate objects.");

    def = this->add("scale_to_fit", coPoint3);
    def->label = L("Scale to Fit");
    def->tooltip = L("Scale to fit the given volume.");
    def->set_default_value(new ConfigOptionPoint3(Vec3d(0,0,0)));*/
}

CLIMiscConfigDef::CLIMiscConfigDef()
{
    ConfigOptionDef* def;

    /*def = this->add("ignore_nonexistent_config", coBool);
    def->label = L("Ignore non-existent config files");
    def->tooltip = L("Do not fail if a file supplied to --load does not exist.");

    def = this->add("config_compatibility", coEnum);
    def->label = L("Forward-compatibility rule when loading configurations from config files and project files (3MF, AMF).");
    def->tooltip = L("This version of BambuStudio may not understand configurations produced by the newest BambuStudio versions. "
                     "For example, newer BambuStudio may extend the list of supported firmware flavors. One may decide to "
                     "bail out or to substitute an unknown value with a default silently or verbosely.");
    def->enum_keys_map = &ConfigOptionEnum<ForwardCompatibilitySubstitutionRule>::get_enum_values();
    def->enum_values.push_back("disable");
    def->enum_values.push_back("enable");
    def->enum_values.push_back("enable_silent");
    def->enum_labels.push_back(L("Bail out on unknown configuration values"));
    def->enum_labels.push_back(L("Enable reading unknown configuration values by verbosely substituting them with defaults."));
    def->enum_labels.push_back(L("Enable reading unknown configuration values by silently substituting them with defaults."));
    def->set_default_value(new ConfigOptionEnum<ForwardCompatibilitySubstitutionRule>(ForwardCompatibilitySubstitutionRule::Enable));*/

    /*def = this->add("load", coStrings);
    def->label = L("Load config file");
    def->tooltip = L("Load configuration from the specified file. It can be used more than once to load options from multiple files.");*/

    def = this->add("load_settings", coStrings);
    def->label = "Load General Settings";
    def->tooltip = "Load process/machine settings from the specified file";
    def->cli_params = "\"setting1.json;setting2.json\"";
    def->set_default_value(new ConfigOptionStrings());

    def = this->add("load_filaments", coStrings);
    def->label = "Load Filament Settings";
    def->tooltip = "Load filament settings from the specified file list";
    def->cli_params = "\"filament1.json;filament2.json;...\"";
    def->set_default_value(new ConfigOptionStrings());

    def = this->add("skip_objects", coInts);
    def->label = "Skip Objects";
    def->tooltip = "Skip some objects in this print";
    def->cli_params = "\"3,5,10,77\"";
    def->set_default_value(new ConfigOptionInts());

    def = this->add("clone_objects", coInts);
    def->label = "Clone Objects";
    def->tooltip = "Clone objects in the load list";
    def->cli_params = "\"1,3,1,10\"";
    def->set_default_value(new ConfigOptionInts());

    def = this->add("uptodate_settings", coStrings);
    def->label = "load uptodate process/machine settings when using uptodate";
    def->tooltip = "load uptodate process/machine settings from the specified file when using uptodate";
    def->cli_params = "\"setting1.json;setting2.json\"";
    def->set_default_value(new ConfigOptionStrings());

    def = this->add("uptodate_filaments", coStrings);
    def->label = "load uptodate filament settings when using uptodate";
    def->tooltip = "load uptodate filament settings from the specified file when using uptodate";
    def->cli_params = "\"filament1.json;filament2.json;...\"";
    def->set_default_value(new ConfigOptionStrings());

    def = this->add("downward_check", coBool);
    def->label = "downward machines check";
    def->tooltip = "if enabled, check whether current machine downward compatible with the machines in the list";
    def->set_default_value(new ConfigOptionBool(false));

    def = this->add("downward_settings", coStrings);
    def->label = "downward machines settings";
    def->tooltip = "the machine settings list need to do downward checking";
    def->cli_params = "\"machine1.json;machine2.json;...\"";
    def->set_default_value(new ConfigOptionStrings());

    def = this->add("load_assemble_list", coString);
    def->label = "Load assemble list";
    def->tooltip = "Load assemble object list from config file";
    def->cli_params = "assemble_list.json";
    def->set_default_value(new ConfigOptionString());

    /*def = this->add("output", coString);
    def->label = L("Output File");
    def->tooltip = L("The file where the output will be written (if not specified, it will be based on the input file).");
    def->cli = "output|o";

    def = this->add("single_instance", coBool);
    def->label = L("Single instance mode");
    def->tooltip = L("If enabled, the command line arguments are sent to an existing instance of GUI BambuStudio, "
                     "or an existing BambuStudio window is activated. "
                     "Overrides the \"single_instance\" configuration value from application preferences.");*/

/*
    def = this->add("autosave", coString);
    def->label = L("Autosave");
    def->tooltip = L("Automatically export current configuration to the specified file.");
*/

    def = this->add("outputdir", coString);
    def->label = "Output directory";
    def->tooltip = "Output directory for the exported files.";
    def->cli_params = "dir";
    def->set_default_value(new ConfigOptionString());

    def = this->add("debug", coInt);
    def->label = "Debug level";
    def->tooltip = "Sets debug logging level. 0:fatal, 1:error, 2:warning, 3:info, 4:debug, 5:trace\n";
    def->min = 0;
    def->cli_params = "level";
    def->set_default_value(new ConfigOptionInt(1));

    def = this->add("enable_timelapse", coBool);
    def->label = "Enable timeplapse for print";
    def->tooltip = "If enabled, this slicing will be considered using timelapse";
    def->set_default_value(new ConfigOptionBool(false));

#if (defined(_MSC_VER) || defined(__MINGW32__)) && defined(SLIC3R_GUI)
    /*def = this->add("sw_renderer", coBool);
    def->label = L("Render with a software renderer");
    def->tooltip = L("Render with a software renderer. The bundled MESA software renderer is loaded instead of the default OpenGL driver.");
    def->min = 0;*/
#endif /* _MSC_VER */

    def = this->add("load_custom_gcodes", coString);
    def->label = "Load custom gcode";
    def->tooltip = "Load custom gcode from json";
    def->cli_params = "custom_gcode_toolchange.json";
    def->set_default_value(new ConfigOptionString());

    def = this->add("load_filament_ids", coInts);
    def->label = "Load filament ids";
    def->tooltip = "Load filament ids for each object";
    def->cli_params = "\"1,2,3,1\"";
    def->set_default_value(new ConfigOptionInts());

    def = this->add("allow_multicolor_oneplate", coBool);
    def->label = "Allow multiple color on one plate";
    def->tooltip = "If enabled, the arrange will allow multiple color on one plate";
    def->set_default_value(new ConfigOptionBool(true));

    def = this->add("allow_rotations", coBool);
    def->label = "Allow rotatations when arrange";
    def->tooltip = "If enabled, the arrange will allow rotations when place object";
    def->set_default_value(new ConfigOptionBool(true));

    def = this->add("avoid_extrusion_cali_region", coBool);
    def->label = "Avoid extrusion calibrate region when doing arrange";
    def->tooltip = "If enabled, the arrange will avoid extrusion calibrate region when place object";
    def->set_default_value(new ConfigOptionBool(false));

    def = this->add("skip_modified_gcodes", coBool);
    def->label = "Skip modified gcodes in 3mf";
    def->tooltip = "Skip the modified gcodes in 3mf from Printer or filament Presets";
    def->cli_params = "option";
    def->set_default_value(new ConfigOptionBool(false));

    def = this->add("skip_useless_pick", coBool);
    def->label = "Skip generating useless pick/top images into 3mf";
    def->tooltip = "Skip generating useless pick/top images into 3mf";
    def->cli_params = "option";
    def->set_default_value(new ConfigOptionBool(false));

    def = this->add("makerlab_name", coString);
    def->label = "MakerLab name";
    def->tooltip = "MakerLab name to generate this 3mf";
    def->cli_params = "name";
    def->set_default_value(new ConfigOptionString());

    def = this->add("makerlab_version", coString);
    def->label = "MakerLab version";
    def->tooltip = "MakerLab version to generate this 3mf";
    def->cli_params = "version";
    def->set_default_value(new ConfigOptionString());

    def = this->add("metadata_name", coStrings);
    def->label = "metadata name list";
    def->tooltip = "matadata name list added into 3mf";
    def->cli_params = "\"name1;name2;...\"";
    def->set_default_value(new ConfigOptionStrings());

    def = this->add("metadata_value", coStrings);
    def->label = "metadata value list";
    def->tooltip = "matadata value list added into 3mf";
    def->cli_params = "\"value1;value2;...\"";
    def->set_default_value(new ConfigOptionStrings());

    def = this->add("allow_newer_file", coBool);
    def->label = "Allow 3mf with newer version to be sliced";
    def->tooltip = "Allow 3mf with newer version to be sliced";
    def->cli_params = "option";
    def->set_default_value(new  ConfigOptionBool(false));

    def = this->add("allow_mix_temp", coBool);
    def->label = "Allow filaments with high/low temperature to be printed together";
    def->tooltip = "Allow filaments with high/low temperature to be printed together";
    def->cli_params = "option";
    def->set_default_value(new  ConfigOptionBool(false));

    def = this->add("camera_view", coInt);
    def->label = "Camera view angle for exporting png";
    def->tooltip = "Camera view angle for exporting png: 0-Iso, 1-Top_Front, 2-Left, 3-Right, 10-Iso_1, 11-Iso_2, 12-Iso_3";
    def->cli_params = "angle";
    def->set_default_value(new ConfigOptionInt(0));
}

const CLIActionsConfigDef    cli_actions_config_def;
const CLITransformConfigDef  cli_transform_config_def;
const CLIMiscConfigDef       cli_misc_config_def;

DynamicPrintAndCLIConfig::PrintAndCLIConfigDef DynamicPrintAndCLIConfig::s_def;

void DynamicPrintAndCLIConfig::handle_legacy(t_config_option_key &opt_key, std::string &value) const
{
    if (cli_actions_config_def  .options.find(opt_key) == cli_actions_config_def  .options.end() &&
        cli_transform_config_def.options.find(opt_key) == cli_transform_config_def.options.end() &&
        cli_misc_config_def     .options.find(opt_key) == cli_misc_config_def     .options.end()) {
        PrintConfigDef::handle_legacy(opt_key, value);
    }
}

uint64_t ModelConfig::s_last_timestamp = 1;

static Points to_points(const std::vector<Vec2d> &dpts)
{
    Points pts; pts.reserve(dpts.size());
    for (auto &v : dpts)
        pts.emplace_back( coord_t(scale_(v.x())), coord_t(scale_(v.y())) );
    return pts;
}

Polygon get_shared_poly(const std::vector<Pointfs>& extruder_polys)
{
    Polygon result;
    for (int index = 0; index < extruder_polys.size(); index++)
    {
        const Pointfs& extruder_area = extruder_polys[index];
        if (index == 0)
            result.points = to_points(extruder_area);
        else {
            Polygon extruer_poly;
            extruer_poly.points = to_points(extruder_area);
            Polygons result_polygon = intersection(extruer_poly, result);
            result = result_polygon[0];
        }
    }
    return result;
}
Points get_bed_shape(const DynamicPrintConfig &config, bool use_share)
{
    const ConfigOptionPoints *bed_shape_opt = config.opt<ConfigOptionPoints>("printable_area");
    if (!bed_shape_opt) {

        // Here, it is certain that the bed shape is missing, so an infinite one
        // has to be used, but still, the center of bed can be queried
        if (auto center_opt = config.opt<ConfigOptionPoint>("center"))
            return { scaled(center_opt->value) };

        return {};
    }

    Polygon bed_poly;
    if (use_share) {
        const ConfigOptionPointsGroups *extruder_area_opt = config.opt<ConfigOptionPointsGroups>("extruder_printable_area");
        if (extruder_area_opt && (extruder_area_opt->size() > 0)) {
            const std::vector<Pointfs>& extruder_areas = extruder_area_opt->values;
            bed_poly = get_shared_poly(extruder_areas);
        }
        else
            bed_poly.points = to_points(bed_shape_opt->values);
    }
    else
        bed_poly.points = to_points(bed_shape_opt->values);

    return bed_poly.points;
}

Points get_bed_shape(const PrintConfig &cfg, bool use_share)
{
    Polygon bed_poly;
    if (use_share) {
        const std::vector<Pointfs>& extruder_areas = cfg.extruder_printable_area.values;
        if (extruder_areas.size() > 0) {
            bed_poly = get_shared_poly(extruder_areas);
        }
        else
            bed_poly.points = to_points(cfg.printable_area.values);
    }
    else
        bed_poly.points = to_points(cfg.printable_area.values);

    return bed_poly.points;
}

Points get_bed_shape(const SLAPrinterConfig &cfg) { return to_points(cfg.printable_area.values); }

Polygon get_bed_shape_with_excluded_area(const PrintConfig& cfg, bool use_share)
{
    Polygon bed_poly;
    bed_poly.points = get_bed_shape(cfg, use_share);

    Points excluse_area_points = to_points(cfg.bed_exclude_area.values);
    Polygons exclude_polys;
    Polygon exclude_poly;
    for (int i = 0; i < excluse_area_points.size(); i++) {
        auto pt = excluse_area_points[i];
        exclude_poly.points.emplace_back(pt);
        if (i % 4 == 3) {  // exclude areas are always rectangle
            exclude_polys.push_back(exclude_poly);
            exclude_poly.points.clear();
        }
    }
    auto tmp = diff({ bed_poly }, exclude_polys);
    if (!tmp.empty()) bed_poly = tmp[0];
    return bed_poly;
}
bool has_skirt(const DynamicPrintConfig& cfg)
{
    auto opt_skirt_height = cfg.option("skirt_height");
    auto opt_skirt_loops = cfg.option("skirt_loops");
    auto opt_draft_shield = cfg.option("draft_shield");
    return (opt_skirt_height && opt_skirt_height->getInt() > 0 && opt_skirt_loops && opt_skirt_loops->getInt() > 0)
        || (opt_draft_shield && opt_draft_shield->getInt() != dsDisabled);
}
float get_real_skirt_dist(const DynamicPrintConfig& cfg) {
    return has_skirt(cfg) ? cfg.opt_float("skirt_distance") : 0;
}
} // namespace Slic3r

#include <cereal/types/polymorphic.hpp>
CEREAL_REGISTER_TYPE(Slic3r::DynamicPrintConfig)
CEREAL_REGISTER_POLYMORPHIC_RELATION(Slic3r::DynamicConfig, Slic3r::DynamicPrintConfig)
