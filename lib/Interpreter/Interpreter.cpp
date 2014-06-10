//------------------------------------------------------------------------------
// CLING - the C++ LLVM-based InterpreterG :)
// author:  Lukasz Janyst <ljanyst@cern.ch>
//
// This file is dual-licensed: you can choose to license it under the University
// of Illinois Open Source License or the GNU Lesser General Public License. See
// LICENSE.TXT for details.
//------------------------------------------------------------------------------

#include "cling/Interpreter/Interpreter.h"

#include "cling-compiledata.h"
#include "DynamicLookup.h"
#include "IncrementalExecutor.h"
#include "IncrementalParser.h"
#include "AutoloadingVisitor.h"

#include "cling/Interpreter/CIFactory.h"
#include "cling/Interpreter/ClangInternalState.h"
#include "cling/Interpreter/CompilationOptions.h"
#include "cling/Interpreter/DynamicLibraryManager.h"
#include "cling/Interpreter/InterpreterCallbacks.h"
#include "cling/Interpreter/LookupHelper.h"
#include "cling/Interpreter/Transaction.h"
#include "cling/Interpreter/Value.h"
#include "cling/Utils/AST.h"

#include "clang/AST/ASTContext.h"
#include "clang/AST/GlobalDecl.h"
#include "clang/Basic/TargetInfo.h"
#include "clang/Basic/SourceManager.h"
#include "clang/CodeGen/ModuleBuilder.h"
#include "clang/Frontend/CompilerInstance.h"
#include "clang/Frontend/Utils.h"
#include "clang/Lex/Preprocessor.h"
#include "clang/Parse/Parser.h"
#include "clang/Sema/Sema.h"
#include "clang/Sema/SemaDiagnostic.h"

#include "llvm/IR/LLVMContext.h"
#include "llvm/IR/Module.h"
#include "llvm/Support/Path.h"
#include "llvm/Support/raw_ostream.h"

#include <sstream>
#include <string>
#include <vector>

using namespace clang;

namespace {

  static cling::Interpreter::ExecutionResult
  ConvertExecutionResult(cling::IncrementalExecutor::ExecutionResult ExeRes) {
    switch (ExeRes) {
    case cling::IncrementalExecutor::kExeSuccess:
      return cling::Interpreter::kExeSuccess;
    case cling::IncrementalExecutor::kExeFunctionNotCompiled:
      return cling::Interpreter::kExeFunctionNotCompiled;
    case cling::IncrementalExecutor::kExeUnresolvedSymbols:
      return cling::Interpreter::kExeUnresolvedSymbols;
    default: break;
    }
    return cling::Interpreter::kExeSuccess;
  }
} // unnamed namespace

namespace cling {
  namespace runtime {
    namespace internal {
      // "Declared" to the JIT in RuntimeUniverse.h
      void local_cxa_atexit(void (*func) (void*), void* arg, void* interp) {
        Interpreter* cling = (cling::Interpreter*)interp;
        cling->AddAtExitFunc(func, arg);
      }
    } // end namespace internal
  } // end namespace runtime

  // FIXME: workaround until JIT supports exceptions
  jmp_buf* Interpreter::m_JumpBuf;

  Interpreter::PushTransactionRAII::PushTransactionRAII(const Interpreter* i)
    : m_Interpreter(i) {
    CompilationOptions CO;
    CO.DeclarationExtraction = 0;
    CO.ValuePrinting = 0;
    CO.ResultEvaluation = 0;
    CO.DynamicScoping = 0;
    CO.Debug = 0;
    CO.CodeGeneration = 1;
    CO.CodeGenerationForModule = 0;

    m_Transaction = m_Interpreter->m_IncrParser->beginTransaction(CO);
  }

  Interpreter::PushTransactionRAII::~PushTransactionRAII() {
    pop();
  }

  void Interpreter::PushTransactionRAII::pop() const {
    if (Transaction* T 
        = m_Interpreter->m_IncrParser->endTransaction(m_Transaction)) {
      assert(T == m_Transaction && "Ended different transaction?");
      m_Interpreter->m_IncrParser->commitTransaction(T);
    }
  }

  Interpreter::StateDebuggerRAII::StateDebuggerRAII(const Interpreter* i)
    : m_Interpreter(i) {
    if (!i->isPrintingDebug())
      return;
    const CompilerInstance& CI = *m_Interpreter->getCI();
    CodeGenerator* CG = i->m_IncrParser->getCodeGenerator();

    // The ClangInternalState constructor can provoke deserialization,
    // we need a transaction.
    PushTransactionRAII pushedT(i);

    m_State.reset(new ClangInternalState(CI.getASTContext(),
                                         CI.getPreprocessor(),
                                         CG ? CG->GetModule() : 0,
                                         CG,
                                         "aName"));
  }

  Interpreter::StateDebuggerRAII::~StateDebuggerRAII() {
    // The ClangInternalState destructor can provoke deserialization,
    // we need a transaction.
    PushTransactionRAII pushedT(m_Interpreter);

    pop();
  }

  void Interpreter::StateDebuggerRAII::pop() const {
    if (!m_Interpreter->isPrintingDebug())
      return;
    m_State->compare("aName");
  }

  // This function isn't referenced outside its translation unit, but it
  // can't use the "static" keyword because its address is used for
  // GetMainExecutable (since some platforms don't support taking the
  // address of main, and some platforms can't implement GetMainExecutable
  // without being given the address of a function in the main executable).
  std::string GetExecutablePath(const char *Argv0) {
    // This just needs to be some symbol in the binary; C++ doesn't
    // allow taking the address of ::main however.
    void *MainAddr = (void*) (intptr_t) GetExecutablePath;
    return llvm::sys::fs::getMainExecutable(Argv0, MainAddr);
  }

  const Parser& Interpreter::getParser() const {
    return *m_IncrParser->getParser();
  }

