#ifndef _DG_POINTS_TO_SET_H_
#define _DG_POINTS_TO_SET_H_

#include "dg/analysis/PointsTo/Pointer.h"
#include "dg/ADT/Bitvector.h"

#include <map>
#include <set>
#include <vector>
#include <cassert>

namespace dg {
namespace analysis {
namespace pta {

// declare PSNode
class PSNode;

class PointsToSet {
    // each pointer is a pair (PSNode *, {offsets}),
    // so we represent them coinciesly this way
    using ContainerT = std::map<PSNode *, ADT::SparseBitvector>;
    ContainerT pointers;

    bool addWithUnknownOffset(PSNode *target) {
        auto it = pointers.find(target);
        if (it != pointers.end()) {
            // we already had that offset?
            if (it->second.get(Offset::UNKNOWN))
                return false;

            // get rid of other offsets and keep
            // only the unknown offset
            it->second.reset();
            it->second.set(Offset::UNKNOWN);
            return true;
        }

        return !pointers[target].set(Offset::UNKNOWN);
    }

public:
    PointsToSet() = default;
    PointsToSet(std::initializer_list<Pointer> elems) { add(elems); }

    bool add(PSNode *target, Offset off) {
        if (off.isUnknown())
            return addWithUnknownOffset(target);

        auto it = pointers.find(target);
        if (it == pointers.end()) {
            pointers.emplace_hint(it, target, *off);
            return true;
        } else {
            if (it->second.get(Offset::UNKNOWN))
                return false;
            else {
                // the set will return the previous value
                // of the bit, so that means false if we are
                // setting a new value
                return !it->second.set(*off);
            }
        }
    }

    bool add(const Pointer& ptr) {
        return add(ptr.target, ptr.offset);
    }

    // union (unite S into this set)
    bool add(const PointsToSet& S) {
        bool changed = false;
        for (auto& it : S.pointers) {
            changed |= pointers[it.first].set(it.second);
        }
        return changed;
    }

    bool add(std::initializer_list<Pointer> elems) {
        bool changed = false;
        for (const auto& e : elems) {
            changed |= add(e);
        }
        return changed;
    }

    bool remove(const Pointer& ptr) {
        return remove(ptr.target, ptr.offset);
    }

    ///
    // Remove pointer to this target with this offset.
    // This is method really removes the pair
    // (target, off) even when the off is unknown
    bool remove(PSNode *target, Offset offset) {
        auto it = pointers.find(target);
        if (it == pointers.end()) {
            return false;
        }

        return it->second.unset(*offset);
    }

    ///
    // Remove pointers pointing to this target
    bool removeAny(PSNode *target) {
        auto it = pointers.find(target);
        if (it == pointers.end()) {
            return false;
        }

        pointers.erase(it);
        return true;
    }

    void clear() { pointers.clear(); }

    bool pointsTo(const Pointer& ptr) const {
        auto it = pointers.find(ptr.target);
        if (it == pointers.end())
            return false;
        return it->second.get(*ptr.offset);
    }

    // points to the pointer or the the same target
    // with unknown offset? Note: we do not count
    // unknown memory here...
    bool mayPointTo(const Pointer& ptr) const {
        return pointsTo(ptr) ||
                pointsTo(Pointer(ptr.target, Offset::UNKNOWN));
    }

    bool mustPointTo(const Pointer& ptr) const {
        assert(!ptr.offset.isUnknown() && "Makes no sense");
        return pointsTo(ptr) && isSingleton();
    }

    bool pointsToTarget(PSNode *target) const {
        return pointers.find(target) != pointers.end();
    }

    bool isSingleton() const {
        return pointers.size() == 1;
    }

    bool empty() const { return pointers.empty(); }

    size_t count(const Pointer& ptr) const {
        auto it = pointers.find(ptr.target);
        if (it != pointers.end()) {
            return it->second.get(*ptr.offset);
        }

        return 0;
    }

    bool has(const Pointer& ptr) const {
        return count(ptr) > 0;
    }

    bool hasUnknown() const { return pointsToTarget(UNKNOWN_MEMORY); }
    bool hasNull() const { return pointsToTarget(NULLPTR); }
    bool hasInvalidated() const { return pointsToTarget(INVALIDATED); }

    size_t size() const {
        size_t num = 0;
        for (auto& it : pointers) {
            num += it.second.size();
        }

        return num;
    }

    void swap(PointsToSet& rhs) { pointers.swap(rhs.pointers); }

    class const_iterator {
        typename ContainerT::const_iterator container_it;
        typename ContainerT::const_iterator container_end;
        typename ADT::SparseBitvector::const_iterator innerIt;

