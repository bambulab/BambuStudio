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
	// limit the speed in case of F0 generated in gcode when user set 0 speed in UI
	// which cause printer stopped. 1mm/s is slow enough and can make printer not really stopped.
	max_speed = max_speed < 1 ? 1 : max_speed;
	min_speed = min_speed < 1 ? 1 : min_speed;
	// switch min and max speed if user set the max speed to be slower than min_speed
	if (max_speed < min_speed) {
		double temp = max_speed;
		max_speed = min_speed;
		min_speed = temp;
	}

	int overhang_degree = path.get_overhang_degree();
	int curva_degree = path.get_curve_degree();
	assert(overhang_degree >= 0 && overhang_degree <= 10);
	assert(curva_degree >= 0 && curva_degree <= 10);
	double speed = min_speed + ((double)speed_ratio_table[overhang_degree][curva_degree]) * (max_speed - min_speed) / 100;
	return speed;
}

}