  bool Interpreter::isInSyntaxOnlyMode() const {
    return getCI()->getFrontendOpts().ProgramAction
      == clang::frontend::ParseSyntaxOnly;
  }

  Interpreter::Interpreter(int argc, const char* const *argv,
                           const char* llvmdir /*= 0*/) :
    m_UniqueCounter(0), m_PrintDebug(false),
    m_DynamicLookupEnabled(false), m_RawInputEnabled(false),
    m_LastCustomPragmaDiagPopPoint(){

    m_LLVMContext.reset(new llvm::LLVMContext);
    std::vector<unsigned> LeftoverArgsIdx;
    m_Opts = InvocationOptions::CreateFromArgs(argc, argv, LeftoverArgsIdx);
    std::vector<const char*> LeftoverArgs;

    for (size_t I = 0, N = LeftoverArgsIdx.size(); I < N; ++I) {
      LeftoverArgs.push_back(argv[LeftoverArgsIdx[I]]);
    }

    m_DyLibManager.reset(new DynamicLibraryManager(getOptions()));

    m_IncrParser.reset(new IncrementalParser(this, LeftoverArgs.size(),
                                             &LeftoverArgs[0],
                                             llvmdir));
    Sema& SemaRef = getSema();
    Preprocessor& PP = SemaRef.getPreprocessor();
    // Enable incremental processing, which prevents the preprocessor destroying
    // the lexer on EOF token.
    PP.enableIncrementalProcessing();

    m_LookupHelper.reset(new LookupHelper(new Parser(PP, SemaRef,
                                                     /*SkipFunctionBodies*/false,
                                                     /*isTemp*/true), this));

    if (!isInSyntaxOnlyMode()) {
      llvm::Module* theModule = m_IncrParser->getCodeGenerator()->GetModule();
      m_Executor.reset(new IncrementalExecutor(theModule, SemaRef.Diags));
    }

    llvm::SmallVector<Transaction*, 2> IncrParserTransactions;
    m_IncrParser->Initialize(IncrParserTransactions);

    handleFrontendOptions();

    AddRuntimeIncludePaths(argv[0]);

    // Tell the diagnostic client that we are entering file parsing mode.
    DiagnosticConsumer& DClient = getCI()->getDiagnosticClient();
    DClient.BeginSourceFile(getCI()->getLangOpts(), &PP);

    if (getCI()->getLangOpts().CPlusPlus)
      IncludeCXXRuntime();
    else
      IncludeCRuntime();

    // Commit the transactions, now that gCling is set up. It is needed for
    // static initialization in these transactions through local_cxa_atexit().
    for (llvm::SmallVectorImpl<Transaction*>::const_iterator
           I = IncrParserTransactions.begin(), E = IncrParserTransactions.end();
         I != E; ++I)
      m_IncrParser->commitTransaction(*I);
  }

  Interpreter::~Interpreter() {
    if (m_Executor)
      m_Executor->shuttingDown();
    for (size_t i = 0, e = m_StoredStates.size(); i != e; ++i)
      delete m_StoredStates[i];
    getCI()->getDiagnostics().getClient()->EndSourceFile();
  }

  const char* Interpreter::getVersion() const {
    return CLING_VERSION;
  }

  void Interpreter::handleFrontendOptions() {
    if (m_Opts.ShowVersion) {
      llvm::errs() << getVersion() << '\n';
    }
    if (m_Opts.Help) {
      m_Opts.PrintHelp();
    }
  }

  void Interpreter::AddRuntimeIncludePaths(const char* argv0) {
    // Add configuration paths to interpreter's include files.
#ifdef CLING_INCLUDE_PATHS
    llvm::StringRef InclPaths(CLING_INCLUDE_PATHS);
    for (std::pair<llvm::StringRef, llvm::StringRef> Split
           = InclPaths.split(':');
         !Split.second.empty(); Split = InclPaths.split(':')) {
      if (llvm::sys::fs::is_directory(Split.first))
        AddIncludePath(Split.first);
      InclPaths = Split.second;
    }
    // Add remaining part
    AddIncludePath(InclPaths);
#endif
    llvm::SmallString<512> P(GetExecutablePath(argv0));
    if (!P.empty()) {
      // Remove /cling from foo/bin/clang
      llvm::StringRef ExeIncl = llvm::sys::path::parent_path(P);
      // Remove /bin   from foo/bin
      ExeIncl = llvm::sys::path::parent_path(ExeIncl);
      P.resize(ExeIncl.size());
      // Get foo/include
      llvm::sys::path::append(P, "include");
      if (llvm::sys::fs::is_directory(P.str()))
        AddIncludePath(P.str());
    }

  }

  void Interpreter::IncludeCXXRuntime() {
    // Set up common declarations which are going to be available
    // only at runtime
    // Make sure that the universe won't be included to compile time by using
    // -D __CLING__ as CompilerInstance's arguments
#ifdef _WIN32
    // We have to use the #defined __CLING__ on windows first.
    //FIXME: Find proper fix.
    declare("#ifdef __CLING__ \n#endif");
#endif
    declare("#include \"cling/Interpreter/RuntimeUniverse.h\"");

    if (!isInSyntaxOnlyMode()) {
      // Set up the gCling variable if it can be used
      std::stringstream initializer;
      initializer << "namespace cling {namespace runtime { "
        "cling::Interpreter *gCling=(cling::Interpreter*)"
                  << (uintptr_t)this << ";} }";
      declare(initializer.str());
    }
  }

