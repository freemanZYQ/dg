#ifndef HAVE_LLVM
#error "This code needs LLVM enabled"
#endif

#include <set>
#include <iostream>
#include <sstream>
#include <fstream>
#include <string>

// ignore unused parameters in LLVM libraries
#if (__clang__)
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wunused-parameter"
#else
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wunused-parameter"
#endif

#include <llvm/IR/LLVMContext.h>
#include <llvm/IR/Module.h>
#include <llvm/IR/Instructions.h>
#include <llvm/Support/SourceMgr.h>
#include <llvm/Support/raw_os_ostream.h>
#include <llvm/IRReader/IRReader.h>

#if LLVM_VERSION_MAJOR >= 4
#include <llvm/Bitcode/BitcodeReader.h>
#else
#include <llvm/Bitcode/ReaderWriter.h>
#endif

#if (__clang__)
#pragma clang diagnostic pop // ignore -Wunused-parameter
#else
#pragma GCC diagnostic pop
#endif

#include "dg/llvm/analysis/PointsTo/PointerAnalysis.h"

#include "dg/analysis/PointsTo/PointerAnalysisFI.h"
#include "dg/analysis/PointsTo/PointerAnalysisFS.h"
#include "dg/analysis/PointsTo/Pointer.h"

#include "TimeMeasure.h"

using namespace dg;
using namespace dg::analysis::pta;
using llvm::errs;

enum PTType {
    FLOW_SENSITIVE = 1,
    FLOW_INSENSITIVE,
};

static std::string
getInstName(const llvm::Value *val)
{
    std::ostringstream ostr;
    llvm::raw_os_ostream ro(ostr);

    assert(val);
    ro << *val;
    ro.flush();

    // break the string if it is too long
    return ostr.str();
}

static void
printName(PSNode *node)
{
    if (!node->getUserData<llvm::Value>()) {
        printf("%p", static_cast<void*>(node));
        return;
    }

    std::string nm = getInstName(node->getUserData<llvm::Value>());
    const char *name = nm.c_str();

    // escape the " character
    for (int i = 0; name[i] != '\0'; ++i) {
        // crop long names
        if (i >= 70) {
            printf(" ...");
            break;
        }

        if (name[i] == '"')
            putchar('\\');

        putchar(name[i]);
    }
}

static void
dumpPSNode(PSNode *n)
{
    printf("NODE %3u: ", n->getID());
    printName(n);

    PSNodeAlloc *alloc = PSNodeAlloc::get(n);
    if (alloc &&
        (alloc->getSize() || alloc->isHeap() || alloc->isZeroInitialized()))
        printf(" [size: %lu, heap: %u, zeroed: %u]",
               alloc->getSize(), alloc->isHeap(), alloc->isZeroInitialized());

    if (n->pointsTo.empty()) {
        puts("\n    -> no points-to");
        return;
    } else
        putchar('\n');

    for (const Pointer& ptr : n->pointsTo) {
        printf("    -> ");
        printName(ptr.target);
        if (ptr.offset.isUnknown())
            puts(" + Offset::UNKNOWN");
        else
            printf(" + %lu\n", *ptr.offset);
    }
}

