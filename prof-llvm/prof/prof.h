#ifndef LLVM_STATICCALLCOUNTER_H
#define LLVM_STATICCALLCOUNTER_H

#include "llvm/ADT/MapVector.h"
#include "llvm/IR/AbstractCallSite.h"
#include "llvm/IR/Module.h"
#include "llvm/IR/PassManager.h"
#include "llvm/Pass.h"
#include "llvm/IR/InstrTypes.h"

namespace llvm {
    // create an alias for this specific MapVector type
    using ResultStaticCC = MapVector<const Function *, unsigned>;
struct StaticCallCounter : public AnalysisInfoMixin<StaticCallCounter> {
    using Result = ResultStaticCC; 
    Result run(Module &M, ModuleAnalysisManager &);
    Result runOnModule(Module &M);
    
    static bool isRequired() { return true; }

    private: 
    // a special type used by analysis passes to provide an address that 
    // identifies that particular analysis pass type
    static AnalysisKey Key;
    // why??? 
    friend struct AnalysisInfoMixin<StaticCallCounter>;

};

// printer class
class StaticCallCounterPrinter : public PassInfoMixin<StaticCallCounterPrinter> {
    public: 
        explicit StaticCallCounterPrinter(raw_ostream &OutS) : OS(OutS) {}
        PreservedAnalyses run(Module &M, ModuleAnalysisManager &MAM);

        static bool isRequired() { return true; }
    private: 
        raw_ostream &OS;
};

} // namespace llvm

#endif // LLVM_STATICCALLCOUNTER_H


/*
   Open questions
   ------------------
   1. What is the run member function supposed to return? 
   2. Why are we declaring the base class as a friend on line 22

*/