  void Interpreter::IncludeCRuntime() {
    // Set up the gCling variable if it can be used
    std::stringstream initializer;
    initializer << "void* gCling=(void*)" << (uintptr_t)this << ';';
    declare(initializer.str());
    // declare("void setValueNoAlloc(void* vpI, void* vpSVR, void* vpQT);");
    // declare("void setValueNoAlloc(void* vpI, void* vpV, void* vpQT, float value);");
    // declare("void setValueNoAlloc(void* vpI, void* vpV, void* vpQT, double value);");
    // declare("void setValueNoAlloc(void* vpI, void* vpV, void* vpQT, long double value);");
    // declare("void setValueNoAlloc(void* vpI, void* vpV, void* vpQT, unsigned long long value);");
    // declare("void setValueNoAlloc(void* vpI, void* vpV, void* vpQT, const void* value);");
    // declare("void* setValueWithAlloc(void* vpI, void* vpV, void* vpQT);");

    declare("#include \"cling/Interpreter/CValuePrinter.h\"");
  }

  void Interpreter::AddIncludePath(llvm::StringRef incpath)
  {
    // Add the given path to the list of directories in which the interpreter
    // looks for include files. Only one path item can be specified at a
    // time, i.e. "path1:path2" is not supported.

    CompilerInstance* CI = getCI();
    HeaderSearchOptions& headerOpts = CI->getHeaderSearchOpts();
    const bool IsFramework = false;
    const bool IsSysRootRelative = true;

    // Avoid duplicates; just return early if incpath is already in UserEntries.
    for (std::vector<HeaderSearchOptions::Entry>::const_iterator
           I = headerOpts.UserEntries.begin(),
           E = headerOpts.UserEntries.end(); I != E; ++I)
      if (I->Path == incpath)
        return;

    headerOpts.AddPath(incpath, frontend::Angled, IsFramework,
                       IsSysRootRelative);

    Preprocessor& PP = CI->getPreprocessor();
    ApplyHeaderSearchOptions(PP.getHeaderSearchInfo(), headerOpts,
                                    PP.getLangOpts(),
                                    PP.getTargetInfo().getTriple());
  }

  void Interpreter::DumpIncludePath() {
    llvm::SmallVector<std::string, 100> IncPaths;
    GetIncludePaths(IncPaths, true /*withSystem*/, true /*withFlags*/);
    // print'em all
    for (unsigned i = 0; i < IncPaths.size(); ++i) {
      llvm::errs() << IncPaths[i] <<"\n";
    }
  }

  void Interpreter::storeInterpreterState(const std::string& name) const {
    // This may induce deserialization
    PushTransactionRAII RAII(this);
    CodeGenerator* CG = m_IncrParser->getCodeGenerator();
    ClangInternalState* state
      = new ClangInternalState(getCI()->getASTContext(),
                               getCI()->getPreprocessor(),
                               CG ? CG->GetModule() : 0,
                               CG, name);
    m_StoredStates.push_back(state);
  }

  void Interpreter::compareInterpreterState(const std::string& name) const {
    short foundAtPos = -1;
    for (short i = 0, e = m_StoredStates.size(); i != e; ++i) {
      if (m_StoredStates[i]->getName() == name) {
        foundAtPos = i;
        break;
      }
    }
    assert(foundAtPos>-1 && "The name doesnt exist. Unbalanced store/compare");

    // This may induce deserialization
    PushTransactionRAII RAII(this);
    m_StoredStates[foundAtPos]->compare(name);
  }

  void Interpreter::printIncludedFiles(llvm::raw_ostream& Out) const {
    ClangInternalState::printIncludedFiles(Out, getCI()->getSourceManager());
  }


  // Adapted from clang/lib/Frontend/CompilerInvocation.cpp
  void Interpreter::GetIncludePaths(llvm::SmallVectorImpl<std::string>& incpaths,
                                   bool withSystem, bool withFlags) {
    const HeaderSearchOptions Opts(getCI()->getHeaderSearchOpts());

    if (withFlags && Opts.Sysroot != "/") {
      incpaths.push_back("-isysroot");
      incpaths.push_back(Opts.Sysroot);
    }

    /// User specified include entries.
    for (unsigned i = 0, e = Opts.UserEntries.size(); i != e; ++i) {
      const HeaderSearchOptions::Entry &E = Opts.UserEntries[i];
      if (E.IsFramework && E.Group != frontend::Angled)
        llvm::report_fatal_error("Invalid option set!");
      switch (E.Group) {
      case frontend::After:
        if (withFlags) incpaths.push_back("-idirafter");
        break;

      case frontend::Quoted:
        if (withFlags) incpaths.push_back("-iquote");
        break;

      case frontend::System:
        if (!withSystem) continue;
        if (withFlags) incpaths.push_back("-isystem");
        break;

      case frontend::IndexHeaderMap:
        if (!withSystem) continue;
        if (withFlags) incpaths.push_back("-index-header-map");
        if (withFlags) incpaths.push_back(E.IsFramework? "-F" : "-I");
        break;

      case frontend::CSystem:
        if (!withSystem) continue;
        if (withFlags) incpaths.push_back("-c-isystem");
        break;

      case frontend::ExternCSystem:
        if (!withSystem) continue;
        if (withFlags) incpaths.push_back("-extern-c-isystem");
        break;

      case frontend::CXXSystem:
        if (!withSystem) continue;
        if (withFlags) incpaths.push_back("-cxx-isystem");
        break;

      case frontend::ObjCSystem:
        if (!withSystem) continue;
        if (withFlags) incpaths.push_back("-objc-isystem");
        break;

      case frontend::ObjCXXSystem:
        if (!withSystem) continue;
        if (withFlags) incpaths.push_back("-objcxx-isystem");
        break;

      case frontend::Angled:
        if (withFlags) incpaths.push_back(E.IsFramework ? "-F" : "-I");
        break;
      }
      incpaths.push_back(E.Path);
    }

    if (withSystem && !Opts.ResourceDir.empty()) {
      if (withFlags) incpaths.push_back("-resource-dir");
      incpaths.push_back(Opts.ResourceDir);
    }
    if (withSystem && withFlags && !Opts.ModuleCachePath.empty()) {
      incpaths.push_back("-fmodule-cache-path");
      incpaths.push_back(Opts.ModuleCachePath);
    }
    if (withSystem && withFlags && !Opts.UseStandardSystemIncludes)
      incpaths.push_back("-nostdinc");
    if (withSystem && withFlags && !Opts.UseStandardCXXIncludes)
      incpaths.push_back("-nostdinc++");
    if (withSystem && withFlags && Opts.UseLibcxx)
      incpaths.push_back("-stdlib=libc++");
    if (withSystem && withFlags && Opts.Verbose)
      incpaths.push_back("-v");
  }