        const_iterator(const ContainerT& cont, bool end = false)
        : container_it(end ? cont.end() : cont.begin()), container_end(cont.end()) {
            if (container_it != container_end) {
                innerIt = container_it->second.begin();
            }
        }
    public:
        const_iterator& operator++() {
            ++innerIt;
            if (innerIt == container_it->second.end()) {
                ++container_it;
                if (container_it != container_end)
                    innerIt = container_it->second.begin();
                else
                    innerIt = ADT::SparseBitvector::const_iterator();
            }
            return *this;
        }

        const_iterator operator++(int) {
            auto tmp = *this;
            operator++();
            return tmp;
        }

        Pointer operator*() const {
            return Pointer(container_it->first, *innerIt);
        }

        bool operator==(const const_iterator& rhs) const {
            return container_it == rhs.container_it && innerIt == rhs.innerIt;
        }

        bool operator!=(const const_iterator& rhs) const {
            return !operator==(rhs);
        }

        friend class PointsToSet;
    };

    const_iterator begin() const { return const_iterator(pointers); }
    const_iterator end() const { return const_iterator(pointers, true /* end */); }

    friend class const_iterator;
};


///
// We keep the implementation of this points-to set because
// it is good for comparison and regression testing
class SimplePointsToSet {
    using ContainerT = std::set<Pointer>;
    ContainerT pointers;

    using const_iterator = typename ContainerT::const_iterator;

    bool addWithUnknownOffset(PSNode *target) {
        if (has({target, Offset::UNKNOWN}))
            return false;

        ContainerT tmp;
        for (const auto& ptr : pointers) {
            if (ptr.target != target)
                tmp.insert(ptr);
        }

        tmp.swap(pointers);
        return pointers.insert({target, Offset::UNKNOWN}).second;
    }

public:
    SimplePointsToSet() = default;
    SimplePointsToSet(std::initializer_list<Pointer> elems) { add(elems); }

    bool add(PSNode *target, Offset off) {
        if (off.isUnknown())
            return addWithUnknownOffset(target);

        // if we have the same pointer but with unknown offset,
        // do nothing
        if (has({target, Offset::UNKNOWN}))
            return false;

        return pointers.emplace(target, off).second;
    }

    bool add(const Pointer& ptr) {
        return add(ptr.target, ptr.offset);
    }

    // make union of the two sets and store it
    // into 'this' set (i.e. merge rhs to this set)
    bool add(const SimplePointsToSet& rhs) {
        bool changed = false;
        for (const auto& ptr : rhs.pointers) {
            changed |= pointers.insert(ptr).second;
        }

        return changed;
    }

    bool add(std::initializer_list<Pointer> elems) {
        bool changed = false;
        for (const auto& e : elems) {
            changed |= add(e);
        }
        return changed;
    }

    bool remove(const Pointer& ptr) {
        return pointers.erase(ptr) != 0;
    }

    ///
    // Remove pointer to this target with this offset.
    // This is method really removes the pair
    // (target, off), even when the off is unknown
    bool remove(PSNode *target, Offset offset) {
        return remove(Pointer(target, offset));
    }

    ///
    // Remove pointers pointing to this target
    bool removeAny(PSNode *target) {
        if (pointsToTarget(target)) {
            SimplePointsToSet tmp;
            for (const auto& ptr : pointers) {
                if (ptr.target == target) {
                    continue;
                }
                tmp.add(ptr);
            }
            assert(tmp.size() < size());
            swap(tmp);
            return true;
        }
        return false;
    }

    void clear() { pointers.clear(); }

    bool pointsTo(const Pointer& ptr) const {
        return pointers.count(ptr) > 0;
    }

    // points to the pointer or the the same target
    // with unknown offset? Note: we do not count
    // unknown memory here...
    bool mayPointTo(const Pointer& ptr) const {
        return pointsTo(ptr) ||
                pointsTo(Pointer(ptr.target, Offset::UNKNOWN));
    }

    bool mustPointTo(const Pointer& ptr) const {
        assert(!ptr.offset.isUnknown() && "Makes no sense");
        return pointsTo(ptr) && isSingleton();
    }

    bool pointsToTarget(PSNode *target) const {
        for (const auto& ptr : pointers) {
            if (ptr.target == target)
                return true;
        }
        return false;
    }

    bool isSingleton() const {
        return pointers.size() == 1;
    }

