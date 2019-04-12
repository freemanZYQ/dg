#ifndef SEPARATEOFFSETSPOINTSTOSET_H
#define SEPARATEOFFSETSPOINTSTOSET_H

#include "dg/analysis/PointsTo/Pointer.h"
#include "dg/ADT/Bitvector.h"

#include <map>
#include <vector>

namespace dg {
namespace analysis {
namespace pta {

class PSNode;

class SeparateOffsetsPointsToSet {
    ADT::SparseBitvector nodes;
    ADT::SparseBitvector offsets;
    static std::map<PSNode*,size_t> ids;
    static std::vector<PSNode*> idVector; //starts from 0 for now

    size_t getNodeID(PSNode *node) const {
        auto it = ids.find(node);
        if(it != ids.end())
            return it->second;
        idVector.push_back(node);
        return ids.emplace_hint(it, node, ids.size() + 1)->second;
    }
    
public:
    bool add(PSNode *target, Offset off) {
        bool changed = !nodes.set(getNodeID(target));
        return !offsets.set(*off) || changed;
    }

    bool add(const Pointer& ptr) {
        return add(ptr.target, ptr.offset);
    }

    bool add(const SeparateOffsetsPointsToSet& S) {
        bool changed = nodes.set(S.nodes);
        return offsets.set(S.offsets) || changed;
    }

    bool remove(const Pointer& ptr) {
        abort();
    }
    
    bool remove(PSNode *target, Offset offset) {
        abort();
    }
    
    bool removeAny(PSNode *target) {
        abort();
        /*
        bool changed = nodes.unset(getNodeID(target));
        if(nodes.empty()) {
            offsets.reset();
        }
        return changed;
        */
    }
    
    void clear() { 
        nodes.reset();
        offsets.reset();
    }
   
    bool pointsTo(const Pointer& ptr) const {
        return nodes.get(getNodeID(ptr.target)) && offsets.get(*ptr.offset);
    }

    bool mayPointTo(const Pointer& ptr) const {
        return pointsTo(ptr);
    }

    bool mustPointTo(const Pointer& ptr) const {
        return (nodes.size() == 1 || offsets.size() == 1)
                && pointsTo(ptr);
    }

    bool pointsToTarget(PSNode *target) const {
        return nodes.get(getNodeID(target));
    }

    bool isSingleton() const {
        return nodes.size() == 1 && offsets.size() == 1;
    }

    bool empty() const {
        return nodes.empty() && offsets.empty();
    }

    size_t count(const Pointer& ptr) const {
        return pointsTo(ptr);
    }

    bool has(const Pointer& ptr) const {
        return count(ptr) > 0;
    }

    size_t size() const {
        return nodes.size() * offsets.size();
    }

    void swap(SeparateOffsetsPointsToSet& rhs) {
        nodes.swap(rhs.nodes);
        offsets.swap(rhs.offsets);
    }
    
    class const_iterator {
        typename ADT::SparseBitvector::const_iterator nodes_it;
        typename ADT::SparseBitvector::const_iterator nodes_end;
        typename ADT::SparseBitvector::const_iterator offsets_it;
        typename ADT::SparseBitvector::const_iterator offsets_begin;
        typename ADT::SparseBitvector::const_iterator offsets_end;

        const_iterator(const ADT::SparseBitvector& nodes, const ADT::SparseBitvector& offsets, bool end = false) :
        nodes_it(end ? nodes.end() : nodes.begin()),
        nodes_end(nodes.end()),
        offsets_it(offsets.begin()),
        offsets_begin(offsets.begin()),
        offsets_end(offsets.end()) {
            if(nodes_it == nodes_end) {
                offsets_it = offsets_end;
            }
        }
        
    public:
        const_iterator& operator++() {
            offsets_it++;
            if(offsets_it == offsets_end) {
                nodes_it++;
                if(nodes_it != nodes_end) {
                    offsets_it = offsets_begin;
                }
            }
            return *this;
        }

        const_iterator operator++(int) {
            auto tmp = *this;
            operator++();
            return tmp;
        }

        Pointer operator*() const {
            return Pointer(idVector[*nodes_it - 1], *offsets_it);
        }

        bool operator==(const const_iterator& rhs) const {
            return nodes_it == rhs.nodes_it
                    && offsets_it == rhs.offsets_it;
        }
        bool operator!=(const const_iterator& rhs) const {
            return !operator==(rhs);
        }

        friend class SeparateOffsetsPointsToSet;
    };

    const_iterator begin() const { return const_iterator(nodes, offsets); }
    const_iterator end() const { return const_iterator(nodes, offsets, true /* end */); }

    friend class const_iterator;
    
};
    
} // namespace pta
} // namespace analysis
} // namespace dg

#endif /* SEPARATEOFFSETSPOINTSTOSET_H */