  CompilerInstance* Interpreter::getCI() const {
    return m_IncrParser->getCI();
  }

  const Sema& Interpreter::getSema() const {
    return getCI()->getSema();
  }

  Sema& Interpreter::getSema() {
    return getCI()->getSema();
  }

  llvm::ExecutionEngine* Interpreter::getExecutionEngine() const {
    if (!m_Executor) return 0;
    return m_Executor->getExecutionEngine();
  }

  ///\brief Maybe transform the input line to implement cint command line
  /// semantics (declarations are global) and compile to produce a module.
  ///
  Interpreter::CompilationResult
  Interpreter::process(const std::string& input, Value* V /* = 0 */,
                       Transaction** T /* = 0 */) {
    if (isRawInputEnabled() || !ShouldWrapInput(input))
      return declare(input, T);

    CompilationOptions CO;
    CO.DeclarationExtraction = 1;
    CO.ValuePrinting = CompilationOptions::VPAuto;
    CO.ResultEvaluation = (bool)V;
    CO.DynamicScoping = isDynamicLookupEnabled();
    CO.Debug = isPrintingDebug();
    if (EvaluateInternal(input, CO, V, T) == Interpreter::kFailure) {
      return Interpreter::kFailure;
    }

    return Interpreter::kSuccess;
  }

  Interpreter::CompilationResult 
  Interpreter::parse(const std::string& input, Transaction** T /*=0*/) const {
    CompilationOptions CO;
    CO.CodeGeneration = 0;
    CO.DeclarationExtraction = 0;
    CO.ValuePrinting = 0;
    CO.ResultEvaluation = 0;
    CO.DynamicScoping = isDynamicLookupEnabled();
    CO.Debug = isPrintingDebug();

    return DeclareInternal(input, CO, T);
  }

  Interpreter::CompilationResult
  Interpreter::loadModuleForHeader(const std::string& headerFile) {
    Preprocessor& PP = getCI()->getPreprocessor();
    //Copied from clang's PPDirectives.cpp
    bool isAngled = false;
    // Clang doc says:
    // "LookupFrom is set when this is a \#include_next directive, it specifies 
    // the file to start searching from."
    const DirectoryLookup* LookupFrom = 0;
    const DirectoryLookup* CurDir = 0;

    ModuleMap::KnownHeader suggestedModule;
    // PP::LookupFile uses it to issue 'nice' diagnostic
    SourceLocation fileNameLoc;
    PP.LookupFile(fileNameLoc, headerFile, isAngled, LookupFrom, CurDir,
                  /*SearchPath*/0, /*RelativePath*/ 0, &suggestedModule,
                  /*SkipCache*/false, /*OpenFile*/ false, /*CacheFail*/ false);
    if (!suggestedModule)
      return Interpreter::kFailure;

    // Copied from PPDirectives.cpp
    SmallVector<std::pair<IdentifierInfo *, SourceLocation>, 2> path;
    for (Module *mod = suggestedModule.getModule(); mod; mod = mod->Parent) {
      IdentifierInfo* II 
        = &getSema().getPreprocessor().getIdentifierTable().get(mod->Name);
      path.push_back(std::make_pair(II, fileNameLoc));
    }

    std::reverse(path.begin(), path.end());

    // Pretend that the module came from an inclusion directive, so that clang
    // will create an implicit import declaration to capture it in the AST.
    bool isInclude = true;
    SourceLocation includeLoc;
    if (getCI()->loadModule(includeLoc, path, Module::AllVisible, isInclude)) {
      // After module load we need to "force" Sema to generate the code for
      // things like dynamic classes.
      getSema().ActOnEndOfTranslationUnit();
      return Interpreter::kSuccess;
    }

    return Interpreter::kFailure;
  }

  Interpreter::CompilationResult
  Interpreter::parseForModule(const std::string& input) {
    CompilationOptions CO;
    CO.CodeGeneration = 1;
    CO.CodeGenerationForModule = 1;
    CO.DeclarationExtraction = 0;
    CO.ValuePrinting = 0;
    CO.ResultEvaluation = 0;
    CO.DynamicScoping = isDynamicLookupEnabled();
    CO.Debug = isPrintingDebug();

    // When doing parseForModule avoid warning about the user code
    // being loaded ... we probably might as well extend this to
    // ALL warnings ... but this will suffice for now (working
    // around a real bug in QT :().
    DiagnosticsEngine& Diag = getCI()->getDiagnostics();
    Diag.setDiagnosticMapping(clang::diag::warn_field_is_uninit,
                              clang::diag::MAP_IGNORE, SourceLocation());
    return DeclareInternal(input, CO);
  }
  
  Interpreter::CompilationResult
  Interpreter::declare(const std::string& input, Transaction** T/*=0 */) {
    CompilationOptions CO;
    CO.DeclarationExtraction = 0;
    CO.ValuePrinting = 0;
    CO.ResultEvaluation = 0;
    CO.DynamicScoping = isDynamicLookupEnabled();
    CO.Debug = isPrintingDebug();

    return DeclareInternal(input, CO, T);
  }

  Interpreter::CompilationResult
  Interpreter::evaluate(const std::string& input, Value& V) {
    // Here we might want to enforce further restrictions like: Only one
    // ExprStmt can be evaluated and etc. Such enforcement cannot happen in the
    // worker, because it is used from various places, where there is no such
    // rule
    CompilationOptions CO;
    CO.DeclarationExtraction = 0;
    CO.ValuePrinting = 0;
    CO.ResultEvaluation = 1;

    return EvaluateInternal(input, CO, &V);
  }

