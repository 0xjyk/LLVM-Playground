#include "llvm/IR/LegacyPassManager.h"
#include "llvm/Passes/PassBuilder.h"
#include "llvm/Passes/PassPlugin.h"
#include "llvm/Support/raw_ostream.h"

// pass specific includes
#include "llvm/IR/InstrTypes.h"
#include "llvm/IR/IRBuilder.h"
#include "HelloWorld.h"
using namespace llvm;

void pass(Function &F) {
    for (auto &B : F) {
        for (auto &I : B) {
            if (auto *op = dyn_cast<BinaryOperator>(&I)) {
                IRBuilder<> builder(op);
                Value *lhs = op->getOperand(0);
                Value *rhs = op->getOperand(1);
                Value *mul = builder.CreateMul(lhs, rhs);
                
                for (auto& U : op->uses()) {
                    User *user = U.getUser();
                    user->setOperand(U.getOperandNo(), mul);
                }
            }
        }
    }
}

PreservedAnalyses HelloWorldPass::run(Function &F, FunctionAnalysisManager &AM) {
    pass(F);
    errs() << F << "\n";
    return PreservedAnalyses::all(); 
}

// New PM Registration
llvm::PassPluginLibraryInfo getHelloWorldPluginInfo() {
    return {LLVM_PLUGIN_API_VERSION, "HelloWorld", LLVM_VERSION_STRING, 
            [](PassBuilder &PB) {
                PB.registerPipelineParsingCallback(
                        [](StringRef Name, FunctionPassManager &FPM, 
                            ArrayRef<PassBuilder::PipelineElement>) {
                        if (Name == "hw") {
                        FPM.addPass(HelloWorldPass());
                        return true;
                        }
                        return false;
                        });
            }};
}

extern "C" LLVM_ATTRIBUTE_WEAK ::llvm::PassPluginLibraryInfo
llvmGetPassPluginInfo() {
    return getHelloWorldPluginInfo();
}


