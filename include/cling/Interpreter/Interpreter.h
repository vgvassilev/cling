//--------------------------------------------------------------------*- C++ -*-
// CLING - the C++ LLVM-based InterpreterG :)
// author:  Lukasz Janyst <ljanyst@cern.ch>
//
// This file is dual-licensed: you can choose to license it under the University
// of Illinois Open Source License or the GNU Lesser General Public License. See
// LICENSE.TXT for details.
//------------------------------------------------------------------------------

#ifndef CLING_INTERPRETER_H
#define CLING_INTERPRETER_H

#include "cling/Interpreter/InvocationOptions.h"

#include "llvm/ADT/OwningPtr.h"
#include "llvm/ADT/StringRef.h"

#include <string>
#include <cstdlib>

// FIXME: workaround until JIT supports exceptions
#include <setjmp.h>

namespace llvm {
  class raw_ostream;
  struct GenericValue;
  class ExecutionEngine;
  class LLVMContext;
  class Module;
  class Type;
  template <typename T> class SmallVectorImpl;
}

namespace clang {
  class ASTContext;
  class ASTDeserializationListener;
  class CodeGenerator;
  class CompilerInstance;
  class Decl;
  class DeclContext;
  class FunctionDecl;
  class GlobalDecl;
  class NamedDecl;
  class Parser;
  class QualType;
  class Sema;
}

namespace cling {
  namespace runtime {
    namespace internal {
      class DynamicExprInfo;
      template <typename T>
      T EvaluateT(DynamicExprInfo* ExprInfo, clang::DeclContext* DC);
      class LifetimeHandler;
    }
  }
  class ClangInternalState;
  class CompilationOptions;
  class DynamicLibraryManager;
  class IncrementalExecutor;
  class IncrementalParser;
  class InterpreterCallbacks;
  class LookupHelper;
  class Value;
  class Transaction;

  ///\brief Class that implements the interpreter-like behavior. It manages the
  /// incremental compilation.
  ///
  class Interpreter {
  public:

    ///\brief Pushes a new transaction, which will collect the decls that came
    /// within the scope of the RAII object. Calls commit transaction at 
    /// destruction.
    class PushTransactionRAII {
    private:
      Transaction* m_Transaction;
      const Interpreter* m_Interpreter;
    public:
      PushTransactionRAII(const Interpreter* i);
      ~PushTransactionRAII();
      void pop() const;
    };

    class StateDebuggerRAII {
    private:
      const Interpreter* m_Interpreter;
      llvm::OwningPtr<ClangInternalState> m_State;
    public:
      StateDebuggerRAII(const Interpreter* i);
      ~StateDebuggerRAII();
      void pop() const;
    };

    ///\brief Describes the return result of the different routines that do the
    /// incremental compilation.
    ///
    enum CompilationResult {
      kSuccess,
      kFailure,
      kMoreInputExpected
    };

    ///\brief Describes the result of running a function.
    ///
    enum ExecutionResult {
      ///\brief The function was run successfully.
      kExeSuccess,
      ///\brief Code generator is unavailable; not an error.
      kExeNoCodeGen,

      ///\brief First error value.
      kExeFirstError,
      ///\brief The function is not known and cannot be called.
      kExeFunctionNotCompiled = kExeFirstError,
      ///\brief While compiling the function, unknown symbols were encountered.
      kExeUnresolvedSymbols,
      ///\brief Compilation error.
      kExeCompilationError,
      ///\brief The function is not known.
      kExeUnkownFunction,

      ///\brief Number of possible results.
      kNumExeResults
    };

  private:

    ///\brief Interpreter invocation options.
    ///
    InvocationOptions m_Opts;

    ///\brief The llvm library state, a per-thread object.
    ///
    llvm::OwningPtr<llvm::LLVMContext> m_LLVMContext;

    ///\brief Cling's execution engine - a well wrapped llvm execution engine.
    ///
    llvm::OwningPtr<IncrementalExecutor> m_Executor;

    ///\brief Cling's worker class implementing the incremental compilation.
    ///
    llvm::OwningPtr<IncrementalParser> m_IncrParser;

    ///\brief Cling's reflection information query.
    ///
    llvm::OwningPtr<LookupHelper> m_LookupHelper;

    ///\brief Counter used when we need unique names.
    ///
    unsigned long long m_UniqueCounter;

