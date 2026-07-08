#ifndef slic3r_PositionConfigImport_hpp_
#define slic3r_PositionConfigImport_hpp_

#include "libslic3r/Config.hpp"
#include "libslic3r/Model.hpp"

namespace Slic3r { namespace GUI {

class PartPlateList;

enum class PositionConfigImportError {
    NoError = 0,
    FileOpenFailed,
    ParseFailed,
    InvalidFormat,
    PrinterModelMismatch,
};

struct PositionConfigImportResult {
    PositionConfigImportError error = PositionConfigImportError::NoError;
    std::string               message;
    int                       applied = 0;
    int                       missing = 0;
};

PositionConfigImportResult import_position_config_from_file(
    Model&              model,
    PartPlateList&      plate_list,
    DynamicPrintConfig& config,
    const std::string&  json_path,
    const std::string&  expected_printer_model);

}} // namespace Slic3r::GUI

#endif // slic3r_PositionConfigImport_hpp_