  Interpreter::CompilationResult
  Interpreter::echo(const std::string& input, Value* V /* = 0 */) {
    CompilationOptions CO;
    CO.DeclarationExtraction = 0;
    CO.ValuePrinting = CompilationOptions::VPEnabled;
    CO.ResultEvaluation = (bool)V;

    return EvaluateInternal(input, CO, V);
  }

  Interpreter::CompilationResult
  Interpreter::execute(const std::string& input) {
    CompilationOptions CO;
    CO.DeclarationExtraction = 0;
    CO.ValuePrinting = 0;
    CO.ResultEvaluation = 0;
    CO.DynamicScoping = 0;
    CO.Debug = isPrintingDebug();
    return EvaluateInternal(input, CO);
  }

  Interpreter::CompilationResult Interpreter::emitAllDecls(Transaction* T) {
    assert(!isInSyntaxOnlyMode() && "No CodeGenerator?");
    m_IncrParser->markWholeTransactionAsUsed(T);
    m_IncrParser->codeGenTransaction(T);

    // The static initializers might run anything and can thus cause more
    // decls that need to end up in a transaction. But this one is done
    // with CodeGen...
    T->setState(Transaction::kCommitted);
    if (runStaticInitializersOnce(*T))
      return Interpreter::kSuccess;

    return Interpreter::kFailure;
  }

  bool Interpreter::ShouldWrapInput(const std::string& input) {
    // TODO: For future reference.
    // Parser* P = const_cast<clang::Parser*>(m_IncrParser->getParser());
    // Parser::TentativeParsingAction TA(P);
    // TPResult result = P->isCXXDeclarationSpecifier();
    // TA.Revert();
    // return result == TPResult::True();

    // FIXME: can't skipToEndOfLine because we don't want to PragmaLex
    // because we don't want to pollute the preprocessor. Without PragmaLex
    // there is no "end of line" / eod token. So skip the #line before lexing.
    size_t posStart = 0;
    size_t lenInput = input.length();
    while (lenInput > posStart && isspace(input[posStart]))
      ++posStart;
    // Don't wrap empty input
    if (posStart == lenInput)
      return false;
    if (input[posStart] == '#') {
      size_t posDirective = posStart + 1;
      while (lenInput > posDirective && isspace(input[posDirective]))
        ++posDirective;
      // A single '#'? Weird... better don't wrap.
      if (posDirective == lenInput)
        return false;
      if (!strncmp(&input[posDirective], "line ", 5)) {
        // There is a line directive. It does affect the determination whether
        // this input should be wrapped; skip the line.
        size_t posEOD = input.find('\n', posDirective + 5);
        if (posEOD != std::string::npos)
          posStart = posEOD + 1;
      }
    }
    //llvm::OwningPtr<llvm::MemoryBuffer> buf;
    //buf.reset(llvm::MemoryBuffer::getMemBuffer(&input[posStart],
    //                                           "Cling Preparse Buf"));
    Lexer WrapLexer(SourceLocation(), getSema().getLangOpts(),
                    input.c_str() + posStart,
                    input.c_str() + posStart,
                    input.c_str() + input.size());
    Token Tok;
    WrapLexer.LexFromRawLexer(Tok);
    const tok::TokenKind kind = Tok.getKind();

    if (kind == tok::raw_identifier && !Tok.needsCleaning()) {
      StringRef keyword(Tok.getRawIdentifierData(), Tok.getLength());
      if (keyword.equals("using")) {
        // FIXME: Using definitions and declarations should be decl extracted.
        // Until we have that, don't wrap them if they are the only input.
        const char* cursor = keyword.data();
        cursor = strchr(cursor, ';'); // advance to end of using decl / def.
        if (!cursor) {
          // Using decl / def without trailing ';' means input consists of only
          // that using decl /def: should not wrap.
          return false;
        }
        // Skip whitespace after ';'
        do ++cursor;
        while (*cursor && isspace(*cursor));
        if (!*cursor)
          return false;
        // There is "more" - let's assume this input consists of a using
        // declaration or definition plus some code that should be wrapped.
        return true;
      }
      if (keyword.equals("extern"))
        return false;
      if (keyword.equals("namespace"))
        return false;
      if (keyword.equals("template"))
        return false;
    }
    else if (kind == tok::hash) {
      WrapLexer.LexFromRawLexer(Tok);
      if (Tok.is(tok::raw_identifier) && !Tok.needsCleaning()) {
        StringRef keyword(Tok.getRawIdentifierData(), Tok.getLength());
        if (keyword.equals("include"))
          return false;
      }
    }

    return true;
  }

  void Interpreter::WrapInput(std::string& input, std::string& fname) {
    fname = createUniqueWrapper();
    input.insert(0, "void " + fname + "(void* vpClingValue) {\n ");
    input.append("\n;\n}");
  }

  Interpreter::ExecutionResult
  Interpreter::RunFunction(const FunctionDecl* FD, Value* res /*=0*/) {
    if (getCI()->getDiagnostics().hasErrorOccurred())
      return kExeCompilationError;

    if (isInSyntaxOnlyMode()) {
      return kExeNoCodeGen;
    }

    if (!FD)
      return kExeUnkownFunction;

    std::string mangledNameIfNeeded;
    utils::Analyze::maybeMangleDeclName(FD, mangledNameIfNeeded);
    IncrementalExecutor::ExecutionResult ExeRes =
       m_Executor->executeFunction(mangledNameIfNeeded.c_str(), res);
    return ConvertExecutionResult(ExeRes);
  }