    ///\brief Flag toggling the Debug printing on or off.
    ///
    bool m_PrintDebug;

    ///\brief Flag toggling the dynamic scopes on or off.
    ///
    bool m_DynamicLookupEnabled;

    ///\brief Flag toggling the raw input on or off.
    ///
    bool m_RawInputEnabled;

    ///\brief Interpreter callbacks.
    ///
    llvm::OwningPtr<InterpreterCallbacks> m_Callbacks;

    ///\brief Dynamic library manager object.
    ///
    llvm::OwningPtr<DynamicLibraryManager> m_DyLibManager;

    ///\brief Information about the last stored states through .storeState
    ///
    mutable std::vector<ClangInternalState*> m_StoredStates;

    ///\ brief The last pragma pop point we faked. This helps emulating clang's
    /// logic when it comes to pragma handling and diagnostic states.
    /// NOTE: this approach continues to add up states, which can be a
    /// performance penalty.
    ///
    unsigned m_LastCustomPragmaDiagPopPoint;

    ///\brief: FIXME: workaround until JIT supports exceptions
    static jmp_buf* m_JumpBuf;

    ///\brief Processes the invocation options.
    ///
    void handleFrontendOptions();

    ///\brief Worker function, building block for interpreter's public
    /// interfaces.
    ///
    ///\param [in] input - The input being compiled.
    ///\param [in] CompilationOptions - The option set driving the compilation.
    ///\param [out] T - The cling::Transaction of the compiled input.
    ///
    ///\returns Whether the operation was fully successful.
    ///
    CompilationResult DeclareInternal(const std::string& input,
                                      const CompilationOptions& CO,
                                      Transaction** T = 0) const;

    ///\brief Worker function, building block for interpreter's public
    /// interfaces.
    ///
    ///\param [in] input - The input being compiled.
    ///\param [in] CompilationOptions - The option set driving the compilation.
    ///\param [in,out] V - The result of the evaluation of the input. Must be
    ///       initialized to point to the return value's location if the 
    ///       expression result is an aggregate.
    ///\param [out] T - The cling::Transaction of the compiled input.
    ///
    ///\returns Whether the operation was fully successful.
    ///
    CompilationResult EvaluateInternal(const std::string& input,
                                       const CompilationOptions& CO,
                                       Value* V = 0,
                                       Transaction** T = 0);

    ///\brief Decides whether the input line should be wrapped or not by using
    /// simple lexing to determine whether it is known that it should be on the
    /// global scope or not.
    ///
    ///\param[in] input - The input being scanned.
    ///
    ///\returns true if the input should be wrapped.
    ///
    bool ShouldWrapInput(const std::string& input);

    ///\brief Wraps a given input.
    ///
    /// The interpreter must be able to run statements on the fly, which is not
    /// C++ standard-compliant operation. In order to do that we must wrap the
    /// input into a artificial function, containing the statements and run it.
    ///\param [out] input - The input to wrap.
    ///\param [out] fname - The wrapper function's name.
    ///
    void WrapInput(std::string& input, std::string& fname);

    ///\brief Runs given function.
    ///
    ///\param [in] fname - The function name.
    ///\param [in,out] res - The return result of the run function. Must be
    ///       initialized to point to the return value's location if the
    ///       expression result is an aggregate.
    ///
    ///\returns The result of the execution.
    ///
    ExecutionResult RunFunction(const clang::FunctionDecl* FD,
                                Value* res = 0);

    ///\brief Forwards to cling::IncrementalExecutor::addSymbol.
    ///
    bool addSymbol(const char* symbolName,  void* symbolAddress);

    ///\brief Compile the function definition and return its Decl.
    ///
    ///\param[in] name - name of the function, used to find its Decl.
    ///\param[in] code - function definition, starting with 'extern "C"'.
    ///\param[in] withAccessControl - whether to enforce access restrictions.
    const clang::FunctionDecl* DeclareCFunction(llvm::StringRef name,
                                                llvm::StringRef code,
                                                bool withAccessControl);

    ///\brief Set up include paths for runtime headers.
    ///
    void AddRuntimeIncludePaths(const char* argv0);

    ///\brief Include C++ runtime headers and definitions.
    ///
    void IncludeCXXRuntime();

