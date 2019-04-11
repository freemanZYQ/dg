#include "dg/analysis/PointsTo/PointsToSet.h"
#include "dg/analysis/PointsTo/PSNode.h"
#include "dg/analysis/PointsTo/Pointer.h"

#include <vector>
#include <map>

namespace dg {
namespace analysis {
namespace pta {
    std::vector<PSNode*> BitvectorPointsToSet::idVector;
    std::vector<Pointer> BitvectorPointsToSet2::idVector;
    std::vector<PSNode*> BitvectorPointsToSet3::idVector;
    std::vector<PSNode*> BitvectorPointsToSet4::idVector;
    std::map<PSNode*,size_t> BitvectorPointsToSet::ids;
    std::map<Pointer,size_t> BitvectorPointsToSet2::ids;
    std::map<PSNode*,size_t> BitvectorPointsToSet3::ids;
    std::map<PSNode*,size_t> BitvectorPointsToSet4::ids;
} // namespace pta
} // namespace analysis
} // namespace debug