#ifndef LLVM_INJECT_FN_H
#define LLVM_INJECT_FN_H

#include "llvm/IR/PassManager.h"
#include "llvm/Pass.h"

struct InjectFuncCall : public llvm::PassInfoMixin<InjectFuncCall> {
    llvm::PreservedAnalyses run(llvm::Module &M, 
            llvm::ModuleAnalysisManager &);
    bool runOnModule(llvm::Module &M);

    static bool isRequired() { return true; }
};





#endif // LLVM_INJECT_FN_H
