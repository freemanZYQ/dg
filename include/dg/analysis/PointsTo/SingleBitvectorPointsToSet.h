#ifndef SINGLEBITVECTORPOINTSTOSET_H
#define SINGLEBITVECTORPOINTSTOSET_H

#include "dg/analysis/PointsTo/Pointer.h"
#include "dg/ADT/Bitvector.h"

#include <map>
#include <vector>
#include <cassert>

namespace dg {
namespace analysis {
namespace pta {

class PSNode;

class SingleBitvectorPointsToSet {
    
    ADT::SparseBitvector pointers;
    static std::map<Pointer, size_t> ids; //pointers are numbered 1, 2, ...
    static std::vector<Pointer> idVector; //starts from 0 (pointer = idVector[id - 1])
    
    //if the pointer doesn't have ID, it's assigned one
    size_t getPointerID(const Pointer& ptr) const {
        auto it = ids.find(ptr);
        if(it != ids.end()) {
            return it->second;
        }
        idVector.push_back(ptr);
        return ids.emplace_hint(it, ptr, ids.size() + 1)->second;
    }
    
    bool addWithUnknownOffset(PSNode* node) {
        removeAny(node);
        return !pointers.set(getPointerID({node, Offset::UNKNOWN}));
    }
    
public:
    SingleBitvectorPointsToSet() = default;
    SingleBitvectorPointsToSet(std::initializer_list<Pointer> elems) { add(elems); }
    
    bool add(PSNode *target, Offset off) {
        return add(Pointer(target,off));
    }

    bool add(const Pointer& ptr) {
        if(has({ptr.target, Offset::UNKNOWN})) {
            return false;
        }
        if(ptr.offset.isUnknown()) {
            return addWithUnknownOffset(ptr.target);
        }
        return !pointers.set(getPointerID(ptr));
    }

    bool add(const SingleBitvectorPointsToSet& S) {
        return pointers.set(S.pointers);
    }

    bool remove(const Pointer& ptr) {
        return pointers.unset(getPointerID(ptr));
    }
    
    bool remove(PSNode *target, Offset offset) {
        return remove(Pointer(target,offset));
    }
    
    bool removeAny(PSNode *target) {
        bool changed = false;
        for(const auto& ptrID : pointers) {
            if(idVector[ptrID - 1].target == target) {
                changed = true;
                pointers.unset(ptrID);
            }
        }
        return changed;
    }
    
    void clear() { 
        pointers.reset();
    }
   
    bool pointsTo(const Pointer& ptr) const {
        return pointers.get(getPointerID(ptr));
    }

    bool mayPointTo(const Pointer& ptr) const {
        return pointsTo(ptr)
                || pointsTo(Pointer(ptr.target, Offset::UNKNOWN));
    }

    bool mustPointTo(const Pointer& ptr) const {
        assert(!ptr.offset.isUnknown() && "Makes no sense");
        return pointsTo(ptr) && isSingleton();
    }

    bool pointsToTarget(PSNode *target) const {
        for(const auto& kv : ids) {
            if(kv.first.target == target && pointers.get(kv.second)) {
                return true;
            }
        }
        return false;
    }

    bool isSingleton() const {
        return pointers.size() == 1;
    }

    bool empty() const {
        return pointers.empty();
    }

    size_t count(const Pointer& ptr) const {
        return pointsTo(ptr);
    }

    bool has(const Pointer& ptr) const {
        return count(ptr) > 0;
    }
    
    bool hasUnknown() const { 
        return pointsToTarget(UNKNOWN_MEMORY);
    }
    
    bool hasNull() const {
        return pointsToTarget(NULLPTR);
    
    }
    
    bool hasInvalidated() const {
        return pointsToTarget(INVALIDATED);
    }
    
    size_t size() const {
        return pointers.size();
    }

    void swap(SingleBitvectorPointsToSet& rhs) {
        pointers.swap(rhs.pointers);
    }
    
    size_t overflowSetSize() const {
        return 0;
    }

    size_t containerSize() const {
        return pointers;
    }
    
    class const_iterator {
        
        typename ADT::SparseBitvector::const_iterator container_it;

        const_iterator(const ADT::SparseBitvector& pointers, bool end = false) :
        container_it(end ? pointers.end() : pointers.begin()) {}
        
    public:
        const_iterator& operator++() {
            container_it++;
            return *this;
        }

        const_iterator operator++(int) {
            auto tmp = *this;
            operator++();
            return tmp;
        }

        Pointer operator*() const {
            return Pointer(idVector[*container_it - 1]);
        }

        bool operator==(const const_iterator& rhs) const {
            return container_it == rhs.container_it;
        }

        bool operator!=(const const_iterator& rhs) const {
            return !operator==(rhs);
        }

        friend class SingleBitvectorPointsToSet;
    };

    const_iterator begin() const { return const_iterator(pointers); }
    const_iterator end() const { return const_iterator(pointers, true /* end */); }

    friend class const_iterator;
};    
    
} // namespace pta
} // namespace analysis
} // namespace dg

#endif /* SINGLEBITVECTORPOINTSTOSET_H */