    ///\brief Include C runtime headers and definitions.
    ///
    void IncludeCRuntime();

  public:
    Interpreter(int argc, const char* const *argv, const char* llvmdir = 0);
    virtual ~Interpreter();

    const InvocationOptions& getOptions() const { return m_Opts; }
    InvocationOptions& getOptions() { return m_Opts; }

    const llvm::LLVMContext* getLLVMContext() const {
      return m_LLVMContext.get();
    }

    llvm::LLVMContext* getLLVMContext() { return m_LLVMContext.get(); }

    const LookupHelper& getLookupHelper() const { return *m_LookupHelper; }

    const clang::Parser& getParser() const;

    ///\brief true if -fsyntax-only flag passed.
    ///
    bool isInSyntaxOnlyMode() const;

    ///\brief Shows the current version of the project.
    ///
    ///\returns The current svn revision (svn Id).
    ///
    const char* getVersion() const;

    ///\brief Creates unique name that can be used for various aims.
    ///
    void createUniqueName(std::string& out);

    ///\brief Checks whether the name was generated by Interpreter's unique name
    /// generator.
    ///
    ///\param[in] name - The name being checked.
    ///
    ///\returns true if the name is generated.
    ///
    bool isUniqueName(llvm::StringRef name);

    ///\brief Super efficient way of creating unique names, which will be used
    /// as a part of the compilation process.
    ///
    /// Creates the name directly in the compiler's identifier table, so that
    /// next time the compiler looks for that name it will find it directly
    /// there.
    ///
    llvm::StringRef createUniqueWrapper();

    ///\brief Checks whether the name was generated by Interpreter's unique
    /// wrapper name generator.
    ///
    ///\param[in] name - The name being checked.
    ///
    ///\returns true if the name is generated.
    ///
    bool isUniqueWrapper(llvm::StringRef name);

    ///\brief Adds an include path (-I).
    ///
    void AddIncludePath(llvm::StringRef incpath);

    ///\brief Prints the current include paths that are used.
    ///
    ///\param[out] incpaths - Pass in a llvm::SmallVector<std::string, N> with
    ///       sufficiently sized N, to hold the result of the call.
    ///\param[in] withSystem - if true, incpaths will also contain system
    ///       include paths (framework, STL etc).
    ///\param[in] withFlags - if true, each element in incpaths will be prefixed
    ///       with a "-I" or similar, and some entries of incpaths will signal
    ///       a new include path region (e.g. "-cxx-isystem"). Also, flags
    ///       defining header search behavior will be included in incpaths, e.g.
    ///       "-nostdinc".
    void GetIncludePaths(llvm::SmallVectorImpl<std::string>& incpaths,
                         bool withSystem, bool withFlags);

    ///\brief Prints the current include paths that are used.
    ///
    void DumpIncludePath();

    ///\brief Store the interpreter state in files
    /// Store the AST, the included files and the lookup tables
    ///
    ///\param[in] name - The name of the files where the state will
    /// be printed
    ///
    void storeInterpreterState(const std::string& name) const;

    ///\brief Compare the actual interpreter state with the one stored 
    /// previously.
    ///
    ///\param[in] name - The name of the previously stored file
    ///
    void compareInterpreterState(const std::string& name) const;

    ///\brief Print the included files in a temporary file 
    ///
    ///\param[in] out - The output stream to be printed into.
    ///
    void printIncludedFiles (llvm::raw_ostream& out) const;

    ///\brief Compiles the given input.
    ///
    /// This interface helps to run everything that cling can run. From
    /// declaring header files to running or evaluating single statements.
    /// Note that this should be used when there is no idea of what kind of
    /// input is going to be processed. Otherwise if is known, for example
    /// only header files are going to be processed it is much faster to run the
    /// specific interface for doing that - in the particular case - declare().
    ///
    ///\param[in] input - The input to be compiled.
    ///\param[in,out] V - The result of the evaluation of the input. Must be
    ///       initialized to point to the return value's location if the 
    ///       expression result is an aggregate.
    ///\param[out] T - The cling::Transaction of the compiled input.
    ///
    ///\returns Whether the operation was fully successful.
    ///
    CompilationResult process(const std::string& input, Value* V = 0,
                              Transaction** T = 0);

