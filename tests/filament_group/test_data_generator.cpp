#include "fg_test_utils.hpp"
#include "fg_test_serialization.hpp"

#include <filesystem>
#include <iostream>
#include <fstream>
#include <string>
#include <vector>

namespace fs = std::filesystem;
using namespace Slic3r;
using namespace Slic3r::FGTest;

struct CaseSpec {
    std::string category;
    int num_filaments_min;
    int num_filaments_max;
    int num_layers_min;
    int num_layers_max;
    bool chaotic;
    bool with_constraints;
    FGMode mode;
    FGStrategy strategy;
    bool group_with_time;
};

static void generate_cases_for_config(const std::string& config_type,
                                       const std::string& output_dir,
                                       int config_id,
                                       const std::vector<std::pair<CaseSpec, int>>& specs) {
    fs::create_directories(output_dir);
    int case_idx = 0;

    for (auto& [spec, count] : specs) {
        for (int i = 0; i < count; ++i) {
            int seed = config_id * 10000 + case_idx;

            TestRng param_rng(seed);
            int num_filaments = param_rng.rand_int(spec.num_filaments_min, spec.num_filaments_max);
            int num_layers = param_rng.rand_int(spec.num_layers_min, spec.num_layers_max);

            std::string id = config_type + "_" + spec.category + "_" +
                             std::to_string(case_idx);

            auto tc = build_test_case(id, config_type, seed,
                                       num_filaments, num_layers,
                                       spec.chaotic, spec.with_constraints,
                                       spec.mode, spec.strategy, spec.group_with_time);

            std::string filename = output_dir + "/" + spec.category + "_" +
                                    std::to_string(case_idx) + ".json";
            save_test_case(filename, tc);

            case_idx++;
        }
    }
    std::cout << "Generated " << case_idx << " cases for config " << config_type
              << " in " << output_dir << std::endl;
}

int main(int argc, char* argv[]) {
    std::string base_dir = FG_TEST_DATA_DIR;
    if (argc > 1)
        base_dir = argv[1];

    std::cout << "Generating test data to: " << base_dir << std::endl;

    // ============ Config A: 2 extruders, 2 nozzles ============
    {
        std::vector<std::pair<CaseSpec, int>> specs = {
            // basic: random with interval characteristics
            {{"basic", 2, 6, 100, 500, false, false,
              FGMode::FlushMode, FGStrategy::BestCost, false}, 50},
            // stress: more filaments, more layers
            {{"stress", 7, 10, 1000, 2000, false, false,
              FGMode::FlushMode, FGStrategy::BestCost, false}, 35},
            // constraint: with unprintable limits
            {{"constraint", 3, 8, 100, 500, false, true,
              FGMode::FlushMode, FGStrategy::BestCost, false}, 35},
            // edge: minimal filaments or chaotic layers
            {{"edge", 2, 3, 10, 50, true, false,
              FGMode::FlushMode, FGStrategy::BestCost, false}, 13},
            {{"edge_uniform", 2, 6, 100, 500, false, false,
              FGMode::FlushMode, FGStrategy::BestCost, false}, 12},
            // mode variants
            {{"mode_match", 3, 8, 100, 500, false, false,
              FGMode::MatchMode, FGStrategy::BestCost, false}, 10},
            {{"mode_bestfit", 3, 8, 100, 500, false, false,
              FGMode::FlushMode, FGStrategy::BestFit, false}, 8},
            {{"mode_time", 4, 8, 100, 500, false, false,
              FGMode::FlushMode, FGStrategy::BestCost, true}, 7},
        };
        generate_cases_for_config("A", base_dir + "/config_a", 1, specs);
    }

    // ============ Config B: 2 extruders, 1+K nozzles ============
    {
        std::vector<std::pair<CaseSpec, int>> specs = {
            {{"basic", 3, 8, 100, 500, false, false,
              FGMode::FlushMode, FGStrategy::BestCost, false}, 50},
            {{"stress", 9, 14, 1000, 2000, false, false,
              FGMode::FlushMode, FGStrategy::BestCost, false}, 35},
            {{"constraint", 4, 10, 100, 500, false, true,
              FGMode::FlushMode, FGStrategy::BestCost, false}, 35},
            {{"edge", 2, 4, 10, 50, true, false,
              FGMode::FlushMode, FGStrategy::BestCost, false}, 13},
            {{"edge_uniform", 3, 8, 100, 500, false, false,
              FGMode::FlushMode, FGStrategy::BestCost, false}, 12},
            {{"mode_match", 4, 10, 100, 500, false, false,
              FGMode::MatchMode, FGStrategy::BestCost, false}, 10},
            {{"mode_bestfit", 4, 10, 100, 500, false, false,
              FGMode::FlushMode, FGStrategy::BestFit, false}, 8},
            {{"mode_time", 4, 10, 100, 500, false, false,
              FGMode::FlushMode, FGStrategy::BestCost, true}, 7},
        };
        generate_cases_for_config("B", base_dir + "/config_b", 2, specs);
    }

    // ============ Config C: 1 extruder, up to 9 nozzles ============
    {
        std::vector<std::pair<CaseSpec, int>> specs = {
            {{"basic", 3, 9, 100, 500, false, false,
              FGMode::FlushMode, FGStrategy::BestCost, false}, 50},
            {{"stress", 12, 20, 1000, 2000, false, false,
              FGMode::FlushMode, FGStrategy::BestCost, false}, 30},
            {{"constraint", 4, 12, 100, 500, false, true,
              FGMode::FlushMode, FGStrategy::BestCost, false}, 30},
            {{"edge", 2, 4, 10, 50, true, false,
              FGMode::FlushMode, FGStrategy::BestCost, false}, 13},
            {{"edge_uniform", 3, 9, 100, 500, false, false,
              FGMode::FlushMode, FGStrategy::BestCost, false}, 12},
            {{"mode_match", 4, 12, 100, 500, false, false,
              FGMode::MatchMode, FGStrategy::BestCost, false}, 10},
            {{"mode_bestfit", 4, 12, 100, 500, false, false,
              FGMode::FlushMode, FGStrategy::BestFit, false}, 8},
            {{"mode_time", 4, 12, 100, 500, false, false,
              FGMode::FlushMode, FGStrategy::BestCost, true}, 7},
        };
        generate_cases_for_config("C", base_dir + "/config_c", 3, specs);
    }

    std::cout << "Done. Total ~500 cases generated." << std::endl;
    return 0;
}