  const FunctionDecl* Interpreter::DeclareCFunction(StringRef name,
                                                    StringRef code,
                                                    bool withAccessControl) {
    /*
    In CallFunc we currently always (intentionally and somewhat necessarily)
    always fully specify member function template, however this can lead to
    an ambiguity with a class template.  For example in
    roottest/cling/functionTemplate we get:

    input_line_171:3:15: warning: lookup of 'set' in member access expression
    is ambiguous; using member of 't'
    ((t*)obj)->set<int>(*(int*)args[0]);
               ^
    roottest/cling/functionTemplate/t.h:19:9: note: lookup in the object type
    't' refers here
    void set(T targ) {
         ^
    /usr/include/c++/4.4.5/bits/stl_set.h:87:11: note: lookup from the
    current scope refers here
    class set
          ^
    This is an intention warning implemented in clang, see
    http://llvm.org/viewvc/llvm-project?view=revision&revision=105518

    which 'should have been' an error:

    C++ [basic.lookup.classref] requires this to be an error, but,
    because it's hard to work around, Clang downgrades it to a warning as
    an extension.</p>

    // C++98 [basic.lookup.classref]p1:
    // In a class member access expression (5.2.5), if the . or -> token is
    // immediately followed by an identifier followed by a <, the identifier
    // must be looked up to determine whether the < is the beginning of a
    // template argument list (14.2) or a less-than operator. The identifier
    // is first looked up in the class of the object expression. If the
    // identifier is not found, it is then looked up in the context of the
    // entire postfix-expression and shall name a class or function template. If
    // the lookup in the class of the object expression finds a template, the
    // name is also looked up in the context of the entire postfix-expression
    // and
    // -- if the name is not found, the name found in the class of the
    // object expression is used, otherwise
    // -- if the name is found in the context of the entire postfix-expression
    // and does not name a class template, the name found in the class of the
    // object expression is used, otherwise
    // -- if the name found is a class template, it must refer to the same
    // entity as the one found in the class of the object expression,
    // otherwise the program is ill-formed.

    See -Wambiguous-member-template

    An alternative to disabling the diagnostics is to use a pointer to
    member function:

    #include <set>
    using namespace std;

    extern "C" int printf(const char*,...);

    struct S {
    template <typename T>
    void set(T) {};

    virtual void virtua() { printf("S\n"); }
    };

    struct T: public S {
    void virtua() { printf("T\n"); }
    };

    int main() {
    S *s = new T();
    typedef void (S::*Func_p)(int);
    Func_p p = &S::set<int>;
    (s->*p)(12);

    typedef void (S::*Vunc_p)(void);
    Vunc_p q = &S::virtua;
    (s->*q)(); // prints "T"
    return 0;
    }
    */
    DiagnosticsEngine& Diag = getCI()->getDiagnostics();
    Diag.setDiagnosticMapping(
                       clang::diag::ext_nested_name_member_ref_lookup_ambiguous,
                       clang::diag::MAP_IGNORE, SourceLocation());


    LangOptions& LO = const_cast<LangOptions&>(getCI()->getLangOpts());
    bool savedAccessControl = LO.AccessControl;
    LO.AccessControl = withAccessControl;
    cling::Transaction* T = 0;
    cling::Interpreter::CompilationResult CR = declare(code, &T);
    LO.AccessControl = savedAccessControl;

    if (CR != cling::Interpreter::kSuccess)
      return 0;

    for (cling::Transaction::const_iterator I = T->decls_begin(),
           E = T->decls_end(); I != E; ++I) {
      if (I->m_Call != cling::Transaction::kCCIHandleTopLevelDecl)
        continue;
      if (const LinkageSpecDecl* LSD
          = dyn_cast<LinkageSpecDecl>(*I->m_DGR.begin())) {
        DeclContext::decl_iterator DeclBegin = LSD->decls_begin();
        if (DeclBegin == LSD->decls_end())
          continue;
        if (const FunctionDecl* D = dyn_cast<FunctionDecl>(*DeclBegin)) {
          const IdentifierInfo* II = D->getDeclName().getAsIdentifierInfo();
          if (II && II->getName() == name)
            return D;
        }
      }
    }
    return 0;
  }

  void*
  Interpreter::compileFunction(llvm::StringRef name, llvm::StringRef code,
                               bool ifUnique, bool withAccessControl) {
    //
    //  Compile the wrapper code.
    //
    const llvm::GlobalValue* GV = 0;
    if (isInSyntaxOnlyMode())
      return 0;

    if (ifUnique)
      GV = getLastTransaction()->getModule()->getNamedValue(name);

    if (!GV) {
      const FunctionDecl* FD = DeclareCFunction(name, code, withAccessControl);
      if (!FD) return 0;
      //
      //  Get the wrapper function pointer
      //  from the ExecutionEngine (the JIT).
      //
      GV = getLastTransaction()->getModule()->getNamedValue(name);
    }

    if (!GV)
      return 0;

    return m_Executor->getPointerToGlobalFromJIT(*GV);
  }

  void Interpreter::createUniqueName(std::string& out) {
    out += utils::Synthesize::UniquePrefix;
    llvm::raw_string_ostream(out) << m_UniqueCounter++;
  }

  bool Interpreter::isUniqueName(llvm::StringRef name) {
    return name.startswith(utils::Synthesize::UniquePrefix);
  }

  llvm::StringRef Interpreter::createUniqueWrapper() {
    const size_t size
      = sizeof(utils::Synthesize::UniquePrefix) + sizeof(m_UniqueCounter);
    llvm::SmallString<size> out(utils::Synthesize::UniquePrefix);
    llvm::raw_svector_ostream(out) << m_UniqueCounter++;

    return (getCI()->getASTContext().Idents.getOwn(out)).getName();
  }

  bool Interpreter::isUniqueWrapper(llvm::StringRef name) {
    return name.startswith(utils::Synthesize::UniquePrefix);
  }

