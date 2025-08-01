#ifndef slic3r_GUI_AMSMaterialManager_hpp_
#define slic3r_GUI_AMSMaterialManager_hpp_

#include <map>
#include <set>
#include <vector>
#include <string>

namespace Slic3r { namespace GUI {

class AmsTray;
class MachineObject;

// Structure to represent complete material identity
struct MaterialIdentity {
    std::string brand;
    std::string type;
    std::string color;
    
    bool operator<(const MaterialIdentity& other) const {
        if (brand != other.brand) return brand < other.brand;
        if (type != other.type) return type < other.type;
        return color < other.color;
    }
    
    bool operator==(const MaterialIdentity& other) const {
        return brand == other.brand && type == other.type && color == other.color;
    }
};

class AMSMaterialManager {
public:
    // Build backup groups based on identical materials (same brand, type, and color)
    static std::vector<std::vector<int>> BuildIdenticalMaterialGroups(MachineObject* obj);
    
    // Get material identity from AmsTray
    static MaterialIdentity GetMaterialIdentity(const AmsTray* tray);
    
    // Check if two materials are identical (same brand, type, and color)
    static bool AreMaterialsIdentical(const AmsTray* tray1, const AmsTray* tray2);
    
    // Convert backup groups to filam_bak format
    static std::vector<int> ConvertGroupsToFilamBak(const std::vector<std::vector<int>>& groups);
    
private:
    // Helper to create bit mask for backup group
    static int CreateBackupMask(const std::vector<int>& tray_ids);
};

}} // namespace Slic3r::GUI

#endif // slic3r_GUI_AMSMaterialManager_hpp_