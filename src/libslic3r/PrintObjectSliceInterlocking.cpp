#include "PrintObjectSliceInterlocking.hpp"

#include "Interlocking/InterlockingGenerator.hpp"

namespace Slic3r {

void apply_interlocking_features(PrintObject& print_object)
{
    InterlockingGenerator::generate_interlocking_structure(&print_object);
    print_object.print()->throw_if_canceled();

    InterlockingGenerator::generate_embedding_wall(&print_object);
    print_object.print()->throw_if_canceled();
}

} // namespace Slic3r