    ///\brief Parses input line, which doesn't contain statements. No code 
    /// generation is done.
    ///
    /// Same as declare without codegening. Useful when a library is loaded and
    /// the header files need to be imported.
    ///
    ///\param[in] input - The input containing the declarations.
    ///\param[out] T - The cling::Transaction of the parsed input.
    ///
    ///\returns Whether the operation was fully successful.
    ///
    CompilationResult parse(const std::string& input, 
                            Transaction** T = 0) const;

    ///\brief Looks for a already generated PCM for the given header file and 
    /// loads it.
    ///
    ///\param[in] headerFile - The header file for which a module should be 
    ///                        loaded.
    ///
    ///\returns Whether the operation was fully successful.
    ///
    CompilationResult loadModuleForHeader(const std::string& headerFile);

    ///\brief Parses input line, which doesn't contain statements. Code 
    /// generation needed to make the module functional.
    ///
    /// Same as declare without most of the codegening.  Only a few 
    /// things, like inline function are codegened.  Useful when a 
    /// library is loaded and the header files need to be imported.
    ///
    ///\param[in] input - The input containing the declarations.
    ///
    ///\returns Whether the operation was fully successful.
    ///
    CompilationResult parseForModule(const std::string& input);

    ///\brief Compiles input line, which doesn't contain statements.
    ///
    /// The interface circumvents the most of the extra work necessary to
    /// compile and run statements.
    ///
    /// @param[in] input - The input containing only declarations (aka
    ///                    Top Level Declarations)
    /// @param[out] T - The cling::Transaction of the input
    ///
    ///\returns Whether the operation was fully successful.
    ///
    CompilationResult declare(const std::string& input, Transaction** T = 0);

    ///\brief Compiles input line, which contains only expressions.
    ///
    /// The interface circumvents the most of the extra work necessary extract
    /// the declarations from the input.
    ///
    /// @param[in] input - The input containing only expressions
    /// @param[in,out] V - The value of the executed input. Must be
    ///       initialized to point to the return value's location if the 
    ///       expression result is an aggregate.
    ///
    ///\returns Whether the operation was fully successful.
    ///
    CompilationResult evaluate(const std::string& input, Value& V);

    ///\brief Compiles input line, which contains only expressions and prints
    /// out the result of its execution.
    ///
    /// The interface circumvents the most of the extra work necessary extract
    /// the declarations from the input.
    ///
    /// @param[in] input - The input containing only expressions.
    /// @param[in,out] V - The value of the executed input. Must be
    ///       initialized to point to the return value's location if the 
    ///       expression result is an aggregate.
    ///
    ///\returns Whether the operation was fully successful.
    /// 
    CompilationResult echo(const std::string& input, Value* V = 0);

    ///\brief Compiles input line and runs.
    ///
    /// The interface is the fastest way to compile and run a statement or
    /// expression. It just wraps the input and runs the wrapper, without any
    /// other "magic"
    ///
    /// @param[in] input - The input containing only expressions.
    ///
    ///\returns Whether the operation was fully successful.
    ///
    CompilationResult execute(const std::string& input);

    ///\brief Generates code for all Decls of a transaction.
    ///
    /// @param[in] T - The cling::Transaction that contains the declarations and
    ///                the compilation/generation options.
    ///
    ///\returns Whether the operation was fully successful.
    ///
    CompilationResult emitAllDecls(Transaction* T);

    ///\brief Looks up a file or library according to the current interpreter
    /// include paths and system include paths.
    ///\param[in] file - The name of the file.
    ///
    ///\returns the canonical path to the file or library or empty string if not
    /// found.
    ///
    std::string lookupFileOrLibrary(llvm::StringRef file);

    ///\brief Loads header file or shared library.
    ///
    ///\param [in] filename - The file to loaded.
    ///\param [in] allowSharedLib - Whether to try to load the file as shared
    ///                             library.
    ///\returns result of the compilation.
    ///
    CompilationResult loadFile(const std::string& filename,
                               bool allowSharedLib = true);

    ///\brief Unloads (forgets) given number of transactions.
    ///
    ///\param[in] numberOfTransactions - how many transactions to revert
    ///                                  starting from the last.
    ///
    void unload(unsigned numberOfTransactions);

    bool isPrintingDebug() const { return m_PrintDebug; }
    void enablePrintDebug(bool print = true) { m_PrintDebug = print; }

