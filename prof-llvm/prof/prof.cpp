#include "prof.h"

#include "llvm/IR/IRBuilder.h"
#include "llvm/Passes/PassPlugin.h"
#include "llvm/Passes/PassBuilder.h"
#include "llvm/IR/Module.h"
using namespace llvm; 

#define DEBUG_TYPE "inject-func-call"


// InjectFuncCall implementation 

bool InjectFuncCall::runOnModule(Module &M) {
    bool InsertedAtLeastOnePrintf = false; 

    auto &CTX = M.getContext();
    PointerType *PrintfArgTy = PointerType::getUnqual(Type::getInt8Ty(CTX));

    // step 1: Inject the declaration of printf
    FunctionType *PrintfTy = FunctionType::get(
            IntegerType::getInt32Ty(CTX), 
            PrintfArgTy, 
            true);
    FunctionCallee Printf = M.getOrInsertFunction("printf", PrintfTy);

    // Set attributes as per inferLibFuncAttributes in BuildLibCalls.cpp
    Function *PrintfF = dyn_cast<Function>(Printf.getCallee());
    PrintfF->setDoesNotThrow(); 
    PrintfF->addParamAttr(0, Attribute::NoCapture);
    PrintfF->addParamAttr(0, Attribute::ReadOnly);

    // Step 2: Inject a global variable that will hold the printf format string

    llvm::Constant *PrintfFormatStr = llvm::ConstantDataArray::getString(
            CTX, "Hello from: %s\n; number of arguments: %d\n");
    Constant *PrintfFormatStrVar = 
        M.getOrInsertGlobal("PrintfFormatStr", PrintfFormatStr->getType());
    dyn_cast<GlobalVariable>(PrintfFormatStrVar)->setInitializer(PrintfFormatStr);

    // Step 3: For each function in the module, inject a call to printf
    for (auto &F : M) {
        if (F.isDeclaration())
            continue;
        // Get an IR builder. Sets the insertion point to the top of the function 
        IRBuilder<> Builder(&*F.getEntryBlock().getFirstInsertionPt());

        // Inject a global variable that contains the function name
        auto FuncName = Builder.CreateGlobalStringPtr(F.getName());

        // Printf requires i8*, but PrintFormatStrVar is an array: [n x i8]. 
        // Add a cast: [n x i8] ->i8*
        llvm::Value *FormatStrPtr = 
            Builder.CreatePointerCast(PrintfFormatStrVar, PrintfArgTy, "formatStr");

        // Finally, inject a call to printf
        Builder.CreateCall(
                Printf, {FormatStrPtr, FuncName, Builder.getInt32(F.arg_size())});
        InsertedAtLeastOnePrintf = true;
    }
    return InsertedAtLeastOnePrintf;
}

PreservedAnalyses InjectFuncCall::run(llvm::Module &M, 
                                      llvm::ModuleAnalysisManager &) {
    bool Changed = runOnModule(M);
    return (Changed ? llvm::PreservedAnalyses::none()
                    : llvm::PreservedAnalyses::all());
}




//-----------------------------------------------------------------------------
// New PM Registration
//-----------------------------------------------------------------------------
llvm::PassPluginLibraryInfo getInjectFuncCallPluginInfo() {
  return {LLVM_PLUGIN_API_VERSION, "inject-func-call", LLVM_VERSION_STRING,
          [](PassBuilder &PB) {
            PB.registerPipelineParsingCallback(
                [](StringRef Name, ModulePassManager &MPM,
                   ArrayRef<PassBuilder::PipelineElement>) {
                  if (Name == "inject-func-call") {
                    MPM.addPass(InjectFuncCall());
                    return true;
                  }
                  return false;
                });
          }};
}

extern "C" LLVM_ATTRIBUTE_WEAK ::llvm::PassPluginLibraryInfo
llvmGetPassPluginInfo() {
  return getInjectFuncCallPluginInfo();
}