  Interpreter::CompilationResult
  Interpreter::DeclareInternal(const std::string& input, 
                               const CompilationOptions& CO,
                               Transaction** T /* = 0 */) const {
    StateDebuggerRAII stateDebugger(this);

    if (Transaction* lastT = m_IncrParser->Compile(input, CO)) {
      if (lastT->getIssuedDiags() != Transaction::kErrors) {
        if (T)
          *T = lastT;
        return Interpreter::kSuccess;
      }
      return Interpreter::kFailure;     
    }

    // Even if the transaction was empty it is still success.
    return Interpreter::kSuccess;
  }

  Interpreter::CompilationResult
  Interpreter::EvaluateInternal(const std::string& input,
                                const CompilationOptions& CO,
                                Value* V, /* = 0 */
                                Transaction** T /* = 0 */) {
    StateDebuggerRAII stateDebugger(this);

    // Wrap the expression
    std::string WrapperName;
    std::string Wrapper = input;
    WrapInput(Wrapper, WrapperName);

    // Disable warnings which doesn't make sense when using the prompt
    // This gets reset with the clang::Diagnostics().Reset(/*soft*/=false)
    // using clang's API we simulate:
    // #pragma warning push
    // #pragma warning ignore ...
    // #pragma warning ignore ...
    // #pragma warning pop
    SourceLocation Loc = m_IncrParser->getLastMemoryBufferEndLoc();
    DiagnosticsEngine& Diags = getCI()->getDiagnostics();
    Diags.pushMappings(Loc);
    // The source locations of #pragma warning ignore must be greater than
    // the ones from #pragma push
    Loc = Loc.getLocWithOffset(1);
    Diags.setDiagnosticMapping(clang::diag::warn_unused_expr,
                               clang::diag::MAP_IGNORE, Loc);
    Diags.setDiagnosticMapping(clang::diag::warn_unused_call,
                               clang::diag::MAP_IGNORE, Loc);
    Diags.setDiagnosticMapping(clang::diag::warn_unused_comparison,
                               clang::diag::MAP_IGNORE, Loc);
    Diags.setDiagnosticMapping(clang::diag::ext_return_has_expr,
                               clang::diag::MAP_IGNORE, Loc);
    if (Transaction* lastT = m_IncrParser->Compile(Wrapper, CO)) {
      Loc = m_IncrParser->getLastMemoryBufferEndLoc().getLocWithOffset(1);
      // if the location was the same we are in recursive calls and to avoid an
      // assert in clang we should increment by a value.
      if (SourceLocation::getFromRawEncoding(m_LastCustomPragmaDiagPopPoint)
          == Loc)
        // Nested #pragma pop-s must be on different source locations.
        Loc = Loc.getLocWithOffset(1);
      m_LastCustomPragmaDiagPopPoint = Loc.getRawEncoding();

      Diags.popMappings(Loc);
      assert((lastT->getState() == Transaction::kCommitted
              || lastT->getState() == Transaction::kRolledBack)
             && "Not committed?");
      if (lastT->getIssuedDiags() != Transaction::kErrors) {
        Value resultV;
        if (!V)
          V = &resultV;
        if (!lastT->getWrapperFD()) // no wrapper to run
          return Interpreter::kSuccess;
        else if (RunFunction(lastT->getWrapperFD(), V) < kExeFirstError){
          if (lastT->getCompilationOpts().ValuePrinting
              != CompilationOptions::VPDisabled
              && V->isValid()
              // the !V->needsManagedAllocation() case is handled by
              // dumpIfNoStorage.
              && V->needsManagedAllocation())
            V->dump();
          return Interpreter::kSuccess;
        }
      }
      if (V)
        *V = Value();

      return Interpreter::kFailure;
    }
    Diags.popMappings(Loc.getLocWithOffset(1));
    return Interpreter::kSuccess;
  }

  std::string Interpreter::lookupFileOrLibrary(llvm::StringRef file) {
    std::string canonicalFile = DynamicLibraryManager::normalizePath(file);
    if (canonicalFile.empty())
      canonicalFile = file;
    const FileEntry* FE = 0;

    //Copied from clang's PPDirectives.cpp
    bool isAngled = false;
    // Clang doc says:
    // "LookupFrom is set when this is a \#include_next directive, it
    // specifies the file to start searching from."
    const DirectoryLookup* LookupFrom = 0;
    const DirectoryLookup* CurDir = 0;
    Preprocessor& PP = getCI()->getPreprocessor();
    // PP::LookupFile uses it to issue 'nice' diagnostic
    SourceLocation fileNameLoc;
    FE = PP.LookupFile(fileNameLoc, canonicalFile, isAngled, LookupFrom, CurDir,
                       /*SearchPath*/0, /*RelativePath*/ 0,
                       /*suggestedModule*/0, /*SkipCache*/false,
                       /*OpenFile*/ false, /*CacheFail*/ false);
    if (FE)
      return FE->getName();
    return getDynamicLibraryManager()->lookupLibrary(canonicalFile);
  }

  Interpreter::CompilationResult
  Interpreter::loadFile(const std::string& filename,
                        bool allowSharedLib /*=true*/) {
    DynamicLibraryManager* DLM = getDynamicLibraryManager();
    std::string canonicalLib = DLM->lookupLibrary(filename);
    if (allowSharedLib && !canonicalLib.empty()) {
      switch (DLM->loadLibrary(filename, /*permanent*/false)) {
      case DynamicLibraryManager::kLoadLibSuccess: // Intentional fall through
      case DynamicLibraryManager::kLoadLibAlreadyLoaded:
        return kSuccess;
      case DynamicLibraryManager::kLoadLibNotFound:
        assert(0 && "Cannot find library with existing canonical name!");
        return kFailure;
      default:
        // Not a source file (canonical name is non-empty) but can't load.
        return kFailure;
      }
    }

    std::string code;
    code += "#include \"" + filename + "\"";
    CompilationResult res = declare(code);
    return res;
  }

