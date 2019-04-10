#ifndef DG_RD_NODE_H_
#define DG_RD_NODE_H_

#include <vector>

#include "dg/analysis/Offset.h"
#include "dg/analysis/SubgraphNode.h"

#include "dg/analysis/ReachingDefinitions/ReachingDefinitionsAnalysisOptions.h"
#include "dg/analysis/ReachingDefinitions/RDMap.h"

namespace dg {
namespace analysis {
namespace rd {

class RDNode;
class ReachingDefinitionsAnalysis;

// here the types are for type-checking (optional - user can do it
// when building the graph) and for later optimizations
enum class RDNodeType {
        // invalid type of node
        NONE,
        // these are nodes that just represent memory allocation sites
        // we need to have them even in reaching definitions analysis,
        // so that we can use them as targets in DefSites
        ALLOC,
        DYN_ALLOC,
        // nodes that write the memory
        STORE,
        // nodes that use the memory
        LOAD,
        // merging information from several locations
        PHI,
        // return from the subprocedure
        RETURN,
        // call node
        CALL,
        // return from the call (in caller)
        CALL_RETURN,
        FORK,
        JOIN,
        // dummy nodes
        NOOP
};

extern RDNode *UNKNOWN_MEMORY;

class RDBBlock;

class RDNode : public SubgraphNode<RDNode> {

    RDNodeType type{RDNodeType::NONE};

    RDBBlock *bblock{nullptr};
    // marks for DFS/BFS
    unsigned int dfsid{0};

public:

    // for invalid nodes like UNKNOWN_MEMLOC
    RDNode(RDNodeType t = RDNodeType::NONE)
    : SubgraphNode<RDNode>(0), type(t) {}

    RDNode(unsigned id, RDNodeType t = RDNodeType::NONE)
    : SubgraphNode<RDNode>(id), type(t) {}

    virtual ~RDNode() = default;

    // weak update
    DefSiteSetT defs;
    // strong update
    DefSiteSetT overwrites;

    // this is set of variables used in this node
    DefSiteSetT uses;

    RDNodeType getType() const { return type; }
    DefSiteSetT& getDefines() { return defs; }
    DefSiteSetT& getOverwrites() { return overwrites; }
    DefSiteSetT& getUses() { return uses; }
    const DefSiteSetT& getDefines() const { return defs; }
    const DefSiteSetT& getOverwrites() const { return defs; }
    const DefSiteSetT& getUses() const { return uses; }

    bool defines(RDNode *target, const Offset& off = Offset::UNKNOWN) const
    {
        // FIXME: this is not efficient implementation,
        // use the ordering on the nodes
        // (see old DefMap.h in llvm/)
        if (off.isUnknown()) {
            for (const DefSite& ds : defs)
                if (ds.target == target)
                    return true;
        } else {
            for (const DefSite& ds : defs)
                if (ds.target == target
                    && off.inRange(*ds.offset, *ds.offset + *ds.len))
                    return true;

            for (const DefSite& ds : overwrites)
                if (ds.target == target
                    && off.inRange(*ds.offset, *ds.offset + *ds.len))
                    return true;
        }

        return false;
    }

    void addUse(RDNode *target,
                const Offset& off = Offset::UNKNOWN,
                const Offset& len = Offset::UNKNOWN) {
        addUse(DefSite(target, off, len));
    }

    void addUse(const DefSite& ds) { uses.insert(ds); }

    template <typename T>
    void addUses(T&& u)
    {
        for (auto& ds : u) {
            uses.insert(ds);
        }
    }

    void addDef(const DefSite& ds, bool strong_update = false)
    {
        if (strong_update)
            overwrites.insert(ds);
        else
            defs.insert(ds);
    }

    ///
    // register that the node defines the memory 'target'
    // at offset 'off' of length 'len', i.e. it writes
    // to memory 'target' to bytes [off, off + len].
    void addDef(RDNode *target,
                const Offset& off = Offset::UNKNOWN,
                const Offset& len = Offset::UNKNOWN,
                bool strong_update = false)
    {
        addDef(DefSite(target, off, len), strong_update);
    }

    template <typename T>
    void addDefs(T&& defs)
    {
        for (auto& ds : defs) {
            addDef(ds);
        }
    }

    void addOverwrites(RDNode *target,
                       const Offset& off = Offset::UNKNOWN,
                       const Offset& len = Offset::UNKNOWN)
    {
        addOverwrites(DefSite(target, off, len));
    }

    void addOverwrites(const DefSite& ds)
    {
        overwrites.insert(ds);
    }

    bool isOverwritten(const DefSite& ds)
    {
        return overwrites.find(ds) != overwrites.end();
    }

    bool isUnknown() const
    {
        return this == UNKNOWN_MEMORY;
    }

    bool isUse() const { return !uses.empty(); }

    RDBBlock *getBBlock() { return bblock; }
    void setBBlock(RDBBlock *bb) { bblock = bb; }

    friend class ReachingDefinitionsAnalysis;
};

///
// Data-flow RD node
class DFRDNode : public RDNode {
    friend class ReachingDefinitionsAnalysis;

public:

    DFRDNode(RDNodeType t = RDNodeType::NONE)
    : RDNode(0, t){}

    DFRDNode(unsigned id, RDNodeType t = RDNodeType::NONE)
    : RDNode(id, t){}

    static DFRDNode *get(RDNode *n) {
        assert(n->getType() != RDNodeType::NONE);
        assert(n != UNKNOWN_MEMORY);
        return static_cast<DFRDNode *>(n);
    }

    // state of the data-flow analysis
    RDMap def_map{};
};

///
// MemorySSA RD node
class SSARDNode : public RDNode {
    friend class SSAReachingDefinitionsAnalysis;

    class DefUses {
        using T = std::vector<RDNode *>;
        T defuse;
    public:
        bool add(RDNode *d) {
            for (auto x : defuse) {
                if (x == d) {
                    return false;
                }
            }
            defuse.push_back(d);
            return true;
        }

        template <typename Cont>
        bool add(const Cont& C) {
            bool changed = false;
            for (RDNode *n : C)
                changed |= add(n);
            return changed;
        }

        operator std::vector<RDNode *>() { return defuse; }

        T::iterator begin() { return defuse.begin(); }
        T::iterator end() { return defuse.end(); }
        T::const_iterator begin() const { return defuse.begin(); }
        T::const_iterator end() const { return defuse.end(); }
    };

public:

    SSARDNode(RDNodeType t = RDNodeType::NONE)
    : RDNode(0, t){}

    SSARDNode(unsigned id, RDNodeType t = RDNodeType::NONE)
    : RDNode(id, t){}

    static SSARDNode *get(RDNode *n) {
        assert(n->getType() != RDNodeType::NONE);
        assert(n != UNKNOWN_MEMORY);
        return static_cast<SSARDNode *>(n);
    }

    // places where this node is defined
    // (so this node has non-empty uses)
    DefUses defuse{};
};

} // namespace rd
} // namespace analysis
} // namespace dg

#endif //  DG_RD_NODE_H_
