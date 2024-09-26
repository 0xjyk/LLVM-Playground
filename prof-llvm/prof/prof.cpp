#include "prof.h"

#include "llvm/Passes/PassBuilder.h"
#include "llvm/Passes/PassPlugin.h"

using namespace llvm;

// pretty prints the resutl of the analysis
static void printOpcodeCounterResult(llvm::raw_ostream &, 
        const ResultOpCounter &OC, StringRef name); 


// OpcodeCounter implementation
llvm::AnalysisKey OpcodeCounter::Key;

OpcodeCounter::Result OpcodeCounter::generateOpcodeMap(llvm::Function &F) {
    OpcodeCounter::Result OpcodeMap; 

    for (auto &BB : F) {
        for (auto &Inst : BB) {
            StringRef Name = Inst.getOpcodeName();
            if (OpcodeMap.find(Name) == OpcodeMap.end())
                OpcodeMap[Inst.getOpcodeName()] = 1;
            else 
                OpcodeMap[Inst.getOpcodeName()]++;
        }
    }
    return OpcodeMap;
}

OpcodeCounter::Result OpcodeCounter::run(llvm::Function &F, llvm::FunctionAnalysisManager &) {
    return generateOpcodeMap(F);
}

llvm::PreservedAnalyses OpcodeCounterPrinter::run(llvm::Function &F, 
        llvm::FunctionAnalysisManager &FAM) {
    auto &OpcodeMap = FAM.getResult<OpcodeCounter>(F);


    printOpcodeCounterResult(OS, OpcodeMap, F.getName());
    return PreservedAnalyses::all();
}

// New PM Registration

llvm::PassPluginLibraryInfo getOpcodeCounterPluginInfo() {
    return {
        LLVM_PLUGIN_API_VERSION, "OpcodeCounter", LLVM_VERSION_STRING, 
        [](PassBuilder &PB) {
            // #1 registration for "opt -passes=print<opcode-counter>"
            // Register OpcodeCounterPrinter so that it can be used when 
            // specifying pass pipelines with '-passes='
            PB.registerPipelineParsingCallback(
                [&](StringRef Name, FunctionPassManager &FPM, 
                    ArrayRef<PassBuilder::PipelineElement>) {
                    if (Name == "print<opcode-counter>") {
                        FPM.addPass(OpcodeCounterPrinter(llvm::errs()));
                        return true;
                    }
                    return false; 
                    });
            // #2 registration for "-O{1|2|3|s}"
            PB.registerVectorizerStartEPCallback(
                [](llvm::FunctionPassManager &PM, 
                    llvm::OptimizationLevel Level) {
                    PM.addPass(OpcodeCounterPrinter(llvm::errs()));
                });
            // #3 registration for "FAM.getResult<OpcodeCounter>(Func)"
            PB.registerAnalysisRegistrationCallback(
                    [](FunctionAnalysisManager &FAM) {
                    FAM.registerPass([&] { return OpcodeCounter(); });
                    });
        }};
}

extern "C" LLVM_ATTRIBUTE_WEAK ::llvm::PassPluginLibraryInfo 
llvmGetPassPluginInfo() {
    return getOpcodeCounterPluginInfo(); 
}

// Helper functions - implementation 
static void printOpcodeCounterResult(raw_ostream &OutS, 
        const ResultOpCounter &OpcodeMap, StringRef name) {
    OutS << "================================================="
         << "\n";
    OutS << "OpcodeCounter results " << name << "\n";
    OutS << "=================================================\n";
    const char *str1 = "OPCODE";
    const char *str2 = "#TIMES USED";
    OutS << format("%-20s %-10s\n", str1, str2);
    OutS << "-------------------------------------------------"
         << "\n";
    for (auto &Inst : OpcodeMap) {
    OutS << format("%-20s %-10lu\n", Inst.first().str().c_str(),
                           Inst.second);
    }
    OutS << "-------------------------------------------------"
         << "\n\n";
}
