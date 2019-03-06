#ifndef BLOCK_H
#define BLOCK_H

#include <vector>
#include <set>
#include <map>
#include <iosfwd>

namespace llvm {
    class Instruction;
    class Function;
    class BasicBlock;
}

namespace dg {
namespace cd {

struct Function;

struct Block
{
public:

    Block(bool callReturn = false):callReturn(callReturn) {}

    const std::set<Block *> & predecessors() const;

    const std::set<Block *> & successors() const;

    bool addPredecessor(Block * predecessor);
    bool removePredecessor(Block * predecessor);

    bool addSuccessor(Block * successor);
    bool removeSuccessor(Block * successor);

    const std::vector<const llvm::Instruction *> & llvmInstructions() const { return llvmInstructions_; }

    const llvm::Instruction * lastInstruction() const;

    bool addInstruction(const llvm::Instruction * instruction);

    bool addCallee(const llvm::Function * llvmFunction, Function * function);
    bool addFork(const llvm::Function * llvmFunction, Function * function);
    bool addJoin(const llvm::Function * llvmFunction, Function * function);

    const std::map<const llvm::Function *, Function *> & callees() const;
          std::map<const llvm::Function *, Function *>   callees();

    const std::map<const llvm::Function *, Function *> & forks() const;
          std::map<const llvm::Function *, Function *>   forks();

    const std::map<const llvm::Function *, Function *> & joins() const;
          std::map<const llvm::Function *, Function *>   joins();

    bool isCall() const;
    bool isArtificial() const;
    bool isCallReturn() const;
    bool isExit() const;

    const llvm::BasicBlock * llvmBlock() const;

    std::string dotName() const;

    std::string label() const;

    void dumpNode(std::ostream & ostream) const;
    void dumpEdges(std::ostream & ostream) const;


private:

    std::vector<const llvm::Instruction *> llvmInstructions_;

    std::set<Block *> predecessors_;
    std::set<Block *> successors_;

    bool callReturn = false;

    std::map<const llvm::Function *, Function *> callees_;
    std::map<const llvm::Function *, Function *> forks_;
    std::map<const llvm::Function *, Function *> joins_;
};

}
}

#endif // BLOCK_H