static bool verify_ptsets(const llvm::Value *val,
                          LLVMPointerAnalysis *fi,
                          LLVMPointerAnalysis *fs)
{
    PSNode *finode = fi->getPointsTo(val);
    PSNode *fsnode = fs->getPointsTo(val);

    if (!finode) {
        if (fsnode) {
            llvm::errs() << "FI don't have points-to for: " << *val << "\n"
                         << "but FS has:\n";
            dumpPSNode(fsnode);
        } else
            // if boths mapping are null we assume that
            // the value is not reachable from main
            // (if nothing more, its not different for FI and FS)
            return true;

        return false;
    }

    if (!fsnode) {
        if (finode) {
            llvm::errs() << "FS don't have points-to for: " << *val << "\n"
                         << "but FI has:\n";
            dumpPSNode(finode);
        } else
            return true;

        return false;
    }

    for (const Pointer& ptr : fsnode->pointsTo) {
        bool found = false;
        for (const Pointer& ptr2 : finode->pointsTo) {
            // either the pointer is there or
            // FS has (target, offset) and FI has (target, Offset::UNKNOWN),
            // than everything is fine. The other case (FS has Offset::UNKNOWN)
            // we don't consider here, since that should not happen
            if ((ptr2.target->getUserData<llvm::Value>()
                == ptr.target->getUserData<llvm::Value>())
                && (ptr2.offset == ptr.offset ||
                    ptr2.offset.isUnknown()
                    /* || ptr.offset.isUnknown()*/)) {
                found = true;
                break;
            }
        }

        if (!found) {
                llvm::errs() << "FS not subset of FI: " << *val << "\n";
                llvm::errs() << "FI ";
                dumpPSNode(finode);
                llvm::errs() << "FS ";
                dumpPSNode(fsnode);
                llvm::errs() << " ---- \n";
                return false;
        }
    }

    return true;
}

static bool verify_ptsets(llvm::Module *M,
                          LLVMPointerAnalysis *fi,
                          LLVMPointerAnalysis *fs)
{
    using namespace llvm;
    bool ret = true;

    for (Function& F : *M)
        for (BasicBlock& B : F)
            for (Instruction& I : B)
                if (!verify_ptsets(&I, fi, fs))
                    ret = false;

    return ret;
}

