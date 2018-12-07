#ifndef _DG_POINTER_SUBGRAPH_H_
#define _DG_POINTER_SUBGRAPH_H_

#include "dg/ADT/Queue.h"
#include "dg/analysis/SubgraphNode.h"
#include "dg/analysis/CallGraph.h"
#include "dg/analysis/PointsTo/PSNode.h"
#include "dg/analysis/BFS.h"

#include <cassert>
#include <cstdarg>
#include <vector>
#include <string>
#include <memory>

namespace dg {
namespace analysis {
namespace pta {

class PointerGraph;

/*
class PGBBlock {
};
*/

class PointerSubgraph
{
    PointerGraph *_PG;
    // root of the pointer state subgraph
    PSNode *_root{nullptr};
    // unified return node of the subgraph
    PSNode *_ret{nullptr};
    // node representing ...
    PSNode *_vararg{nullptr};

    const std::string _name{};

    // graph has a vector of all nodes and subgraph
    // keeps the structure of the nodes (in basic blocks)
    //std::vector<std::unique_ptr<PGBBlock>> _blocks;

public:
    PointerSubgraph(PointerGraph *graph, const std::string& name ="")
    : _PG(graph), _name(name) {};
    PointerSubgraph(PointerSubgraph&&) = default;
    PointerSubgraph& operator=(PointerSubgraph&&) = default;
    PointerSubgraph(const PointerSubgraph&) = delete;
    PointerSubgraph operator=(const PointerSubgraph&) = delete;

    PSNode *getRoot() { return _root; }
    PSNode *getRet() { return _ret; }
    PSNode *getVararg() { return _vararg; }

    const PSNode *getRoot() const { return _root; }
    const PSNode *getRet() const { return _ret; }
    const PSNode *getVararg() const { return _vararg; }

    void setRoot(PSNode *r) { _root = r; }
    void setRet(PSNode *r) { _ret = r; }
    void setVararg(PSNode *v) { _vararg = v; }
};

///
// Graph for pointer analysis.
// This graph contains a set of (sub)graphs, one for each function
// and the list of nodes in all the graphs.
class PointerGraph {
public:
    using NodesT = std::vector<std::unique_ptr<PSNode>>;
    using SubgraphsT = std::vector<std::unique_ptr<PointerSubgraph>>;

    PointerGraph() {
        // nodes[0] represents invalid node (the node with id 0)
        _nodes.emplace_back(nullptr);
    }

    bool registerCall(PSNode *a, PSNode *b) {
        return _callGraph.addCall(a, b);
    }

    const GenericCallGraph<PSNode *>& getCallGraph() const { return _callGraph; }

    const NodesT& getNodes() const { return _nodes; }
    size_t size() const { return _nodes.size(); }

    // FIXME: this is just a hack until we have basic blocks
    void setRoot(PSNode *e) {
        if (!_entry)
            _entry = createSubgraph("entry");
        _entry->setRoot(e);
    }

    void setEntry(PointerSubgraph *e) { _entry = e; }
    PointerSubgraph *getEntry() { return _entry; }
    const PointerSubgraph *getEntry() const { return _entry; }

    void remove(PSNode *nd) {
        assert(nd && "nullptr passed as nd");
        // the node must be isolated
        assert(nd->successors.empty() && "The node is still in graph");
        assert(nd->predecessors.empty() && "The node is still in graph");
        assert(nd->getID() < size() && "Invalid ID");
        assert(nd->getID() > 0 && "Invalid ID");
        assert(nd->users.empty() && "This node is used by other nodes");
        // if the node has operands, it means that the operands
        // have a reference (an user edge to this node).
        // We do not want to create dangling references.
        assert(nd->operands.empty() && "This node uses other nodes");
        assert(_nodes[nd->getID()].get() == nd && "Inconsistency in nodes");

        // clear the nodes entry
        _nodes[nd->getID()].reset();
    }

    PointerSubgraph *createSubgraph(const std::string& name) {
        _subgraphs.emplace_back(new PointerSubgraph(this, name));
        if (_subgraphs.size() == 1)
            setEntry(_subgraphs.back().get());
        return _subgraphs.back().get();
    }

    PSNode *create(PSNodeType t, ...) {
        va_list args;
        PSNode *node = nullptr;

        va_start(args, t);
        switch (t) {
            case PSNodeType::ALLOC:
            case PSNodeType::DYN_ALLOC:
                node = new PSNodeAlloc(getNewNodeId(), t);
                break;
            case PSNodeType::GEP:
                node = new PSNodeGep(getNewNodeId(),
                                     va_arg(args, PSNode *),
                                     va_arg(args, Offset::type));
                break;
            case PSNodeType::MEMCPY:
                node = new PSNodeMemcpy(getNewNodeId(),
                                        va_arg(args, PSNode *),
                                        va_arg(args, PSNode *),
                                        va_arg(args, Offset::type));
                break;
            case PSNodeType::CONSTANT:
                node = new PSNode(getNewNodeId(), PSNodeType::CONSTANT,
                                  va_arg(args, PSNode *),
                                  va_arg(args, Offset::type));
                break;
            case PSNodeType::ENTRY:
                node = new PSNodeEntry(getNewNodeId());
                break;
            case PSNodeType::CALL:
                node = new PSNodeCall(getNewNodeId());
                break;
            default:
                node = new PSNode(getNewNodeId(), t, args);
                break;
        }
        va_end(args);

        assert(node && "Didn't created node");
        _nodes.emplace_back(node);
        return node;
    }

    // get nodes in BFS order and store them into
    // the container
    template <typename ContainerOrNode>
    std::vector<PSNode *> getNodes(const ContainerOrNode& start,
                                   unsigned expected_num = 0)
    {
        ++dfsnum;

        std::vector<PSNode *> cont;
        if (expected_num != 0)
            cont.reserve(expected_num);

        struct DfsIdTracker {
            const unsigned dfsnum;
            DfsIdTracker(unsigned dnum) : dfsnum(dnum) {}

            void visit(PSNode *n) { n->dfsid = dfsnum; }
            bool visited(PSNode *n) const { return n->dfsid == dfsnum; }
        };

        DfsIdTracker visitTracker(dfsnum);
        BFS<PSNode, DfsIdTracker> bfs(visitTracker);

        bfs.run(start, [&cont](PSNode *n) { cont.push_back(n); });

        return cont;
    }

private:

    // TODO: get rid of me!
    unsigned int dfsnum{0};

    // Take care of assigning ids to new nodes
    unsigned int last_node_id = 0;
    unsigned int getNewNodeId() {
        return ++last_node_id;
    }

    PointerSubgraph *_entry{nullptr};
    NodesT _nodes; // nodes from all graphs
    SubgraphsT _subgraphs; // all subgraphs

    // call-graph for this graph
    GenericCallGraph<PSNode *> _callGraph;
};


///
// get nodes reachable from n (including n),
// stop at node 'exit' (excluding) if not set to null
template <typename NodeT>
std::set<NodeT *>
getReachableNodes(NodeT *n,
                  NodeT *exit = nullptr)
{
    ADT::QueueFIFO<NodeT *> fifo;
    std::set<NodeT *> cont;

    assert(n && "No starting node given.");
    fifo.push(n);

    while (!fifo.empty()) {
        auto cur = fifo.pop();
        if (!cont.insert(cur).second)
            continue; // we already visited this node

        for (PSNode *succ : cur->getSuccessors()) {
            assert(succ != nullptr);

            if (succ == exit)
                continue;

            fifo.push(succ);
        }
    }

    return cont;
}

} // namespace pta
} // namespace analysis
} // namespace dg

#endif