    void enableDynamicLookup(bool value = true);
    bool isDynamicLookupEnabled() const { return m_DynamicLookupEnabled; }

    bool isRawInputEnabled() const { return m_RawInputEnabled; }
    void enableRawInput(bool raw = true) { m_RawInputEnabled = raw; }

    clang::CompilerInstance* getCI() const;
    const clang::Sema& getSema() const;
    clang::Sema& getSema();
    llvm::ExecutionEngine* getExecutionEngine() const;

    //FIXME: This must be in InterpreterCallbacks.
    void installLazyFunctionCreator(void* (*fp)(const std::string&));

    //FIXME: Terrible hack to let the IncrementalParser run static inits on
    // transaction completed.
    ExecutionResult runStaticInitializersOnce(const Transaction& T) const;

    ///\brief Evaluates given expression within given declaration context.
    ///
    ///\param[in] expr - The expression.
    ///\param[in] DC - The declaration context in which the expression is going
    ///                to be evaluated.
    ///\param[in] ValuePrinterReq - Whether the value printing is requested.
    ///
    ///\returns The result of the evaluation if the expression.
    ///
    Value Evaluate(const char* expr, clang::DeclContext* DC,
                            bool ValuePrinterReq = false);

    ///\brief Interpreter callbacks accessors.
    /// Note that this class takes ownership of any callback object given to it.
    ///
    void setCallbacks(InterpreterCallbacks* C);
    const InterpreterCallbacks* getCallbacks() const {return m_Callbacks.get();}
    InterpreterCallbacks* getCallbacks() { return m_Callbacks.get(); }

    const DynamicLibraryManager* getDynamicLibraryManager() const {
      return m_DyLibManager.get();
    }
    DynamicLibraryManager* getDynamicLibraryManager() {
      return m_DyLibManager.get();
    }

    const Transaction* getFirstTransaction() const;
    const Transaction* getLastTransaction() const;
    const Transaction* getCurrentTransaction() const;

    ///\brief Compile extern "C" function and return its address.
    ///
    ///\param[in] name - function name
    ///\param[in] code - function definition, must contain 'extern "C"'
    ///\param[in] ifUniq - only compile this function if no function
    /// with the same name exists, else return the existing address
    ///\param[in] withAccessControl - whether to enforce access restrictions
    ///
    ///\returns the address of the function or 0 if the compilation failed.
    void* compileFunction(llvm::StringRef name, llvm::StringRef code,
                          bool ifUniq = true, bool withAccessControl = true);

    ///\brief Gets the address of an existing global and whether it was JITted.
    ///
    /// JIT symbols might not be immediately convertible to e.g. a function
    /// pointer as their call setup is different.
    ///
    ///\param[in]  D       - the global's Decl to find
    ///\param[out] fromJIT - whether the symbol was JITted.
    void* getAddressOfGlobal(const clang::GlobalDecl& D,
                             bool* fromJIT = 0) const;

    ///\brief Gets the address of an existing global and whether it was JITted.
    ///
    /// JIT symbols might not be immediately convertible to e.g. a function
    /// pointer as their call setup is different.
    ///
    ///\param[in]  SymName - the name of the global to search
    ///\param[out] fromJIT - whether the symbol was JITted.
    ///
    void* getAddressOfGlobal(llvm::StringRef SymName, bool* fromJIT = 0) const;

    ///\brief Add an atexit function.
    ///
    ///\param[in] Func - Function to be called.
    ///\param[in] Arg - argument passed to the function.
    ///
    void AddAtExitFunc(void (*Func) (void*), void* Arg);


    void GenerateAutoloadingMap(llvm::StringRef inFile,llvm::StringRef outFile);

    friend class runtime::internal::LifetimeHandler;
    // FIXME: workaround until JIT supports exceptions
    static jmp_buf*& getNullDerefJump() { return m_JumpBuf; }
  };

  namespace internal {
    // Force symbols needed by runtime to be included in binaries.
    void symbol_requester();
    static struct ForceSymbolsAsUsed {
      ForceSymbolsAsUsed(){
        // Never true, but don't tell the compiler.
        // Prevents stripping the symbol due to dead-code optimization.
        if (std::getenv("bar") == (char*) -1) symbol_requester();
      }
    } sForceSymbolsAsUsed;
  }
} // namespace cling

#endif // CLING_INTERPRETER_H