static void
dumpStats(LLVMPointerAnalysis *pta)
{
    const auto& nodes = pta->getNodes();
    printf("Pointer subgraph size: %lu\n", nodes.size()-1);

    size_t nonempty_size = 0; // number of nodes with non-empty pt-set
    size_t nonempty_overflow_set_size = 0; //number of nodes with non-empty overflow set
    size_t maximum = 0; // maximum pt-set size
    size_t maximum_overflow = 0; //maximum size of overflow set in SmallOffsets and DivisibleOffsets
    size_t pointing_to_unknown = 0;
    size_t pointing_only_to_unknown = 0;
    size_t pointing_to_invalidated = 0;
    size_t pointing_only_to_invalidated = 0;
    size_t singleton_count = 0;
    size_t singleton_nonconst_count = 0;
    size_t pointing_to_heap = 0;
    size_t pointing_to_global = 0;
    size_t pointing_to_stack = 0;
    size_t pointing_to_function = 0;
    size_t has_known_size = 0;
    size_t allocation_num = 0;
    size_t points_to_only_known_size = 0;
    size_t known_size_known_offset = 0;
    size_t only_valid_target = 0;
    size_t only_valid_and_some_known = 0;

    for (auto& node : nodes) {
        if (!node.get())
            continue;

        if (node->pointsTo.size() > 0)
            ++nonempty_size;
        
        if (node->pointsTo.overflowSetSize() > 0)
            ++nonempty_overflow_set_size;

        if (node->pointsTo.size() == 1) {
            ++singleton_count;
            if (node->getType() == PSNodeType::CONSTANT ||
                node->getType() == PSNodeType::FUNCTION)
                ++singleton_nonconst_count;
        }

        if (node->pointsTo.size() > maximum)
            maximum = node->pointsTo.size();
        
        if(node->pointsTo.overflowSetSize() > maximum_overflow)
            maximum_overflow = node->pointsTo.overflowSetSize();

        bool _points_to_only_known_size = true;
        bool _known_offset_only = true;
        bool _has_known_size_offset = false;
        bool _has_only_valid_targets = true;
        for (const auto& ptr : node->pointsTo) {
            if (ptr.offset.isUnknown()) {
                _known_offset_only = false;
            }

            if (ptr.isUnknown()) {
                _has_only_valid_targets = false;
                ++pointing_to_unknown;
                if (node->pointsTo.size() == 1)
                    ++pointing_only_to_unknown;
            }

            if (ptr.isInvalidated()) {
                _has_only_valid_targets = false;
                ++pointing_to_invalidated;
                if (node->pointsTo.size() == 1)
                    ++pointing_only_to_invalidated;
            }

            if (ptr.isNull()) {
                _has_only_valid_targets = false;
            }

            auto alloc = PSNodeAlloc::get(ptr.target);
            if (alloc) {
                ++allocation_num;
                if (node->getSize() != 0 &&
                    node->getSize() != Offset::UNKNOWN) {
                    ++has_known_size;
                    if (!ptr.offset.isUnknown())
                        _has_known_size_offset = true;
                } else
                    _points_to_only_known_size = false;

                if (alloc->isHeap()) {
                    ++pointing_to_heap;
                } else if (alloc->isGlobal()) {
                    ++pointing_to_global;
                } else if (alloc->getType() == PSNodeType::ALLOC){
                    assert(!alloc->isGlobal());
                    ++pointing_to_stack;
                }
            } else {
                _points_to_only_known_size = false;;

                if (ptr.target->getType() == PSNodeType::FUNCTION) {
                    ++pointing_to_function;
                }
            }
        }

        if (_points_to_only_known_size) {
            ++points_to_only_known_size;
            if (_known_offset_only)
                ++known_size_known_offset;
        }

        if (_has_only_valid_targets) {
            ++only_valid_target;
            if (_has_known_size_offset)
                ++only_valid_and_some_known;
        }
    }

    printf("Allocations: %lu\n", allocation_num);
    printf("Allocations with known size: %lu\n", has_known_size);
    printf("Nodes with non-empty pt-set: %lu\n", nonempty_size);
    printf("Pointers pointing only to known-size allocations: %lu\n",
            points_to_only_known_size);
    printf("Pointers pointing only to known-size allocations with known offset: %lu\n",
           known_size_known_offset);
    printf("Pointers pointing only to valid targets: %lu\n", only_valid_target);
    printf("Pointers pointing only to valid targets and some known size+offset: %lu\n", only_valid_and_some_known);

    double avg_ptset_size = 0;
    double avg_nonempty_ptset_size = 0; // avg over non-empty sets only
    double avg_overflow_set_size = 0; // avg of overflow set size in SmallOffsets and DivisibleOffsets PTSet
    double avg_nonempty_overflow_set_size = 0;
    size_t accumulated_ptset_size = 0;
    size_t accumulated_overflow_set_size = 0;

    for (auto& node : nodes) {
        if (!node.get())
            continue;

        if (accumulated_ptset_size > (~((size_t) 0)) - node->pointsTo.size()) {
            printf("Accumulated points to sets size > 2^64 - 1");
            avg_ptset_size += (accumulated_ptset_size /
                                static_cast<double>(nodes.size()-1));
            avg_nonempty_ptset_size += (accumulated_ptset_size /
                                        static_cast<double>(nonempty_size));
            accumulated_ptset_size = 0;
        }
        if (accumulated_overflow_set_size > (~((size_t) 0)) - node->pointsTo.overflowSetSize()) {
            printf("Accumulated overflow sets size > 2^64 - 1");
            avg_overflow_set_size += (accumulated_overflow_set_size /
                                        static_cast<double>(nodes.size() -1));
            avg_nonempty_overflow_set_size += (accumulated_overflow_set_size /
                                        static_cast<double>(nonempty_overflow_set_size));
            accumulated_overflow_set_size = 0;
        }
        accumulated_ptset_size += node->pointsTo.size();
        accumulated_overflow_set_size += node->pointsTo.overflowSetSize();
    }

    avg_ptset_size += (accumulated_ptset_size /
                            static_cast<double>(nodes.size()-1));
    avg_nonempty_ptset_size += (accumulated_ptset_size /
                                    static_cast<double>(nonempty_size));
    avg_overflow_set_size += (accumulated_overflow_set_size /
                            static_cast<double>(nodes.size()-1));
    avg_nonempty_ptset_size += (accumulated_overflow_set_size /
                                    static_cast<double>(nonempty_overflow_set_size));
    printf("Average pt-set size: %6.3f\n", avg_ptset_size);
    printf("Average non-empty pt-set size: %6.3f\n", avg_nonempty_ptset_size);
    printf("Pointing to singleton: %lu\n", singleton_count);
    printf("Non-constant pointing to singleton: %lu\n", singleton_nonconst_count);
    printf("Pointing to unknown: %lu\n", pointing_to_unknown);
    printf("Pointing to unknown singleton: %lu\n", pointing_only_to_unknown );
    printf("Pointing to invalidated: %lu\n", pointing_to_invalidated);
    printf("Pointing to invalidated singleton: %lu\n", pointing_only_to_invalidated);
    printf("Pointing to heap: %lu\n", pointing_to_heap);
    printf("Pointing to global: %lu\n", pointing_to_global);
    printf("Pointing to stack: %lu\n", pointing_to_stack);
    printf("Pointing to function: %lu\n", pointing_to_function);
    printf("Maximum pt-set size: %lu\n", maximum);
    printf("Average overflow set size: %6.3f\n", avg_overflow_set_size);
    printf("Average non-empty overflow set size: %6.3f\n", avg_nonempty_overflow_set_size);
    printf("Maximum overflow set size: %lu\n", maximum_overflow);
}

