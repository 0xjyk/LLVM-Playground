#ifndef LLVM_HELLOWORLD_H
#define LLVM_HELLOWORLD_H


#include "llvm/IR/PassManager.h"

namespace llvm {

class HelloWorldPass : public PassInfoMixin<HelloWorldPass> {
    public: 
        PreservedAnalyses run(Function &F, FunctionAnalysisManager &AM);
        static bool isRequired() { return true; }
};
} // namespace llvm

#endif // LLVM_HELLOWORLD_H
