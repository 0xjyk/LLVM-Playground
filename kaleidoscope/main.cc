// not original work; original source: https://llvm.org/docs/tutorial/MyFirstLanguageFrontend/LangImpl02.html
#include "KaleidoscopeJIT.h"
#include "llvm/ADT/APFloat.h"
#include "llvm/ADT/STLExtras.h"
#include "llvm/IR/BasicBlock.h"
#include "llvm/IR/Constants.h"
#include "llvm/IR/DerivedTypes.h"
#include "llvm/IR/Function.h"
#include "llvm/IR/IRBuilder.h"
#include "llvm/IR/LLVMContext.h"
#include "llvm/IR/Module.h"
#include "llvm/IR/PassManager.h"
#include "llvm/IR/Type.h"
#include "llvm/IR/Verifier.h"
#include "llvm/Passes/PassBuilder.h"
#include "llvm/Passes/StandardInstrumentations.h"
#include "llvm/Support/TargetSelect.h"
#include "llvm/Target/TargetMachine.h"
#include "llvm/Transforms/InstCombine/InstCombine.h"
#include "llvm/Transforms/Scalar.h"
#include "llvm/Transforms/Scalar/GVN.h"
#include "llvm/Transforms/Scalar/Reassociate.h"
#include "llvm/Transforms/Scalar/SimplifyCFG.h"
#include <algorithm>
#include <cassert>
#include <cctype>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <map>
#include <memory>
#include <string>
#include <vector>

using namespace llvm;
using namespace llvm::orc;



// forward declarations
static void MainLoop();
class ExprAST; 
static std::unique_ptr<ExprAST> ParseExpression();

// lexer

enum Token {
    tok_eof = -1,
    // commands 
    tok_def = -2,
    tok_extern = -3, 
    // primary 
    tok_identifier = -4, 
    tok_number = -5,
};

static std::string IdentifierStr; // filled in if tok_identifier
static double NumVal;             // filled in if tok_number


static int gettok() {
    static int LastChar = ' ';

    // skip any whitespace
    while (isspace(LastChar))
        LastChar = getchar(); 
    
    // identifier: [a-zA-Z][a-zA-Z0-9]
    if (isalpha(LastChar)) {             
        IdentifierStr = LastChar;
        while (isalnum((LastChar = getchar())))
            IdentifierStr += LastChar;

        if (IdentifierStr == "def")
            return tok_def; 
        if (IdentifierStr == "extern")
            return tok_extern;
        return tok_identifier;
    }
    // number: [0-9.]+
    if (isdigit(LastChar) || LastChar == '.') {
        std::string NumStr; 
        // not really correct, accepts [0-9].[0-9].[0-9.]
        do {
            NumStr += LastChar; 
            LastChar = getchar(); 
        } while (isdigit(LastChar) || LastChar == '.');

        NumVal = strtod(NumStr.c_str(), 0); 
        return tok_number;
    }
    // comment until end of line
    if (LastChar == '#') {
        do 
            LastChar = getchar(); 
        while (LastChar != EOF && LastChar != '\n' && LastChar != '\r'); 

        if (LastChar != EOF)
            return gettok();
    }
    
    // check for end of file

    if (LastChar == EOF)
        return tok_eof; 

    // Otherwise, just return the character as it its ascii value
    int ThisChar = LastChar; 
    LastChar = getchar(); 
    return ThisChar;
}

// AST nodes
class ExprAST {
    public:
        virtual ~ExprAST() = default;
        virtual Value *codegen() = 0;
};

// NumberExprAST - Expression class for numeric literals like "1.0"
class NumberExprAST : public ExprAST {
    double Val; 

    public: 
        NumberExprAST(double Val) : Val(Val) {}
        Value *codegen() override;
};

// VariableExprAST - Expression class for referencing a variable, like "a". 
class VariableExprAST : public ExprAST {
    std::string Name; 

    public:
        VariableExprAST(const std::string &Name) : Name(Name) {}
        Value *codegen() override;
};

// BinaryExprAST - Expression class for binary operator
class BinaryExprAST : public ExprAST {
    char Op; 
    std::unique_ptr<ExprAST> LHS, RHS; 

