#include "llvm/Passes/PassBuilder.h"
#include "llvm/Passes/PassPlugin.h"

// pass specific includes
#include "prof.h"


using namespace llvm;

// pretty prints the result of this analysis 
static void printStaticCCResult(raw_ostream &OutS, const ResultStaticCC &DirectCalls);

// StaticCallCounter Implementation
StaticCallCounter::Result StaticCallCounter::runOnModule(Module &M) {
    MapVector<const Function *, unsigned> Res;
    for (auto &Func : M) {
        for (auto &BB : Func) {
            for (auto &Ins : BB) {
                // If this is a call instruction then CB will not be null 
                auto *CB = dyn_cast<CallBase>(&Ins);
                if (nullptr == CB)
                    continue;

                // If CB is a direct function call then DirectInvoc will not be null
                auto DirectInvoc = CB->getCalledFunction(); 
                if (nullptr == DirectInvoc)
                    continue;

                // we have a direct function call - update the count for the function
                auto CallCount = Res.find(DirectInvoc);
                if (Res.end() == CallCount) {
                    CallCount = Res.insert(std::make_pair(DirectInvoc, 0)).first;
                }
                ++CallCount->second;
            }
        }
    }
    return Res;
}

PreservedAnalyses StaticCallCounterPrinter::run(Module &M, ModuleAnalysisManager &MAM) {
    auto DirectCalls = MAM.getResult<StaticCallCounter>(M); 
    
    printStaticCCResult(OS, DirectCalls);
    return PreservedAnalyses::all();
}

StaticCallCounter::Result
StaticCallCounter::run(Module &M, ModuleAnalysisManager &) {
    return runOnModule(M);
}

// why?? should the class construct this already?
AnalysisKey StaticCallCounter::Key;

// New PM Registration

PassPluginLibraryInfo getStaticCallCounterPluginInfo() {
    return {LLVM_PLUGIN_API_VERSION, "static-cc", LLVM_VERSION_STRING,
            [](PassBuilder &PB) {
                // #1 Registration for "opt -passes=print<static-cc>"
                PB.registerPipelineParsingCallback(
                        [&](StringRef Name, ModulePassManager &MPM, 
                            ArrayRef<PassBuilder::PipelineElement>) {
                        if (Name == "print<static-cc>") {
                            MPM.addPass(StaticCallCounterPrinter(errs()));
                            return true;
                            }
                            return false;
                            });
                // #2 Registration for "MAM.getResult<StaticCallCounter>(Module)"
                PB.registerAnalysisRegistrationCallback(
                        [](ModuleAnalysisManager &MAM) {
                        MAM.registerPass([&] { return StaticCallCounter(); });
                        });
            }};
};
                            

extern "C" LLVM_ATTRIBUTE_WEAK ::llvm::PassPluginLibraryInfo
llvmGetPassPluginInfo() {
    return getStaticCallCounterPluginInfo();
}


// Helper functions
static void printStaticCCResult(raw_ostream &OutS, const ResultStaticCC &DirectCalls) {
    OutS << "==============================================="
         << "\n";
    OutS << "static analysis results\n";
    OutS << "==============================================="
         << "\n";
    const char *str1 = "NAME";
    const char *str2 = "#N DIRECT CALLS";
    OutS << format("%-20s %-10s\n", str1, str2);
    OutS << "-----------------------------------------------"
         << "\n";
    for (auto &CallCount : DirectCalls) {
        OutS << format("%-20s %-10lu\n", CallCount.first->getName().str().c_str(),
                CallCount.second);
    }
    OutS << "-----------------------------------------------"
         << "\n\n";
}

