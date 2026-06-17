#include "PrintObjectSliceInterlocking.hpp"

#include "Interlocking/InterlockingGenerator.hpp"

namespace Slic3r {

void apply_interlocking_features(PrintObject& print_object, const std::function<void()>& throw_if_canceled)
{
    InterlockingGenerator::generate_interlocking_structure(&print_object);
    throw_if_canceled();

    InterlockingGenerator::generate_embedding_wall(&print_object);
    throw_if_canceled();
}

} // namespace Slic3r