    size_t count(const Pointer& ptr) { return pointers.count(ptr); }
    size_t size() { return pointers.size(); }
    bool empty() const { return pointers.empty(); }
    bool has(const Pointer& ptr) { return count(ptr) > 0; }
    bool hasUnknown() const { return pointsToTarget(UNKNOWN_MEMORY); }
    bool hasNull() const { return pointsToTarget(NULLPTR); }
    bool hasInvalidated() const { return pointsToTarget(INVALIDATED); }

    void swap(SimplePointsToSet& rhs) { pointers.swap(rhs.pointers); }

    const_iterator begin() const { return pointers.begin(); }
    const_iterator end() const { return pointers.end(); }
};

class BitvectorPointsToSet {
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
        bool changed = nodes.set(getNodeID(target));
        return offsets.set(*off) || changed;
    }

    bool add(const Pointer& ptr) {
        return add(ptr.target, ptr.offset);
    }

    bool add(const BitvectorPointsToSet& S) {
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

    void swap(BitvectorPointsToSet& rhs) {
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

        friend class BitvectorPointsToSet;
    };

    const_iterator begin() const { return const_iterator(nodes, offsets); }
    const_iterator end() const { return const_iterator(nodes, offsets, true /* end */); }

    friend class const_iterator;
    
};

class BitvectorPointsToSet2 {
    ADT::SparseBitvector pointers;
    static std::map<Pointer, size_t> ids;
    static std::vector<Pointer> idVector; //starts from 0 for now
    
    size_t getPointerID(Pointer ptr) const {
        auto it = ids.find(ptr);
        if(it != ids.end())
            return it->second;
        idVector.push_back(ptr);
        return ids.emplace_hint(it, ptr, ids.size() + 1)->second;
    }
    
public:
    bool add(PSNode *target, Offset off) {
        return add(Pointer(target,off));
    }

    bool add(const Pointer& ptr) {
        return pointers.set(getPointerID(ptr));
    }

