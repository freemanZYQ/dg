#ifndef _DG_POINTER_GRAPH_H_
#define _DG_POINTER_GRAPH_H_

#include "dg/ADT/Queue.h"
#include "dg/analysis/SubgraphNode.h"
#include "dg/analysis/CallGraph.h"
#include "dg/analysis/PointsTo/PSNode.h"
#include "dg/analysis/BFS.h"

#include <cassert>
#include <cstdarg>
#include <vector>
#include <memory>
#include <functional>

namespace dg {
namespace analysis {
namespace pta {

class PointerGraph;

// A single procedure in Pointer Graph
class PointerSubgraph {
    friend class PointerGraph;

    unsigned _id{0};

    PointerSubgraph(unsigned id, PSNode *r1, PSNode *va = nullptr)
        : _id(id), root(r1), vararg(va) {}

    PointerSubgraph() = default;
    PointerSubgraph(const PointerSubgraph&) = delete;

public:
    PointerSubgraph(PointerSubgraph&&) = default;

    unsigned getID() const { return _id; }

    // first nodes of the subgraph
	// FIXME: rename to 'entry'
    PSNode *root{nullptr};

	// return nodes of this graph
    std::set<PSNode *> returnNodes{};

    // this is the node where we gather the variadic-length arguments
    PSNode *vararg{nullptr};
};


///
// Basic graph for pointer analysis - contains CFG graphs for all procedures of the program.
class PointerGraph
{
    unsigned int dfsnum;

    // root of the pointer state subgraph
    PSNode *root;

    using NodesT = std::vector<std::unique_ptr<PSNode>>;
    using SubgraphsT = std::vector<std::unique_ptr<PointerSubgraph>>;

    NodesT nodes;
    SubgraphsT _subgraphs;

    // Take care of assigning ids to new nodes
    unsigned int last_node_id = 0;
    unsigned int getNewNodeId() {
        return ++last_node_id;
    }

    GenericCallGraph<PSNode *> callGraph;

    PSNode * _globalNodes;

public:
    PointerGraph() : dfsnum(0), root(nullptr) {
        // nodes[0] represents invalid node (the node with id 0)
        nodes.emplace_back(nullptr);
    }

    bool registerCall(PSNode *a, PSNode *b) {
        return callGraph.addCall(a, b);
    }

    const GenericCallGraph<PSNode *>& getCallGraph() const { return callGraph; }

    const NodesT& getNodes() const { return nodes; }
    size_t size() const { return nodes.size(); }

    PointerGraph(PointerGraph&&) = default;
    PointerGraph& operator=(PointerGraph&&) = default;
    PointerGraph(const PointerGraph&) = delete;
    PointerGraph operator=(const PointerGraph&) = delete;

    PSNode *getRoot() const { return root; }
    void setRoot(PSNode *r) {
#if DEBUG_ENABLED
        bool found = false;
        for (auto& n : nodes) {
            if (n.get() == r) {
                found = true;
                break;
            }
        }
        assert(found && "The root lies outside of the graph");
#endif
        root = r;
    }

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
        assert(nodes[nd->getID()].get() == nd && "Inconsistency in nodes");

        // clear the nodes entry
        nodes[nd->getID()].reset();
    }

    PointerSubgraph *createSubgraph(PSNode *root,
                                    PSNode *vararg = nullptr) {
        // NOTE: id of the subgraph is always index in _subgraphs + 1
        _subgraphs.emplace_back(new PointerSubgraph(_subgraphs.size() + 1,
                                                    root, vararg));
        return _subgraphs.back().get();
    }

    PSNode *create(PSNodeType t, ...) {
        va_list args;
        PSNode *node = nullptr;

        va_start(args, t);
        switch (t) {
            case PSNodeType::ALLOC:
                node = new PSNodeAlloc(getNewNodeId());
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
            case PSNodeType::FORK:
                node = new PSNodeFork(getNewNodeId());
                break;
            case PSNodeType::JOIN:
                node = new PSNodeJoin(getNewNodeId());
                break;
            case PSNodeType::RETURN:
                node = new PSNodeRet(getNewNodeId(), args);
                break;
            case PSNodeType::CALL_RETURN:
                node = new PSNodeCallRet(getNewNodeId(), args);
                break;
            default:
                node = new PSNode(getNewNodeId(), t, args);
                break;
        }
        va_end(args);

        assert(node && "Didn't created node");
        nodes.emplace_back(node);
        return node;
    }

    // set the first global. It is assumed that the globals are
    // connected by successors edges in the order in which they
    // should be processed
    void setGlobals(PSNode *n) {
        _globalNodes = n;
    }

    PSNode *firstGlobal() { return _globalNodes; }
    const PSNode *firstGlobal() const { return _globalNodes; }


    // get nodes in BFS order and store them into
    // the container
    template <typename ContainerOrNode>
    std::vector<PSNode *> getNodes(const ContainerOrNode& start,
                                   bool interprocedural = true,
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

         // iterate over successors and call (return) edges
        struct EdgeChooser {
            const bool interproc;
            EdgeChooser(bool inter = true) : interproc(inter) {}

            void foreach(PSNode *cur, std::function<void(PSNode *)> Dispatch) {
                if (interproc) {
                    if (PSNodeCall *C = PSNodeCall::get(cur)) {
                        for (auto subg : C->getCallees()) {
                            Dispatch(subg->root);
                        }
                        return; // we do not need to iterate over succesors now
                    } else if (PSNodeRet *R = PSNodeRet::get(cur)) {
                        for (auto ret : R->getReturnSites()) {
                            Dispatch(ret);
                        }
                        return; // we do not need to iterate over succesors now
                    }
                }

                for (auto s : cur->getSuccessors())
                    Dispatch(s);
            }
        };

        DfsIdTracker visitTracker(dfsnum);
        EdgeChooser chooser(interprocedural);
        BFS<PSNode, DfsIdTracker, EdgeChooser> bfs(visitTracker, chooser);

        bfs.run(start, [&cont](PSNode *n) { cont.push_back(n); });

        return cont;
    }

};

///
// get nodes reachable from n (including n),
// stop at node 'exit' (excluding) if not set to null
inline std::set<PSNode *>
getReachableNodes(PSNode *n,
                  PSNode *exit = nullptr,
				  bool interproc = true)
{
    ADT::QueueFIFO<PSNode *> fifo;
    std::set<PSNode *> cont;

    assert(n && "No starting node given.");
    fifo.push(n);

    while (!fifo.empty()) {
        PSNode *cur = fifo.pop();
        if (!cont.insert(cur).second)
            continue; // we already visited this node

        for (PSNode *succ : cur->getSuccessors()) {
            assert(succ != nullptr);

            if (succ == exit)
                continue;

            fifo.push(succ);
        }

        if (interproc) {
            if (PSNodeCall *C = PSNodeCall::get(cur)) {
                for (auto subg : C->getCallees()) {
                    if (subg->root == exit)
                        continue;
                    fifo.push(subg->root);
                }
            } else if (PSNodeRet *R = PSNodeRet::get(cur)) {
                for (auto ret : R->getReturnSites()) {
                    if (ret == exit)
                        continue;
                    fifo.push(ret);
                }
            }
        }
    }

    return cont;
}

} // namespace pta
} // namespace analysis
} // namespace dg

#endif