  void Interpreter::unload(unsigned numberOfTransactions) {
    while(true) {
      cling::Transaction* T = m_IncrParser->getLastTransaction();
      if (InterpreterCallbacks* callbacks = getCallbacks())
        callbacks->TransactionUnloaded(*T);
      if (m_Executor) // we also might be in fsyntax-only mode.
        m_Executor->runAndRemoveStaticDestructors(T);
      m_IncrParser->rollbackTransaction(T);

      if (!--numberOfTransactions)
        break;
    }

  }

  void Interpreter::installLazyFunctionCreator(void* (*fp)(const std::string&)) {
    m_Executor->installLazyFunctionCreator(fp);
  }

  Value Interpreter::Evaluate(const char* expr, DeclContext* DC,
                                       bool ValuePrinterReq) {
    Sema& TheSema = getCI()->getSema();
    // The evaluation should happen on the global scope, because of the wrapper
    // that is created. 
    //
    // We can't PushDeclContext, because we don't have scope.
    Sema::ContextRAII pushDC(TheSema, 
                             TheSema.getASTContext().getTranslationUnitDecl());

    Value Result;
    getCallbacks()->SetIsRuntime(true);
    if (ValuePrinterReq)
      echo(expr, &Result);
    else 
      evaluate(expr, Result);
    getCallbacks()->SetIsRuntime(false);

    return Result;
  }

  void Interpreter::setCallbacks(InterpreterCallbacks* C) {
    // We need it to enable LookupObject callback.
    m_Callbacks.reset(C);

    // FIXME: We should add a multiplexer in the ASTContext, too.
    llvm::IntrusiveRefCntPtr<ExternalASTSource>
      astContextExternalSource(getSema().getExternalSource());
    clang::ASTContext& Ctx = getSema().getASTContext();
    // FIXME: This is a gross hack. We must make multiplexer in the astcontext,
    // or a derived class that extends what we need.
    Ctx.ExternalSource.resetWithoutRelease(); // FIXME: make sure we delete it.
    Ctx.setExternalSource(astContextExternalSource);
    if (DynamicLibraryManager* DLM = getDynamicLibraryManager())
      DLM->setCallbacks(C);
  }

  const Transaction* Interpreter::getFirstTransaction() const {
    return m_IncrParser->getFirstTransaction();
  }

  const Transaction* Interpreter::getLastTransaction() const {
    return m_IncrParser->getLastTransaction();
  }

  const Transaction* Interpreter::getCurrentTransaction() const {
    return m_IncrParser->getCurrentTransaction();
  }


  void Interpreter::enableDynamicLookup(bool value /*=true*/) {
    m_DynamicLookupEnabled = value;

    if (isDynamicLookupEnabled()) {
     if (loadModuleForHeader("cling/Interpreter/DynamicLookupRuntimeUniverse.h")
         != kSuccess)
      declare("#include \"cling/Interpreter/DynamicLookupRuntimeUniverse.h\"");
    }
  }

  Interpreter::ExecutionResult
  Interpreter::runStaticInitializersOnce(const Transaction& T) const {
    assert(!isInSyntaxOnlyMode() && "Running on what?");
    assert(T.getState() == Transaction::kCommitted && "Must be committed");
    // Forward to IncrementalExecutor; should not be called by
    // anyone except for IncrementalParser.
    llvm::Module* module = m_IncrParser->getCodeGenerator()->GetModule();
    IncrementalExecutor::ExecutionResult ExeRes
       = m_Executor->runStaticInitializersOnce(module);

    // Avoid eternal additions to llvm.ident; see
    // CodeGenModule::EmitVersionIdentMetadata().
    llvm::NamedMDNode *IdentMetadata = module->getNamedMetadata("llvm.ident");
    if (IdentMetadata)
      module->eraseNamedMetadata(IdentMetadata);

    // Reset the module builder to clean up global initializers, c'tors, d'tors
    ASTContext& C = getCI()->getASTContext();
    m_IncrParser->getCodeGenerator()->HandleTranslationUnit(C);

    return ConvertExecutionResult(ExeRes);
  }

  bool Interpreter::addSymbol(const char* symbolName,  void* symbolAddress) {
    // Forward to IncrementalExecutor;
    if (!symbolName || !symbolAddress )
      return false;

    return m_Executor->addSymbol(symbolName, symbolAddress);
  }

  void* Interpreter::getAddressOfGlobal(const GlobalDecl& GD,
                                        bool* fromJIT /*=0*/) const {
    // Return a symbol's address, and whether it was jitted.
    std::string mangledName;
    utils::Analyze::maybeMangleDeclName(GD, mangledName);
    return getAddressOfGlobal(mangledName.c_str(), fromJIT);
  }

  void* Interpreter::getAddressOfGlobal(llvm::StringRef SymName,
                                        bool* fromJIT /*=0*/) const {
    // Return a symbol's address, and whether it was jitted.
    if (isInSyntaxOnlyMode())
      return 0;
    llvm::Module* module = getLastTransaction()->getModule();
    return m_Executor->getAddressOfGlobal(module, SymName, fromJIT);
  }

  void Interpreter::AddAtExitFunc(void (*Func) (void*), void* Arg) {
    m_Executor->AddAtExitFunc(Func, Arg, getLastTransaction());
  }

  void Interpreter::GenerateAutoloadingMap(llvm::StringRef inFile,
                                           llvm::StringRef outFile) {
    cling::Transaction* T;
    this->declare(std::string("#include \"") + std::string(inFile) + "\"", &T);
    for(auto dcit=T->decls_begin(); dcit!=T->decls_end(); ++dcit) {
      Transaction::DelayCallInfo& dci = *dcit;

      for(auto dit = dci.m_DGR.begin(); dit != dci.m_DGR.end(); ++dit) {
        clang::Decl* decl = *dit;
        auto visitor = new AutoloadingVisitor(inFile,outFile);
        visitor->TraverseDecl(decl);
        delete visitor;
      }

    }
    return;
  }
} // namespace cling
