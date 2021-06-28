#include "SpeedGenerator.hpp"
#include "libslic3r/ExtrusionEntity.hpp"
#include "libslic3r/utils.hpp"

#define BOOST_SPIRIT_THREADSAFE
#include <boost/property_tree/ptree.hpp>
#include <boost/property_tree/json_parser.hpp>
#include <boost/date_time.hpp>
#include <boost/foreach.hpp>

namespace Slic3r {

SpeedGenerator::SpeedGenerator() {
	// default is max speed
	for (int i = 0; i < 11; i++) {
		for (int j = 0; j < 11; j++) {
			speed_ratio_table[i][j] = 100;
		}
	}
	std::string config_file = resources_dir() + "/PerimeterSpeedConfig.json";
	boost::property_tree::read_json<boost::property_tree::ptree>(config_file, root);
	if (root.count("speed_ratio_table")) {
		int i = 0;
		for (auto& array11 : root.get_child("speed_ratio_table")) {
			int j = 0;
			for (auto& it : array11.second) {
				speed_ratio_table[i][j] = it.second.get_value<int>();
				j++;
			}
			i++;
		}

	}
}

double SpeedGenerator::calculate_speed(const ExtrusionPath& path, double max_speed, double min_speed) {
	assert(max_speed > min_speed);
	int overhang_degree = path.get_overhang_degree();
	int curva_degree = path.get_curve_degree();
	assert(overhang_degree >= 0 && overhang_degree <= 10);
	assert(curva_degree >= 0 && curva_degree <= 10);
	double speed = min_speed + ((double)speed_ratio_table[overhang_degree][curva_degree]) * (max_speed - min_speed) / 100;
	return speed;
}

}