    bool add(const BitvectorPointsToSet2& S) {
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
        for(const auto& kv : ids) {
            if(kv.first.target == target) {
                changed |= pointers.unset(kv.second);
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
        return pointsTo(ptr) || pointsTo(Pointer(ptr.target, Offset::UNKNOWN));
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

    size_t size() const {
        return pointers.size();
    }

    void swap(BitvectorPointsToSet2& rhs) {
        pointers.swap(rhs.pointers);
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

        friend class BitvectorPointsToSet2;
    };

    const_iterator begin() const { return const_iterator(pointers); }
    const_iterator end() const { return const_iterator(pointers, true /* end */); }

    friend class const_iterator;
};

class BitvectorPointsToSet3 {
    
    ADT::SparseBitvector pointers;
    std::set<Pointer> largePointers;
    static std::map<PSNode*,size_t> ids;
    static std::vector<PSNode*> idVector; //starts from 0 for now

    size_t getNodeID(PSNode *node) const {
        auto it = ids.find(node);
        if(it != ids.end())
            return it->second;
        idVector.push_back(node);
        return ids.emplace_hint(it, node, ids.size() + 1)->second;
    }
    
    size_t getNodePosition(PSNode *node) const {
        return ((getNodeID(node) - 1) * 64);
    }
    
public:
    bool add(PSNode *target, Offset off) {
        if(off.isUnknown()) {
            return pointers.set(getNodePosition(target) + 63); 
        } else if(off < 63) {
            return pointers.set(getNodePosition(target) + off.offset);
        } else {
            return largePointers.emplace(target, off).second;
        }
    }

    bool add(const Pointer& ptr) {
        return add(ptr.target, ptr.offset);
    }

    bool add(const BitvectorPointsToSet3& S) {
        bool changed = pointers.set(S.pointers);
        for (const auto& ptr : S.largePointers) {
            changed |= largePointers.insert(ptr).second;
        }
        return changed;
    }

    bool remove(const Pointer& ptr) {
        if(ptr.offset.isUnknown()) {
            return pointers.unset(getNodePosition(ptr.target) + 63); 
        } else if(ptr.offset < 63) {
            return pointers.unset(getNodePosition(ptr.target) + ptr.offset.offset);
        } else {
            return largePointers.erase(ptr) != 0;
        }
    }
    
    bool remove(PSNode *target, Offset offset) {
        return remove(Pointer(target,offset));
    }
    
    bool removeAny(PSNode *target) {
        bool changed = false;
        size_t position = getNodePosition(target);
        for(size_t i = position; i <  position + 64; i++) {
            changed |= pointers.unset(i);
        }
        auto it = largePointers.begin();
        while(it != largePointers.end()) {
            if(it->target == target) {
                it = largePointers.erase(it);
                changed = true;
            }
            else {
                it++;
            }
        }
        return changed;
    }
    
    void clear() { 
        pointers.reset();
        largePointers.clear();
    }
   
    bool pointsTo(const Pointer& ptr) const {
        return pointers.get(getNodePosition(ptr.target) + ptr.offset.offset)
                || largePointers.find(ptr) != largePointers.end();
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
        size_t position = getNodePosition(target);
        for(size_t i = position; i <  position + 64; i++) {
            if(pointers.get(i))
                return true;
        }
        for (const auto& ptr : largePointers) {
            if (ptr.target == target)
                return true;
        }
        return false;
    }

    bool isSingleton() const {
        return (pointers.size() == 1 && largePointers.size() == 0)
                || (pointers.size() == 0 && largePointers.size() == 1);
    }

    bool empty() const {
        return pointers.size() == 0
                && largePointers.size() == 0;
    }

    size_t count(const Pointer& ptr) const {
        return pointsTo(ptr);
    }

    bool has(const Pointer& ptr) const {
        return count(ptr) > 0;
    }

    size_t size() const {
        return pointers.size() + largePointers.size();
    }

    void swap(BitvectorPointsToSet3& rhs) {
        pointers.swap(rhs.pointers);
        largePointers.swap(rhs.largePointers);
    }
    
    class const_iterator {
        typename ADT::SparseBitvector::const_iterator bitvector_it;
        typename ADT::SparseBitvector::const_iterator bitvector_end;
        typename std::set<Pointer>::const_iterator set_it;
        bool secondContainer = false;

        const_iterator(const ADT::SparseBitvector& pointers, const std::set<Pointer>& largePointers, bool end = false)
        : bitvector_it(pointers.begin()), 
        bitvector_end(pointers.end()), 
        set_it(end ? largePointers.end() : largePointers.begin()), 
        secondContainer(end) {
            if(bitvector_it == pointers.end()) {
                secondContainer = true;
            }
        }        
    public:
        const_iterator& operator++() {
            if(!secondContainer) {
                bitvector_it++;
                if(bitvector_it == bitvector_end) {
                    secondContainer = true;
                }
            } else {
                set_it++;
            }
            return *this;
        }

        const_iterator operator++(int) {
            auto tmp = *this;
            operator++();
            return tmp;
        }

        Pointer operator*() const { //not finished yet
            if(!secondContainer) {
                size_t offsetID = *bitvector_it % 64;
                size_t nodeID = ((*bitvector_it - offsetID) / 64) + 1;
                return offsetID == 63 ? Pointer(idVector[nodeID - 1], Offset::UNKNOWN) : Pointer(idVector[nodeID - 1], offsetID);  
            }
            return *set_it;
        }

        bool operator==(const const_iterator& rhs) const {
            return bitvector_it == rhs.bitvector_it
                    && set_it == rhs.set_it
                    && secondContainer == rhs.secondContainer;
        }

        bool operator!=(const const_iterator& rhs) const {
            return !operator==(rhs);
        }

        friend class BitvectorPointsToSet3;
    };

    const_iterator begin() const { return const_iterator(pointers, largePointers); }
    const_iterator end() const { return const_iterator(pointers, largePointers, true /* end */); }

    friend class const_iterator;
};

class BitvectorPointsToSet4 {
    
    static const unsigned int multiplier = 4;
    
    ADT::SparseBitvector pointers;
    std::set<Pointer> oddPointers;
    static std::map<PSNode*,size_t> ids;
    static std::vector<PSNode*> idVector; //starts from 0 for now

    size_t getNodeID(PSNode *node) const {
        auto it = ids.find(node);
        if(it != ids.end())
            return it->second;
        idVector.push_back(node);
        return ids.emplace_hint(it, node, ids.size() + 1)->second;
    }
    
    size_t getNodePosition(PSNode *node) const {
        return ((getNodeID(node) - 1) * 64);
    }
    
    size_t getOffsetPosition(PSNode *node, Offset off) const {
        if(off.isUnknown()) {
            return getNodePosition(node) + 63;
        }
        return getNodePosition(node) + (*off / multiplier);
    }
    
    bool isOffsetValid(Offset off) const {
        return off.isUnknown() 
                || (*off <= 62 * multiplier && *off % multiplier == 0);
    }
    
public:
    bool add(PSNode *target, Offset off) {
        if(isOffsetValid(off)) {
            return pointers.set(getOffsetPosition(target, off));
        }
        return oddPointers.emplace(target,off).second;
        
    }

    bool add(const Pointer& ptr) {
        return add(ptr.target, ptr.offset);
    }

    bool add(const BitvectorPointsToSet4& S) {
        bool changed = pointers.set(S.pointers);
        for (const auto& ptr : S.oddPointers) {
            changed |= oddPointers.insert(ptr).second;
        }
        return changed;
    }

    bool remove(const Pointer& ptr) {
        if(isOffsetValid(ptr.offset)) {
            return pointers.unset(getOffsetPosition(ptr.target, ptr.offset)); 
        }
        return oddPointers.erase(ptr) != 0;
    }
    
    bool remove(PSNode *target, Offset offset) {
        return remove(Pointer(target,offset));
    }
    
    bool removeAny(PSNode *target) {
        bool changed = false;
        size_t position = getNodePosition(target);
        for(size_t i = position; i <  position + 64; i++) {
            changed |= pointers.unset(i);
        }
        auto it = oddPointers.begin();
        while(it != oddPointers.end()) {
            if(it->target == target) {
                it = oddPointers.erase(it);
                changed = true;
            }
            else {
                it++;
            }
        }
        return changed;
    }
    
    void clear() { 
        pointers.reset();
        oddPointers.clear();
    }
   
    bool pointsTo(const Pointer& ptr) const {
        return pointers.get(getOffsetPosition(ptr.target,ptr.offset))
                || oddPointers.find(ptr) != oddPointers.end();
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
        size_t position = getNodePosition(target);
        for(size_t i = position; i <  position + 64; i++) {
            if(pointers.get(i))
                return true;
        }
        for (const auto& ptr : oddPointers) {
            if (ptr.target == target)
                return true;
        }
        return false;
    }

    bool isSingleton() const {
        return (pointers.size() == 1 && oddPointers.size() == 0)
                || (pointers.size() == 0 && oddPointers.size() == 1);
    }

    bool empty() const {
        return pointers.size() == 0
                && oddPointers.size() == 0;
    }

    size_t count(const Pointer& ptr) const {
        return pointsTo(ptr);
    }

    bool has(const Pointer& ptr) const {
        return count(ptr) > 0;
    }

    size_t size() const {
        return pointers.size() + oddPointers.size();
    }

    void swap(BitvectorPointsToSet4& rhs) {
        pointers.swap(rhs.pointers);
        oddPointers.swap(rhs.oddPointers);
    }
    
    class const_iterator {
        typename ADT::SparseBitvector::const_iterator bitvector_it;
        typename ADT::SparseBitvector::const_iterator bitvector_end;
        typename std::set<Pointer>::const_iterator set_it;
        bool secondContainer = false;

        const_iterator(const ADT::SparseBitvector& pointers, const std::set<Pointer>& oddPointers, bool end = false)
        : bitvector_it(pointers.begin()),
        bitvector_end(pointers.end()),
        set_it(end ? oddPointers.end() : oddPointers.begin()),
        secondContainer(end) {
            if(bitvector_it == pointers.end()) {
                secondContainer = true;
            }
        }        
    public:
        const_iterator& operator++() {
            if(!secondContainer) {
                bitvector_it++;
                if(bitvector_it == bitvector_end) {
                    secondContainer = true;
                }
            } else {
                set_it++;
            }
            return *this;
        }

        const_iterator operator++(int) {
            auto tmp = *this;
            operator++();
            return tmp;
        }

        Pointer operator*() const { //not finished yet
            if(!secondContainer) {
                size_t offsetPosition = (*bitvector_it % 64);
                size_t nodeID = ((*bitvector_it - offsetPosition) / 64) + 1;
                return offsetPosition == 63 ? Pointer(idVector[nodeID - 1], Offset::UNKNOWN) : Pointer(idVector[nodeID - 1], offsetPosition * multiplier);  
            }
            return *set_it;
        }

        bool operator==(const const_iterator& rhs) const {
            return bitvector_it == rhs.bitvector_it
                    && set_it == rhs.set_it
                    && secondContainer == rhs.secondContainer;
        }

        bool operator!=(const const_iterator& rhs) const {
            return !operator==(rhs);
        }

        friend class BitvectorPointsToSet4;
    };

    const_iterator begin() const { return const_iterator(pointers, oddPointers); }
    const_iterator end() const { return const_iterator(pointers, oddPointers, true /* end */); }

    friend class const_iterator;
};

using PointsToSetT = PointsToSet;
using PointsToMapT = std::map<Offset, PointsToSetT>;

} // namespace pta
} // namespace analysis
} // namespace dg

#endif // _DG_POINTS_TO_SET_H_