int main(int argc, char *argv[])
{
    llvm::Module *M;
    llvm::LLVMContext context;
    llvm::SMDiagnostic SMD;
    const char *module = nullptr;
    unsigned type = FLOW_SENSITIVE | FLOW_INSENSITIVE;

    // parse options
    for (int i = 1; i < argc; ++i) {
        // run only given points-to analysis
        if (strcmp(argv[i], "-pta") == 0) {
            if (strcmp(argv[i+1], "fs") == 0)
                type = FLOW_SENSITIVE;
            else if (strcmp(argv[i+1], "fi") == 0)
                type = FLOW_INSENSITIVE;
            else {
                errs() << "Unknown PTA type" << argv[i + 1] << "\n";
                abort();
            }
        /*} else if (strcmp(argv[i], "-v") == 0) {
            verbose = true;*/
        } else {
            module = argv[i];
        }
    }

    if (!module) {
        errs() << "Usage: % llvm-pta-compare [-pta fs|fi] IR_module\n";
        return 1;
    }

#if ((LLVM_VERSION_MAJOR == 3) && (LLVM_VERSION_MINOR <= 5))
    M = llvm::ParseIRFile(module, SMD, context);
#else
    auto _M = llvm::parseIRFile(module, SMD, context);
    // _M is unique pointer, we need to get Module *
    M = _M.get();
#endif

    if (!M) {
        llvm::errs() << "Failed parsing '" << module << "' file:\n";
        SMD.print(argv[0], errs());
        return 1;
    }

    dg::debug::TimeMeasure tm;

    LLVMPointerAnalysis *PTAfs = nullptr;
    LLVMPointerAnalysis *PTAfi = nullptr;

    if (type & FLOW_INSENSITIVE) {
        PTAfi = new LLVMPointerAnalysis(M);

        tm.start();
        PTAfi->run<analysis::pta::PointerAnalysisFI>();
        tm.stop();
        tm.report("INFO: Points-to flow-insensitive analysis took");
        dumpStats(PTAfi);
        
    }

    if (type & FLOW_SENSITIVE) {
        PTAfs = new LLVMPointerAnalysis(M);

        tm.start();
        PTAfs->run<analysis::pta::PointerAnalysisFS>();
        tm.stop();
        tm.report("INFO: Points-to flow-sensitive analysis took");
        dumpStats(PTAfs);
    }

    int ret = 0;
    if (type == (FLOW_SENSITIVE | FLOW_INSENSITIVE)) {
        ret = !verify_ptsets(M, PTAfi, PTAfs);
        if (ret == 0)
            llvm::errs() << "FS is a subset of FI, all OK\n";
    }

    delete PTAfi;
    delete PTAfs;

    return ret;
}
