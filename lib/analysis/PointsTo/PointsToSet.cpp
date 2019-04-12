#include "dg/analysis/PointsTo/PointsToSet.h"
#include "dg/analysis/PointsTo/PSNode.h"
#include "dg/analysis/PointsTo/Pointer.h"

#include <vector>
#include <map>

namespace dg {
namespace analysis {
namespace pta {
    std::vector<PSNode*> SeparateOffsetsPointsToSet::idVector;
    std::vector<Pointer> SingleBitvectorPointsToSet::idVector;
    std::vector<PSNode*> SmallOffsetsPointsToSet::idVector;
    std::vector<PSNode*> DivisibleOffsetsPointsToSet::idVector;
    std::map<PSNode*,size_t> SeparateOffsetsPointsToSet::ids;
    std::map<Pointer,size_t> SingleBitvectorPointsToSet::ids;
    std::map<PSNode*,size_t> SmallOffsetsPointsToSet::ids;
    std::map<PSNode*,size_t> DivisibleOffsetsPointsToSet::ids;
} // namespace pta
} // namespace analysis
} // namespace debug