    public: 
        BinaryExprAST(char Op, std::unique_ptr<ExprAST> LHS, 
                std::unique_ptr<ExprAST> RHS) 
            : Op(Op), LHS(std::move(LHS)), RHS(std::move(RHS)) {}
        Value *codegen() override;
};

// CallExprAST - Expression class for function calls
class CallExprAST : public ExprAST {
    std::string Callee; 
    std::vector<std::unique_ptr<ExprAST>> Args; 

    public:
        CallExprAST(const std::string &Callee, 
                std::vector<std::unique_ptr<ExprAST>> Args)
        : Callee(Callee), Args(std::move(Args)) {}
        Value *codegen() override;
};

class PrototypeAST {
    std::string Name; 
    std::vector<std::string> Args; 

    public: 
        PrototypeAST(const std::string &Name, std::vector<std::string> Args)
            : Name(Name), Args(std::move(Args)) {}
        const std::string &getName() const { return Name; }
        Function *codegen();
};

// FunctionAST - This class represents a function definition itself 
class FunctionAST {
    std::unique_ptr<PrototypeAST> Proto; 
    std::unique_ptr<ExprAST> Body; 

    public:
        FunctionAST(std::unique_ptr<PrototypeAST> Proto, 
                std::unique_ptr<ExprAST> Body)
            : Proto(std::move(Proto)), Body(std::move(Body)) {}
        Function *codegen();
};

// parser
    
static int CurTok; 
static int getNextToken() {
    return CurTok = gettok();
}

std::unique_ptr<ExprAST> LogError(const char *Str) {
    fprintf(stderr, "Error: %s\n", Str); 
    return nullptr; 
}

std::unique_ptr<PrototypeAST> LogErrorP (const char *Str) {
    LogError(Str); 
    return nullptr;
}

// numberexpr ::= number
static std::unique_ptr<ExprAST> ParseNumberExpr() {
    auto Result = std::make_unique<NumberExprAST>(NumVal);
    getNextToken(); // consume the number
    return std::move(Result);
}

// parenexpr ::= '(' expression ')'
static std::unique_ptr<ExprAST> ParseParenExpr() {
    getNextToken(); // Eat '('
    auto V = ParseExpression(); 
    if (!V)
        return nullptr;

    if (CurTok != ')')
        return LogError("expected ')'");
    getNextToken();     // eat ')'
    return V;
}


// identifierexpr
//      ::= identifier
//      ::= identifer '(' expression* ')'
static std::unique_ptr<ExprAST> ParseIdentifierExpr() {
    std::string IdName = IdentifierStr; 

    getNextToken();     // eat identifier

    if (CurTok != '(')
        return std::make_unique<VariableExprAST>(IdName); 

    // call
    getNextToken();     // eat '('
    std::vector<std::unique_ptr<ExprAST>> Args; 
    if (CurTok != ')') {
        while (true) {
            if (auto Arg = ParseExpression())
                Args.push_back(std::move(Arg)); 
            else 
                return nullptr; 
            if (CurTok == ')')
                break;

            if (CurTok != ',')
                return LogError("Expected ')' or ',' in argument list"); 
            getNextToken(); 
        }
    }

    // Eat the ')'
    getNextToken(); 

    return std::make_unique<CallExprAST>(IdName, std::move(Args));
}


// primary 
//      ::= identifierexpr
//      ::= numberexpr
//      ::= parenexpr
static std::unique_ptr<ExprAST> ParsePrimary() {
    switch(CurTok) {
        default:
            return LogError("unknown token when expecting an expression");
        case tok_identifier:
            return ParseIdentifierExpr(); 
        case tok_number:
            return ParseNumberExpr(); 
        case '(':
            return ParseParenExpr();
    }
}

// binopPrecedence - This holds the precedence for each binary operator that is defined
static std::map<char, int> BinopPrecedence; 

// GetTokPrecedence - Get The precedence of the pending binary operator token
static int GetTokPrecedence() {
    if (!isascii(CurTok))
        return -1;

    // Make sure it's a declared binop
    int TokPrec = BinopPrecedence[CurTok];
    if (TokPrec <= 0) return -1;
    return TokPrec;
}
    

