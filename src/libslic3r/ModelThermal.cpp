#include "Model.hpp"

#include <algorithm>

namespace Slic3r {

std::map<size_t, ExtruderParams> Model::extruderParamsMap = {{0, {"", 0, 0}}};
GlobalSpeedMap Model::printSpeedMap{};

double Model::findMaxSpeed(const ModelObject *object)
{
    auto   objectKeys  = object->config.keys();
    double objMaxSpeed = -1.;
    if (objectKeys.empty())
        return Model::printSpeedMap.maxSpeed;
    double perimeterSpeedObj         = Model::printSpeedMap.perimeterSpeed;
    double externalPerimeterSpeedObj = Model::printSpeedMap.externalPerimeterSpeed;
    double infillSpeedObj            = Model::printSpeedMap.infillSpeed;
    double solidInfillSpeedObj       = Model::printSpeedMap.solidInfillSpeed;
    double topSolidInfillSpeedObj    = Model::printSpeedMap.topSolidInfillSpeed;
    double supportSpeedObj           = Model::printSpeedMap.supportSpeed;
    double smallPerimeterSpeedObj    = Model::printSpeedMap.smallPerimeterSpeed;
    for (std::string objectKey : objectKeys) {
        if (objectKey == "inner_wall_speed") {
            perimeterSpeedObj         = object->config.get().opt_float_nullable(objectKey, 0);
            externalPerimeterSpeedObj = Model::printSpeedMap.externalPerimeterSpeed / Model::printSpeedMap.perimeterSpeed * perimeterSpeedObj;
        }
        if (objectKey == "sparse_infill_speed")
            infillSpeedObj = object->config.get().opt_float_nullable(objectKey, 0);
        if (objectKey == "internal_solid_infill_speed")
            solidInfillSpeedObj = object->config.get().opt_float_nullable(objectKey, 0);
        if (objectKey == "top_surface_speed")
            topSolidInfillSpeedObj = object->config.get().opt_float_nullable(objectKey, 0);
        if (objectKey == "support_speed")
            supportSpeedObj = object->config.get().opt_float_nullable(objectKey, 0);
        if (objectKey == "outer_wall_speed")
            externalPerimeterSpeedObj = object->config.get().opt_float_nullable(objectKey, 0);
        if (objectKey == "small_perimeter_speed")
            smallPerimeterSpeedObj = object->config.get().option<ConfigOptionFloatsOrPercentsNullable>(objectKey)->get_at(0).get_abs_value(externalPerimeterSpeedObj);
    }
    objMaxSpeed = std::max(perimeterSpeedObj, std::max(externalPerimeterSpeedObj, std::max(infillSpeedObj, std::max(solidInfillSpeedObj, std::max(topSolidInfillSpeedObj, std::max(supportSpeedObj, std::max(smallPerimeterSpeedObj, objMaxSpeed)))))));
    if (objMaxSpeed <= 0)
        objMaxSpeed = 250.;
    return objMaxSpeed;
}

double Model::getThermalLength(const ModelVolume *modelVolumePtr)
{
    double thermalLength = 200.;
    auto   aa            = modelVolumePtr->extruder_id();
    if (Model::extruderParamsMap.find(aa) != Model::extruderParamsMap.end()) {
        if (Model::extruderParamsMap.at(aa).materialName == "ABS" || Model::extruderParamsMap.at(aa).materialName == "PA-CF" ||
            Model::extruderParamsMap.at(aa).materialName == "PET-CF")
            thermalLength = 100;
        if (Model::extruderParamsMap.at(aa).materialName == "PC")
            thermalLength = 40;
        if (Model::extruderParamsMap.at(aa).materialName == "TPU" || Model::extruderParamsMap.at(aa).materialName == "TPU-AMS")
            thermalLength = 1000;
    }
    return thermalLength;
}

double Model::getThermalLength(const std::vector<ModelVolume *> modelVolumePtrs)
{
    double thermalLength = 1250.;

    for (const auto &modelVolumePtr : modelVolumePtrs)
        if (modelVolumePtr != nullptr)
            thermalLength = std::min(thermalLength, getThermalLength(modelVolumePtr));
    return thermalLength;
}

} // namespace Slic3r
