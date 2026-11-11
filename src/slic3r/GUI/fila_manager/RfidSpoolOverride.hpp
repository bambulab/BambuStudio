#pragma once
#include <map>
#include <string>
#include <nlohmann/json.hpp>
#include "libslic3r/PresetBundle.hpp"

namespace Slic3r { namespace GUI {

// This is a local overlay. Never serialize it into a cloud spool record or
// change a DevAmsTray: those remain the original printer-reported identity.
struct RfidSpoolOverride {
    std::string material_type;
    std::string color;
    std::map<std::string, std::string> profiles; // exact printer preset -> exact filament preset
};

class RfidSpoolOverrides {
public:
    explicit RfidSpoolOverrides(std::string path);
    static RfidSpoolOverrides& instance();
    static bool valid_uuid(const std::string& uuid);
    static bool valid_color(const std::string& color);
    const RfidSpoolOverride* find(const std::string& uuid) const;
    bool set(const std::string& uuid, const RfidSpoolOverride& value, std::string& error);
    bool remove(const std::string& uuid, std::string& error);
    const std::string& load_error() const { return m_error; }
private:
    bool save(const std::map<std::string, RfidSpoolOverride>& values, std::string& error);
    std::string m_path, m_error;
    std::map<std::string, RfidSpoolOverride> m_values;
};

// A nonempty error means an explicit assignment exists but cannot be applied.
// Callers must surface it rather than fall back silently to the tag's profile.
const Preset* resolve_rfid_override(const RfidSpoolOverride& value, PresetBundle& bundle,
                                   const std::string& reported_type, double nozzle_diameter,
                                   std::string& error);

}} // namespace