static std::unique_ptr<ExprAST> ParseBinOpRHS(int ExprPrec, std::unique_ptr<ExprAST> LHS) {
    
    // if this is a binop, find its precedence 
    while (true) {
        int TokPrec = GetTokPrecedence(); 

        // if this is a binop that binds at least as tightly as the current binop, 
        // consume it, otherwise we are done
        if (TokPrec < ExprPrec)
            return LHS; 

        // Okay, we know this is a binop 
        int BinOp = CurTok; 
        getNextToken();  // eat binop 

        // Parse the primary expression after the binary operator
        auto RHS = ParsePrimary();
        if (!RHS)
            return nullptr;

        // If BinOp binds less tightly with RHS than the operator after RHS, let 
        // the pending operator take RHS at its LHS
        int NextPrec = GetTokPrecedence(); 
        if (TokPrec < NextPrec) {
            RHS = ParseBinOpRHS(TokPrec + 1, std::move(RHS)); 
            if (!RHS)
                return nullptr;
        }

        // Merge LHS/RHS
        LHS = std::make_unique<BinaryExprAST>(BinOp, std::move(LHS), std::move(RHS));
    }
}

// expression 
//      ::= primary binoprhs
static std::unique_ptr<ExprAST> ParseExpression() {
    auto LHS = ParsePrimary(); 
    if (!LHS)
        return nullptr;
    return ParseBinOpRHS(0, std::move(LHS)); 
}

// Prototype
//      ::= id '(' id* ')'
static std::unique_ptr<PrototypeAST> ParsePrototype() {
    if (CurTok != tok_identifier)
        return LogErrorP("Expected function name in prototype");
    std::string FnName = IdentifierStr; 
    getNextToken(); 

    if (CurTok != '(')
        return LogErrorP("Expected '(' in prototype"); 

    // Read the list of argument names
    std::vector<std::string> ArgNames; 
    while (getNextToken() == tok_identifier)
        ArgNames.push_back(IdentifierStr);
    if (CurTok != ')')
        return LogErrorP("Expected ')' in prototype"); 

    // success 
    getNextToken(); // eat ')'

    return std::make_unique<PrototypeAST>(FnName, std::move(ArgNames)); 

}

// definition ::= 'def' prototype expression 
static std::unique_ptr<FunctionAST> ParseDefinition() {
    getNextToken(); // eat def
    auto Proto = ParsePrototype(); 
    if (!Proto) 
        return nullptr; 
    if (auto E = ParseExpression())
        return std::make_unique<FunctionAST>(std::move(Proto), std::move(E));
    return nullptr;
}

// external ::= 'extern' prototype
static std::unique_ptr<PrototypeAST> ParseExtern() {
    getNextToken(); 
    return ParsePrototype(); 
}

// toplevelexpr ::= expression
static std::unique_ptr<FunctionAST> ParseTopLevelExpr() {
    if (auto E = ParseExpression()) {
        // Make anon proto
        auto Proto = std::make_unique<PrototypeAST>("__anon_expr", std::vector<std::string>()); 
        return std::make_unique<FunctionAST>(std::move(Proto), std::move(E));
    }
    return nullptr;
}
// code generation 
static std::unique_ptr<LLVMContext> TheContext;
static std::unique_ptr<Module> TheModule;
static std::unique_ptr<IRBuilder<>> Builder;
static std::map<std::string, Value *> NamedValues;
static std::unique_ptr<KaleidoscopeJIT> TheJIT;
static std::unique_ptr<FunctionPassManager> TheFPM;
static std::unique_ptr<LoopAnalysisManager> TheLAM;
static std::unique_ptr<FunctionAnalysisManager> TheFAM;
static std::unique_ptr<CGSCCAnalysisManager> TheCGAM;
static std::unique_ptr<ModuleAnalysisManager> TheMAM;
static std::unique_ptr<PassInstrumentationCallbacks> ThePIC;
static std::unique_ptr<StandardInstrumentations> TheSI;
static std::map<std::string, std::unique_ptr<PrototypeAST>> FunctionProtos;
static ExitOnError ExitOnErr;
Value *LogErrorV(const char *Str) {
    LogError(Str); 
    return nullptr;
}

