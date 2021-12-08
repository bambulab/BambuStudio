#ifndef slic3r_FaceDetector_hpp_
#define slic3r_FaceDetector_hpp_

namespace Slic3r {
class ModelObject;

class FaceDetector {
public:
	FaceDetector(ModelObject* mo, double sample_interval)
		: m_mo(mo), m_sample_interval(sample_interval) {}

	// Add eExteriorAppearance to exterior facets in m_mo
	void detect_exterior_face();

private:
	ModelObject* m_mo;
	double m_sample_interval;
};

}

#endif // #ifndef slic3r_FaceDetector_hpp_