Value *NumberExprAST::codegen() {
    return ConstantFP::get(*TheContext, APFloat(Val));
}

Value *VariableExprAST::codegen() {
    // look this variable up in the function 
    Value *V = NamedValues[Name]; 
    if (!V)
        LogErrorV("Unknown variable name"); 
    return V;
}

Value *BinaryExprAST::codegen() {
    Value *L = LHS->codegen(); 
    Value *R = RHS->codegen(); 
    if (!L || !R)
        return nullptr;

    switch(Op) {
        case '+': 
            return Builder->CreateFAdd(L, R, "addtmp");
        case '-':
            return Builder->CreateFSub(L, R, "subtmp");
        case '*':
            return Builder->CreateFMul(L, R, "multmp");
        case '<':
            return Builder->CreateFCmpULT(L, R, "cmptmp");
            // convert bool 0/1 to double 0.0 or 1.0
            return Builder->CreateUIToFP(L, Type::getDoubleTy(*TheContext), "booltmp");
        default:
            return LogErrorV("invalid binary operator");
    }
}

Value *CallExprAST::codegen() {
    // look up the name in the global module table
    Function *CalleeF = TheModule->getFunction(Callee);
    if (!CalleeF)
        return LogErrorV("Unkown function referenced");

    // If argument mismatch error 
    if (CalleeF->arg_size() != Args.size())
        return LogErrorV("Incorrect # arguments passed");

    std::vector<Value *>ArgsV;
    for (unsigned i = 0, e = Args.size(); i != e; ++i) {
        ArgsV.push_back(Args[i]->codegen());
        if (!ArgsV.back())
            return nullptr;
    }
    return Builder->CreateCall(CalleeF, ArgsV, "calltmp");
}

Function *PrototypeAST::codegen() {
    // make the function type: double(double, double)
    std::vector<Type *>Doubles(Args.size(), Type::getDoubleTy(*TheContext));

    FunctionType *FT = 
        FunctionType::get(Type::getDoubleTy(*TheContext), Doubles, false);

    Function *F = 
        Function::Create(FT, Function::ExternalLinkage, Name, TheModule.get());
    // set names for all arguments 
    unsigned Idx = 0; 
    for (auto &Arg : F->args())
        Arg.setName(Args[Idx++]);
    return F;
}

Function *FunctionAST::codegen() {
    // first, check for an existing function from a previous 'extern' declaration
    Function *TheFunction = TheModule->getFunction(Proto->getName());

    if(!TheFunction)
        TheFunction = Proto->codegen();
    if (!TheFunction)
        return nullptr;
    if (!TheFunction->empty())
        return (Function *)LogErrorV("Function cannot be redefined");
    // Crete a new basic block to start insertion into 
    BasicBlock *BB = BasicBlock::Create(*TheContext, "entry", TheFunction);
    Builder->SetInsertPoint(BB);

    // Record the function arguments in the NamedValues map
    NamedValues.clear(); 
    for (auto &Arg : TheFunction->args())
        NamedValues[std::string(Arg.getName())] = &Arg;

    if (Value *RetVal = Body->codegen()) {
        // finish off the function 
        Builder->CreateRet(RetVal);

        // Validate the generated code, checking for consistency
        verifyFunction(*TheFunction);

        // optimize the function
        TheFPM->run(*TheFunction, *TheFAM);

        return TheFunction;
    }
    TheFunction->eraseFromParent();
    return nullptr;
}



// top-level parsing and JIT driver

void InitializeModuleAndManagers(void) {
    // Open a new context and module
    TheContext = std::make_unique<LLVMContext>(); 
    TheModule = std::make_unique<Module>("KaleidoscopeJIT", *TheContext);
    TheModule->setDataLayout(TheJIT->getDataLayout());

    // Create a new builder for the module
    Builder = std::make_unique<IRBuilder<>>(*TheContext);


    // create new pass and analysis mangers
    TheFPM = std::make_unique<FunctionPassManager>();
    TheLAM = std::make_unique<LoopAnalysisManager>(); 
    TheFAM = std::make_unique<FunctionAnalysisManager>();
    TheCGAM = std::make_unique<CGSCCAnalysisManager>(); 
    TheMAM = std::make_unique<ModuleAnalysisManager>();
    ThePIC = std::make_unique<PassInstrumentationCallbacks>(); 
    TheSI = std::make_unique<StandardInstrumentations>(*TheContext, true);
    TheSI->registerCallbacks(*ThePIC, TheMAM.get());

    // add transform passes 
    // do simple "peephole" optimizations and bit-twiddling optimizations
    TheFPM->addPass(InstCombinePass());
    // Reassociate expressions
    TheFPM->addPass(ReassociatePass());
    // Eliminate Common SubExpressions
    TheFPM->addPass(GVNPass());
    // Simplify the control flow graph (deleting unreachable blocks, etc)
    TheFPM->addPass(SimplifyCFGPass());

    // Register analysis passes used in these transform passes
    PassBuilder PB; 
    PB.registerModuleAnalyses(*TheMAM); 
    PB.registerFunctionAnalyses(*TheFAM);
    PB.crossRegisterProxies(*TheLAM, *TheFAM, *TheCGAM, *TheMAM);

}

static void HandleDefinition() {
    if (auto FnAST = ParseDefinition()) {
        if (auto *FnIR = FnAST->codegen()) {
            fprintf(stderr, "Read a function definition:");
            FnIR->print(errs());
            fprintf(stderr, "\n");
        }
    } else {
        // skip token for error recovery
        getNextToken();
    }
}


static void HandleExtern() {
    if (auto ProtoAST = ParseExtern()) {
        if (auto *FnIR = ProtoAST->codegen()) {
            fprintf(stderr, "Read extern: ");
            FnIR->print(errs()); 
            fprintf(stderr, "\n");
        }
    } else {
        // skip token for error recovery
        getNextToken();
    }
}

static void HandleTopLevelExpression() {
    // Evaluate a top-level expression into an anon function
    if (auto FnAST = ParseTopLevelExpr()) {
       if (FnAST->codegen()) {
           // Create a ResourceTracker to track JIT's memory allocated to our
           // anonymous expression - that way we can free it after executing 
           auto RT = TheJIT->getMainJITDylib().createResourceTracker();

           auto TSM = ThreadSafeModule(std::move(TheModule), std::move(TheContext));
           ExitOnErr(TheJIT->addModule(std::move(TSM), RT));
           InitializeModuleAndManagers();

           // Search the JIT for the __anon_expr symbol 
           auto ExprSymbol = ExitOnErr(TheJIT->lookup("__anon_expr"));

           // get the symbols address and cast it to the right type (takes no
           // arguments, returns a doube) so we can call it as a native function
           double (*FP)() = ExprSymbol.getAddress().toPtr<double (*)()>();
           fprintf(stderr, "Evaluated to %f\n", FP());

           // Delete the anon expression module from the JIT
           ExitOnErr(RT->remove());
       }
    } else {
        // skip token for error recovery
        getNextToken();
    }
}

// top ::= definition | external | expression | ';'
static void MainLoop() {
    while (true) {
        fprintf(stderr, "ready> "); 
        switch (CurTok) {
            case tok_eof:
                return;
            case ';':   //ignore top-level semicolons
                getNextToken();
                break;
            case tok_def:
                HandleDefinition(); 
                break;
            case tok_extern:
                HandleExtern(); 
                break;
            default:
                HandleTopLevelExpression(); 
                break;
        }
    }
}








// main driver

int main() {
    InitializeNativeTarget(); 
    InitializeNativeTargetAsmPrinter();
    InitializeNativeTargetAsmParser();
    // install standard binary operators 
    // 1 is lowest precedence 
    BinopPrecedence['<'] = 10; 
    BinopPrecedence['+'] = 20;
    BinopPrecedence['-'] = 20;
    BinopPrecedence['*'] = 40; // highest

    // prime the first token
    fprintf(stderr, "ready> "); 
    getNextToken(); 
    
    TheJIT = ExitOnErr(KaleidoscopeJIT::Create());
    InitializeModuleAndManagers();

    // run the main "interpretter loop" now
    MainLoop(); 

    return 0;
}
