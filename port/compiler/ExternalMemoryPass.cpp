// Guard calls leaving the set of memory-instrumented functions. Unknown calls
// stop before entering an unverified library; indirect calls require a finite
// registered checked target. No runtime code generation.
#include "llvm/ADT/StringSet.h"
#include "llvm/Analysis/ValueTracking.h"
#include "llvm/IR/IRBuilder.h"
#include "llvm/IR/InstIterator.h"
#include "llvm/IR/IntrinsicInst.h"
#include "llvm/IR/InlineAsm.h"
#include "llvm/IR/Module.h"
#include "llvm/Passes/PassBuilder.h"
#include "llvm/Passes/PassPlugin.h"
#include "llvm/Support/JSON.h"
#include "llvm/Support/MemoryBuffer.h"
#include "llvm/Support/Path.h"
#include "llvm/Support/raw_ostream.h"
#include "llvm/Transforms/Utils/ModuleUtils.h"
#include <cstdlib>
#include <map>
using namespace llvm;
namespace {
StringRef canonical(StringRef N){if(N.starts_with("\1")){N=N.drop_front();if(N.starts_with("_"))N=N.drop_front();}return N;}
bool containsPointer(Type *T){if(T->isPointerTy())return true;if(auto *S=dyn_cast<StructType>(T)){for(auto *E:S->elements())if(containsPointer(E))return true;}if(auto *A=dyn_cast<ArrayType>(T))return containsPointer(A->getElementType());if(auto *V=dyn_cast<VectorType>(T))return containsPointer(V->getElementType());return false;}
// Retain every check immediately before its access, but delay matching ends
// across at most eight ordinary accesses in one straight-line segment. The
// runtime's nested brackets then share one permission snapshot/OS read lock.
// Never hold it across a call, fence, atomic/volatile access or control flow.
static uint64_t groupAccessBrackets(Module &M){
 uint64_t Grouped=0;
 for(auto &F:M)for(auto &BB:F){
  SmallVector<Instruction *,128> Work;for(auto &I:BB)Work.push_back(&I);
  SmallVector<CallInst *,8> Ends;
  unsigned Accesses=0;
  auto Flush=[&](Instruction *Before){
   if(Ends.size()>1){
    // Each begin returns 0 for host memory or 1 for a retained guest bracket.
    // Retire the same bounded set together, at the former last-end boundary.
    IRBuilder<> B(Before);Value *Count=Ends.front()->getArgOperand(0);
    for(size_t n=1;n<Ends.size();++n)Count=B.CreateAdd(Count,Ends[n]->getArgOperand(0),"tribes.end.count");
    auto EndMany=M.getOrInsertFunction("tribes_memory_access_end_many",B.getVoidTy(),B.getInt32Ty());
    B.CreateCall(EndMany,{Count});Grouped+=Ends.size();
    for(auto *End:Ends)End->eraseFromParent();
   }
   Ends.clear();Accesses=0;
  };
  for(size_t i=0;i<Work.size();++i){
   auto *I=Work[i];auto *Begin=dyn_cast<CallInst>(I);
   if(I->getMetadata("tribes.checked.host.cpu")||I->getMetadata("tribes.checked.host.local")){
    auto *L=dyn_cast<LoadInst>(I);auto *S=dyn_cast<StoreInst>(I);
    if(!M.getModuleFlag("tribes.checked.cpu-host")||!((L&&L->isSimple())||(S&&S->isSimple())))report_fatal_error("Invalid checked CPU host access");
    // Native work before the first bracket holds no permission snapshot.
    // Count it only inside an active group, preserving the hold-time bound.
    if(!Ends.empty()&&++Accesses==8)Flush(I->getNextNode());
    continue;
   }
   if(Begin&&Begin->getCalledFunction()&&Begin->getCalledFunction()->getName()=="tribes_memory_access_begin"&&Begin->hasOneUse()&&i+2<Work.size()){
    auto *Access=Work[i+1];auto *End=dyn_cast<CallInst>(Work[i+2]);
    auto *Load=dyn_cast<LoadInst>(Access);auto *Store=dyn_cast<StoreInst>(Access);
    bool Ordinary=(Load&&Load->isSimple())||(Store&&Store->isSimple());
    if(Ordinary&&End&&End->getCalledFunction()&&End->getCalledFunction()->getName()=="tribes_memory_access_end"&&End->arg_size()==1&&End->getArgOperand(0)==Begin){
     Ends.push_back(End);i+=2;
     if(++Accesses==8)Flush(End->getNextNode());
     continue;
    }
   }
   if(isa<CallBase>(I)||I->mayReadOrWriteMemory()||isa<FenceInst>(I)||I->isTerminator())Flush(I);
  }
  if(!Ends.empty())report_fatal_error("Unterminated checked access group");
 }
 return Grouped;
}
struct ExternalMemoryPass : PassInfoMixin<ExternalMemoryPass>{
 PreservedAnalyses run(Module &M,ModuleAnalysisManager &){
  if(!M.getModuleFlag("tribes.checked.memory")||M.getModuleFlag("tribes.checked.externals"))report_fatal_error("External guard requires exactly one prior memory pass");
  const char *Path=std::getenv("TRIBES_EXTERNAL_POLICY");if(!Path)report_fatal_error("Missing explicit external policy");
  auto File=MemoryBuffer::getFile(Path);if(!File)report_fatal_error("Cannot read external policy");auto Parsed=json::parse((*File)->getBuffer());if(!Parsed)report_fatal_error("Invalid external policy JSON");auto *O=Parsed->getAsObject();if(!O)report_fatal_error("Invalid external policy object");
  StringSet<> Checked,Host,Range,Managers,Sources,Intrinsics,Strings,Callbacks,BarrierSources,ProcessorSources,SchedulingSources,FontSources;
  auto Read=[&](StringRef Key,StringSet<> &Set){auto *A=O->getArray(Key);if(!A)report_fatal_error(Twine("Missing policy list ")+Key);for(auto &V:*A){auto S=V.getAsString();if(!S)report_fatal_error("Nonstring policy entry");Set.insert(*S);}};
  Read("checked_symbols",Checked);Read("host_only",Host);Read("range_checked",Range);Read("memory_manager_calls",Managers);Read("memory_manager_sources",Sources);Read("host_intrinsics",Intrinsics);
  Read("checked_strings",Strings);
  Read("checked_callbacks",Callbacks);
  Read("barrier_asm_sources",BarrierSources);
  if(O->getArray("processor_snapshot_sources"))Read("processor_snapshot_sources",ProcessorSources);
  if(O->getArray("scheduling_sources"))Read("scheduling_sources",SchedulingSources);
  if(O->getArray("font_sources"))Read("font_sources",FontSources);
  std::map<uint64_t,unsigned> ScalarFcntl;auto *Commands=O->getArray("scalar_fcntl_commands");if(!Commands)report_fatal_error("Missing scalar fcntl policy");
  for(auto &V:*Commands){auto *A=V.getAsArray();if(!A||A->size()!=2)report_fatal_error("Invalid scalar fcntl entry");auto Command=(*A)[0].getAsInteger(),Arity=(*A)[1].getAsInteger();if(!Command||!Arity||*Command<0||*Command>INT32_MAX||(*Arity!=2&&*Arity!=3))report_fatal_error("Invalid scalar fcntl ABI");ScalarFcntl[*Command]=(unsigned)*Arity;}
  auto &C=M.getContext();auto *P=PointerType::getUnqual(C);auto *Void=Type::getVoidTy(C);auto *I32=Type::getInt32Ty(C);auto *I64=Type::getInt64Ty(C);
  auto *I1=Type::getInt1Ty(C);auto *I8=Type::getInt8Ty(C);auto *I16=Type::getInt16Ty(C);auto *Double=Type::getDoubleTy(C);
  // The reviewed font backend stages every native argument privately. Exact
  // C prototypes and source identity are required in addition to pointer gates.
  std::map<StringRef,FunctionType *> FontCalls;
  auto FontABI=[&](StringRef Name,Type *Result,std::initializer_list<Type *> Args){FontCalls[Name]=FunctionType::get(Result,ArrayRef<Type *>(Args),false);};
  FontABI("CFStringCreateWithCString",P,{P,P,I32});FontABI("CTFontCreateWithName",P,{P,Double,P});
  FontABI("CFRelease",Void,{P});FontABI("CTFontCopyPostScriptName",P,{P});
  FontABI("CFStringGetCString",I8,{P,P,I64,I32});FontABI("CFStringCreateWithBytes",P,{P,P,I64,I32,I8});
  FontABI("CFStringGetCharacterAtIndex",I16,{P,I64});FontABI("CTFontGetGlyphsForCharacters",I1,{P,P,P,I64});
  for(auto Name:{"CTFontGetAscent","CTFontGetDescent","CTFontGetSize","CTFontGetLeading"})FontABI(Name,Double,{P});
  FontABI("CTFontGetAdvancesForGlyphs",Double,{P,I32,P,P,I64});
  FontABI("CGColorSpaceCreateDeviceGray",P,{});FontABI("CGBitmapContextCreate",P,{P,I64,I64,I64,I64,P,I32});
  FontABI("CGColorSpaceRelease",Void,{P});FontABI("CGContextRelease",Void,{P});
  for(auto Name:{"CGContextSetShouldAntialias","CGContextSetShouldSmoothFonts","CGContextSetShouldSubpixelPositionFonts","CGContextSetShouldSubpixelQuantizeFonts"})FontABI(Name,Void,{P,I1});
  FontABI("CGContextSetGrayFillColor",Void,{P,Double,Double});FontABI("CTFontDrawGlyphs",Void,{P,P,P,I64,P});
  auto Gate=M.getOrInsertFunction("tribes_memory_external_gate",Void,P,I32);auto Pointer=M.getOrInsertFunction("tribes_memory_external_pointer",Void,P,P);auto Indirect=M.getOrInsertFunction("tribes_memory_indirect_target",Void,P);auto Register=M.getOrInsertFunction("tribes_memory_register_targets",Void,P,I64);
  auto Callback=M.getOrInsertFunction("tribes_memory_callback_target",Void,P,I32);
  SmallVector<Function *,128> Functions;for(auto &F:M)if(!F.isDeclaration())Functions.push_back(&F);
  uint64_t HostCalls=0,Blocked=0,IndirectCalls=0,RangeCalls=0,ManagerCalls=0,BarrierCalls=0;
  for(auto *F:Functions){
   SmallVector<CallBase *,64> Calls;for(auto &I:instructions(F))if(auto *Call=dyn_cast<CallBase>(&I))Calls.push_back(Call);
   for(auto *Call:Calls){
    auto *Target=Call->getCalledFunction();IRBuilder<> B(Call);
    if(!Target){if(Call->isInlineAsm()){
     auto *Asm=cast<InlineAsm>(Call->getCalledOperand());
     bool Barrier=BarrierSources.contains(M.getSourceFileName())&&F->getName()=="tb_sse_fence"&&Call->arg_size()==0&&Call->getType()->isVoidTy()&&Asm->hasSideEffects()&&Asm->getConstraintString()=="~{memory}"&&(Asm->getAsmString()=="dsb sy"||Asm->getAsmString()=="isb");
     if(Barrier){++BarrierCalls;continue;}
     auto *Name=B.CreateGlobalStringPtr("<inline-asm>");B.CreateCall(Gate,{Name,B.getInt32(0)});++Blocked;
    }else{B.CreateCall(Indirect,{Call->getCalledOperand()});++IndirectCalls;}continue;}
    auto Name=canonical(Target->getName());
    if(!Target->isDeclaration()||Checked.contains(Name)||Name.starts_with("tribes_memory_access_")||Name=="tribes_memory_transfer_begin")continue;
    // Pinned GL4ES exports use the same GL ABI and private host buffers.
    // Reuse only existing finite GL contracts and all their shape checks.
    // Apple's native GLES sync extension has exactly the existing core ABI.
    if(Name=="glFenceSyncAPPLE"||Name=="glIsSyncAPPLE"||Name=="glClientWaitSyncAPPLE"||Name=="glGetSyncivAPPLE"||Name=="glDeleteSyncAPPLE")Name=Name.drop_back(5);
    // Finite ANGLE adapter functions preserve the matching SDL host-only ABI.
    if(Name=="tribes_angle_create_context")Name="SDL_GL_CreateContext";
    if(Name=="tribes_angle_destroy_context")Name="SDL_GL_DestroyContext";
    if(Name=="tribes_angle_make_current")Name="SDL_GL_MakeCurrent";
    if(Name=="tribes_angle_current_context")Name="SDL_GL_GetCurrentContext";
    if(Name=="tribes_angle_current_window")Name="SDL_GL_GetCurrentWindow";
    if(Name=="tribes_angle_swap_window")Name="SDL_GL_SwapWindow";
    if(Name=="tribes_angle_set_swap_interval")Name="SDL_GL_SetSwapInterval";
    if(Name=="tribes_angle_get_swap_interval")Name="SDL_GL_GetSwapInterval";
    if(Name=="tribes_angle_get_attribute")Name="SDL_GL_GetAttribute";
    if(Name=="gl4es_glFogCoordPointer")Name="glFogCoordPointerEXT";
    if(Name.starts_with("gl4es_gl")&&Host.contains(Name.drop_front(6)))Name=Name.drop_front(6);
    if(Strings.contains(Name)){
     if(Name=="strcmp"||Name=="strncmp"){
      unsigned N=Name=="strcmp"?2:3;
      if(Call->arg_size()!=N||!Call->getType()->isIntegerTy(32)||!Call->getArgOperand(0)->getType()->isPointerTy()||!Call->getArgOperand(1)->getType()->isPointerTy()||(N==3&&!Call->getArgOperand(2)->getType()->isIntegerTy(64)))report_fatal_error("Invalid checked comparison ABI");
      auto Replacement=M.getOrInsertFunction(Name=="strcmp"?"tribes_memory_checked_strcmp":"tribes_memory_checked_strncmp",Target->getFunctionType());Call->setCalledFunction(Replacement);++RangeCalls;continue;
     }
     unsigned N=Name=="strnlen"?2:Name=="strlen"?1:0;
     if(!N||Call->arg_size()!=N||!Call->getType()->isIntegerTy(64)||!Call->getArgOperand(0)->getType()->isPointerTy()||(N==2&&!Call->getArgOperand(1)->getType()->isIntegerTy(64)))report_fatal_error("Invalid checked string ABI");
     auto Replacement=M.getOrInsertFunction(Name=="strnlen"?"tribes_memory_checked_strnlen":"tribes_memory_checked_strlen",Target->getFunctionType());Call->setCalledFunction(Replacement);++RangeCalls;continue;
    }
    if(Target->isIntrinsic()&&!Intrinsics.contains(Name)){
     if(isa<MemIntrinsic>(Call)||isa<LifetimeIntrinsic>(Call)||isa<DbgInfoIntrinsic>(Call)||Target->getIntrinsicID()==Intrinsic::assume||!Call->mayReadOrWriteMemory())continue;
     report_fatal_error(Twine("Missing intrinsic external contract: ")+Name);
    }
    if(Range.contains(Name)){
     // Require the bracketing calls inserted by the memory pass. The first
     // pass owns the exact buffer/length semantics; no name-only exemption.
     auto *Prev=dyn_cast_or_null<CallInst>(Call->getPrevNode());auto *Next=dyn_cast_or_null<CallInst>(Call->getNextNode());
     if(!Prev||!Next||!Prev->getCalledFunction()||!Next->getCalledFunction()||(Prev->getCalledFunction()->getName()!="tribes_memory_transfer_begin"&&Prev->getCalledFunction()->getName()!="tribes_memory_access_begin")||Next->getCalledFunction()->getName()!="tribes_memory_access_end"||Next->getArgOperand(0)!=Prev)report_fatal_error(Twine("Unbracketed library operation ")+Name);
     ++RangeCalls;continue;
    }
    if(Managers.contains(Name)&&Sources.contains(M.getSourceFileName())){++ManagerCalls;continue;}
    bool Allowed=Host.contains(Name)||Intrinsics.contains(Name);
    if(Name=="tribes_gl4es_context_create"||Name=="tribes_gl4es_context_activate"||Name=="tribes_gl4es_context_swap"||Name=="tribes_gl4es_context_destroy"){
     auto *Result=Name=="tribes_gl4es_context_create"?static_cast<Type *>(P):Name=="tribes_gl4es_context_swap"?static_cast<Type *>(B.getInt1Ty()):B.getVoidTy();
     unsigned Count=Name=="tribes_gl4es_context_create"?3:1;
     Allowed=Allowed&&!Target->isVarArg()&&Call->getType()==Result&&Call->arg_size()==Count&&Target->getCallingConv()==CallingConv::C&&Call->getCallingConv()==CallingConv::C;
     if(Allowed)for(auto &A:Call->args())if(A->getType()!=P)Allowed=false;
    }
    if(Name=="tribes_memory_code_revision")
     Allowed=Allowed&&!Target->isVarArg()&&Call->getType()==I32&&Call->arg_size()==1&&Call->getArgOperand(0)->getType()==I64;

    if(auto ABI=FontCalls.find(Name);ABI!=FontCalls.end()){
     Allowed=Allowed&&FontSources.contains(M.getSourceFileName())&&Target->getFunctionType()==ABI->second&&Call->getFunctionType()==ABI->second&&Target->getCallingConv()==CallingConv::C&&Call->getCallingConv()==CallingConv::C;
    }
    if(Name=="CTFontGetBoundingRectsForGlyphs"){
     // ARM64 returns CGRect as two nested two-double records. Pointer-bearing
     // aggregates, alternate layouts and changed calling conventions stop.
     auto *Rect=dyn_cast<StructType>(Call->getType());bool Shape=Rect&&!Rect->isOpaque()&&!Rect->isPacked()&&Rect->getNumElements()==2;
     if(Shape)for(auto *Element:Rect->elements()){
      auto *Pair=dyn_cast<StructType>(Element);if(!Pair||Pair->isOpaque()||Pair->isPacked()||Pair->getNumElements()!=2||Pair->getElementType(0)!=Double||Pair->getElementType(1)!=Double)Shape=false;
     }
     SmallVector<Type *,5> Args={P,I32,P,P,I64};
     Allowed=Allowed&&FontSources.contains(M.getSourceFileName())&&Shape&&!Target->isVarArg()&&Target->getReturnType()==Call->getType()&&Target->arg_size()==Args.size()&&Call->arg_size()==Args.size()&&Target->getCallingConv()==CallingConv::C&&Call->getCallingConv()==CallingConv::C;
     if(Allowed)for(unsigned i=0;i<Args.size();++i)if(Target->getFunctionType()->getParamType(i)!=Args[i]||Call->getArgOperand(i)->getType()!=Args[i])Allowed=false;
    }
    if(Name=="llvm.aarch64.hint"){
     // Only the state-preserving YIELD translation of x86 PAUSE. Other
     // AArch64 hints (including waits) must not inherit this contract.
     Allowed=Allowed&&!Target->isVarArg()&&Call->getType()->isVoidTy()&&Call->arg_size()==1;
     auto *Hint=Allowed?dyn_cast<ConstantInt>(Call->getArgOperand(0)):nullptr;
     Allowed=Allowed&&Hint&&Hint->getType()==I32&&Hint->getZExtValue()==1;
    }
    if(Name=="pthread_main_np")Allowed=Allowed&&!Target->isVarArg()&&Call->getType()==I32&&Call->arg_size()==0;
    if(Name=="read"){
     Allowed=Allowed&&!Target->isVarArg()&&Call->getType()==I64&&Call->arg_size()==3;
     if(Allowed)Allowed=Call->getArgOperand(0)->getType()==I32&&Call->getArgOperand(1)->getType()==P&&Call->getArgOperand(2)->getType()==I64;
    }
    if(Name=="send"||Name=="recv"){
     // Darwin ssize_t socket operations use private native payload staging.
     // Every pointer retains the host-only gate; no guest buffer exemption.
     Allowed=Allowed&&!Target->isVarArg()&&Call->getType()==I64&&Call->arg_size()==4;
     if(Allowed)Allowed=Call->getArgOperand(0)->getType()==I32&&Call->getArgOperand(1)->getType()==P&&Call->getArgOperand(2)->getType()==I64&&Call->getArgOperand(3)->getType()==I32;
    }
    if(Name=="sendto"||Name=="recvfrom"){
     // Flat Darwin datagram ABI. Payload, sockaddr and length pointers must
     // all be native-owned; nested recvmsg remains unsupported.
     Allowed=Allowed&&!Target->isVarArg()&&Call->getType()==I64&&Call->arg_size()==6;
     if(Allowed)Allowed=Call->getArgOperand(0)->getType()==I32&&Call->getArgOperand(1)->getType()==P&&Call->getArgOperand(2)->getType()==I64&&Call->getArgOperand(3)->getType()==I32&&Call->getArgOperand(4)->getType()==P&&(Name=="sendto"?Call->getArgOperand(5)->getType()==I32:Call->getArgOperand(5)->getType()==P);
    }
    if(Name=="fstatfs"){
     Allowed=Allowed&&!Target->isVarArg()&&Call->getType()==I32&&Call->arg_size()==2;
     if(Allowed)Allowed=Call->getArgOperand(0)->getType()==I32&&Call->getArgOperand(1)->getType()==P;
    }
    if(Name=="realpath"||Name=="realpath$DARWIN_EXTSN"){
     // Canonical paths stay in native-owned buffers. Both pointer arguments
     // still receive the host-only checks below, including the optional output.
     Allowed=Allowed&&!Target->isVarArg()&&Call->getType()==P&&Call->arg_size()==2&&
             Call->getArgOperand(0)->getType()==P&&Call->getArgOperand(1)->getType()==P;
    }
    if(Name=="pthread_cond_signal"||Name=="pthread_cond_timedwait_relative_np"){
     unsigned N=Name=="pthread_cond_signal"?1:3;
     Allowed=Allowed&&!Target->isVarArg()&&Call->getType()==I32&&Call->arg_size()==N;
     if(Allowed)for(unsigned i=0;i<N;++i)if(Call->getArgOperand(i)->getType()!=P)Allowed=false;
    }
    if(Name=="pthread_attr_init"||Name=="pthread_attr_destroy"||Name=="pthread_attr_setstacksize"||Name=="pthread_attr_setdetachstate"||Name=="_setjmp"||Name=="_longjmp"){
     SmallVector<Type *,2> Args={P};
     if(Name=="pthread_attr_setstacksize")Args.push_back(I64);
     if(Name=="pthread_attr_setdetachstate"||Name=="_longjmp")Args.push_back(I32);
     Allowed=Allowed&&!Target->isVarArg()&&Call->getType()==(Name=="_longjmp"?Void:I32)&&Call->arg_size()==Args.size();
     if(Allowed)for(unsigned i=0;i<Args.size();++i)if(Call->getArgOperand(i)->getType()!=Args[i])Allowed=false;
    }
    if(Name=="clock_getres"){
     // This provider queries only Darwin CLOCK_MONOTONIC (6), using a native
     // timespec. Guest output pointers are checked by the common host gate.
     Allowed=Allowed&&!Target->isVarArg()&&Call->getType()==I32&&Call->arg_size()==2;
     if(Allowed)Allowed=Call->getArgOperand(0)->getType()==I32&&Call->getArgOperand(1)->getType()==P;
     auto *Clock=Allowed?dyn_cast<ConstantInt>(Call->getArgOperand(0)):nullptr;
     Allowed=Allowed&&Clock&&Clock->getZExtValue()==6;
    }
    if(Name=="setpriority"){
     Allowed=Allowed&&SchedulingSources.contains(M.getSourceFileName())&&!Target->isVarArg()&&Call->getType()==I32&&Call->arg_size()==3;
     if(Allowed)for(unsigned i=0;i<3;++i)if(Call->getArgOperand(i)->getType()!=I32)Allowed=false;
     auto *Which=Allowed?dyn_cast<ConstantInt>(Call->getArgOperand(0)):nullptr;
     auto *Who=Allowed?dyn_cast<ConstantInt>(Call->getArgOperand(1)):nullptr;
     Allowed=Allowed&&Which&&Which->isZero()&&Who&&Who->isZero();
    }
    if(Name=="sysctlbyname"){
     Allowed=Allowed&&!Target->isVarArg()&&Call->getType()==I32&&Call->arg_size()==5;
     if(Allowed)for(unsigned i=0;i<4;++i)if(Call->getArgOperand(i)->getType()!=P)Allowed=false;
     if(Allowed)Allowed=isa<ConstantPointerNull>(Call->getArgOperand(3))&&Call->getArgOperand(4)->getType()==I64;
     auto *Size=Allowed?dyn_cast<ConstantInt>(Call->getArgOperand(4)):nullptr;
     Allowed=Allowed&&Size&&Size->isZero();
    }
    // Mach's deallocation address is an integer in the C ABI. Restrict these
    // snapshot calls to the source-bound service and check that address too.
    if(Name=="mach_host_self"||Name=="host_processor_info"||Name=="mach_port_deallocate"||Name=="vm_deallocate"){
     SmallVector<Type *,5> Args;
     if(Name=="host_processor_info")Args={I32,I32,P,P,P};
     else if(Name=="mach_port_deallocate")Args={I32,I32};
     else if(Name=="vm_deallocate")Args={I32,I64,I64};
     Allowed=Allowed&&ProcessorSources.contains(M.getSourceFileName())&&!Target->isVarArg()&&Call->getType()==I32&&Call->arg_size()==Args.size();
     if(Allowed)for(unsigned i=0;i<Args.size();++i)if(Call->getArgOperand(i)->getType()!=Args[i])Allowed=false;
     if(Allowed&&Name=="vm_deallocate")B.CreateCall(Pointer,{B.CreateGlobalStringPtr(Name),B.CreateIntToPtr(Call->getArgOperand(1),P)});
    }
    // Fixed Darwin socket/resource ABIs. Pointer arguments still pass through
    // the host-only range rejection below; no nested-buffer API is admitted.
    if(Name=="socket"||Name=="shutdown"||Name=="getrlimit"||Name=="setsockopt"||Name=="getsockopt"||Name=="bind"||Name=="connect"||Name=="getsockname"||Name=="getpeername"){
     SmallVector<Type *,5> Args;
     if(Name=="socket")Args={I32,I32,I32};
     else if(Name=="shutdown")Args={I32,I32};
     else if(Name=="getrlimit")Args={I32,P};
     else if(Name=="setsockopt")Args={I32,I32,I32,P,I32};
     else if(Name=="getsockopt")Args={I32,I32,I32,P,P};
     else if(Name=="bind"||Name=="connect")Args={I32,P,I32};
     else Args={I32,P,P};
     Allowed=Allowed&&!Target->isVarArg()&&Call->getType()==I32&&Call->arg_size()==Args.size();
     if(Allowed)for(unsigned i=0;i<Args.size();++i)if(Call->getArgOperand(i)->getType()!=Args[i])Allowed=false;
    }
    if(Name=="getaddrinfo"||Name=="freeaddrinfo"){
     unsigned count=Name=="getaddrinfo"?4:1;
     Allowed=Allowed&&!Target->isVarArg()&&Call->arg_size()==count&&(Name=="getaddrinfo"?Call->getType()==I32:Call->getType()->isVoidTy());
     if(Allowed)for(unsigned i=0;i<count;++i)if(Call->getArgOperand(i)->getType()!=P)Allowed=false;
    }
    if(Name=="getifaddrs"||Name=="freeifaddrs"){
     Allowed=Allowed&&!Target->isVarArg()&&Call->arg_size()==1&&Call->getArgOperand(0)->getType()==P&&(Name=="getifaddrs"?Call->getType()==I32:Call->getType()->isVoidTy());
    }
    if(Name=="arc4random_buf"){
     Allowed=Allowed&&!Target->isVarArg()&&Call->getType()->isVoidTy()&&Call->arg_size()==2&&Call->getArgOperand(0)->getType()==P&&Call->getArgOperand(1)->getType()==I64;
    }
    if(Name=="SDL_Init"||Name=="SDL_WasInit"||Name=="SDL_QuitSubSystem"){
     Allowed=Allowed&&!Target->isVarArg()&&Call->arg_size()==1&&Call->getArgOperand(0)->getType()==I32;
     if(Allowed)Allowed=Name=="SDL_Init"?Call->getType()->isIntegerTy(1):Name=="SDL_WasInit"?Call->getType()==I32:Call->getType()->isVoidTy();
    }
    if(Name=="SDL_GetPrimaryDisplay"||Name=="SDL_OutOfMemory"){
     Allowed=Allowed&&!Target->isVarArg()&&Call->arg_size()==0&&
             (Name=="SDL_GetPrimaryDisplay"?Call->getType()==I32:Call->getType()->isIntegerTy(1));
    }
    if(Name=="SDL_GetFullscreenDisplayModes")Allowed=Allowed&&!Target->isVarArg()&&Call->getType()==P&&Call->arg_size()==2&&Call->getArgOperand(0)->getType()==I32&&Call->getArgOperand(1)->getType()==P;
    if(Name=="SDL_GetDesktopDisplayMode"||Name=="SDL_GetCurrentDisplayMode"){
     Allowed=Allowed&&!Target->isVarArg()&&Call->getType()==P&&Call->arg_size()==1&&Call->getArgOperand(0)->getType()==I32;
    }
    if(Name=="SDL_GetClosestFullscreenDisplayMode"||Name=="SDL_SetWindowFullscreen"||Name=="SDL_SetWindowSize"||Name=="SDL_SetWindowBordered"||Name=="SDL_SetWindowPosition"){
     SmallVector<Type *,6> Args;
     if(Name=="SDL_GetClosestFullscreenDisplayMode")Args={I32,I32,I32,B.getFloatTy(),B.getInt1Ty(),P};
     else if(Name=="SDL_SetWindowFullscreen"||Name=="SDL_SetWindowBordered")Args={P,B.getInt1Ty()};
     else Args={P,I32,I32};
     Allowed=Allowed&&!Target->isVarArg()&&Call->getType()==B.getInt1Ty()&&Call->arg_size()==Args.size();
     if(Allowed)for(unsigned i=0;i<Args.size();++i)if(Call->getArgOperand(i)->getType()!=Args[i])Allowed=false;
    }
    if(Name=="SDL_GetWindowDisplayScale"||Name=="SDL_ceilf")
     Allowed=Allowed&&!Target->isVarArg()&&Call->getType()==B.getFloatTy()&&Call->arg_size()==1&&Call->getArgOperand(0)->getType()==(Name=="SDL_ceilf"?B.getFloatTy():P);
    if(Name=="SDL_randf")Allowed=Allowed&&!Target->isVarArg()&&Call->getType()==B.getFloatTy()&&Call->arg_size()==0;
    if(Name=="SDL_GetSIMDAlignment")Allowed=Allowed&&!Target->isVarArg()&&Call->getType()==I64&&Call->arg_size()==0;
    if(Name=="SDL_strcasecmp")Allowed=Allowed&&!Target->isVarArg()&&Call->getType()==I32&&Call->arg_size()==2&&Call->getArgOperand(0)->getType()==P&&Call->getArgOperand(1)->getType()==P;
    if(Name=="SDL_ReadU32LE")Allowed=Allowed&&!Target->isVarArg()&&Call->getType()==B.getInt1Ty()&&Call->arg_size()==2&&Call->getArgOperand(0)->getType()==P&&Call->getArgOperand(1)->getType()==P;
    if(Name=="SDL_GetError")Allowed=Allowed&&!Target->isVarArg()&&Call->getType()==P&&Call->arg_size()==0;
    if(Name=="SDL_IOFromFile"||Name=="SDL_IOFromConstMem"||Name=="SDL_LoadWAV_IO"||Name=="SDL_CloseIO"||Name=="SDL_SeekIO"||Name=="SDL_TellIO"||Name=="SDL_GetIOSize"||Name=="SDL_ReadIO"){
     SmallVector<Type *,5> Args;Type *Result=I64;
     if(Name=="SDL_IOFromFile"){Args={P,P};Result=P;}
     else if(Name=="SDL_IOFromConstMem"){Args={P,I64};Result=P;}
     else if(Name=="SDL_LoadWAV_IO"){Args={P,B.getInt1Ty(),P,P,P};Result=B.getInt1Ty();}
     else if(Name=="SDL_CloseIO"){Args={P};Result=B.getInt1Ty();}
     else if(Name=="SDL_SeekIO")Args={P,I64,I32};
     else if(Name=="SDL_ReadIO")Args={P,P,I64};
     else Args={P};
     Allowed=Allowed&&!Target->isVarArg()&&Call->getType()==Result&&Call->arg_size()==Args.size();
     if(Allowed)for(unsigned i=0;i<Args.size();++i)if(Call->getArgOperand(i)->getType()!=Args[i])Allowed=false;
    }
    if(Name=="SDL_GetDisplayForWindow"||Name=="SDL_SetWindowFullscreenMode"||Name=="SDL_GL_SetSwapInterval"){
     SmallVector<Type *,2> Args;Type *Result=B.getInt1Ty();
     if(Name=="SDL_GetDisplayForWindow"){Args={P};Result=I32;}
     else if(Name=="SDL_SetWindowFullscreenMode")Args={P,P};
     else Args={I32};
     Allowed=Allowed&&!Target->isVarArg()&&Call->getType()==Result&&Call->arg_size()==Args.size();
     if(Allowed)for(unsigned i=0;i<Args.size();++i)if(Call->getArgOperand(i)->getType()!=Args[i])Allowed=false;
    }
    if(Name=="SDL_CreateMutex"||Name=="SDL_CreateCondition"){
     Allowed=Allowed&&!Target->isVarArg()&&Call->arg_size()==0&&Call->getType()==P;
    }
    if(Name=="SDL_DestroyMutex"||Name=="SDL_LockMutex"||Name=="SDL_TryLockMutex"||Name=="SDL_UnlockMutex"||Name=="SDL_DestroyCondition"||Name=="SDL_SignalCondition"||Name=="SDL_BroadcastCondition"){
     Allowed=Allowed&&!Target->isVarArg()&&Call->arg_size()==1&&Call->getArgOperand(0)->getType()==P&&(Name=="SDL_TryLockMutex"?Call->getType()->isIntegerTy(1):Call->getType()->isVoidTy());
    }
    if(Name=="SDL_WaitConditionTimeout"){
     Allowed=Allowed&&!Target->isVarArg()&&Call->arg_size()==3&&Call->getType()->isIntegerTy(1)&&Call->getArgOperand(0)->getType()==P&&Call->getArgOperand(1)->getType()==P&&Call->getArgOperand(2)->getType()==I32;
    }
    if(Name=="SDL_GL_GetSwapInterval")Allowed=Allowed&&!Target->isVarArg()&&Call->getType()->isIntegerTy(1)&&Call->arg_size()==1&&Call->getArgOperand(0)->getType()==P;
    if(Name=="SDL_PollEvent")Allowed=Allowed&&!Target->isVarArg()&&Call->getType()->isIntegerTy(1)&&Call->arg_size()==1&&Call->getArgOperand(0)->getType()==P;
    if(Name=="SDL_PumpEvents")Allowed=Allowed&&!Target->isVarArg()&&Call->getType()->isVoidTy()&&Call->arg_size()==0;
    if(Name=="SDL_GetKeyboardState"||Name=="SDL_GetGamepads"||Name=="SDL_GetGamepadName")
     Allowed=Allowed&&!Target->isVarArg()&&Call->getType()==P&&Call->arg_size()==1&&Call->getArgOperand(0)->getType()==P;
    if(Name=="SDL_OpenGamepad")Allowed=Allowed&&!Target->isVarArg()&&Call->getType()==P&&Call->arg_size()==1&&Call->getArgOperand(0)->getType()==I32;
    if(Name=="SDL_HasJoystick")Allowed=Allowed&&!Target->isVarArg()&&Call->getType()->isIntegerTy(1)&&Call->arg_size()==0;
    if(Name=="SDL_CloseGamepad"||Name=="SDL_free")Allowed=Allowed&&!Target->isVarArg()&&Call->getType()->isVoidTy()&&Call->arg_size()==1&&Call->getArgOperand(0)->getType()==P;
    if(Name=="SDL_GetTLS")Allowed=Allowed&&!Target->isVarArg()&&Call->getType()==P&&Call->arg_size()==1&&Call->getArgOperand(0)->getType()==P;
    if(Name=="SDL_CreateWindow"||Name=="SDL_DestroyWindow"||Name=="SDL_ShowWindow"||Name=="SDL_SetWindowRelativeMouseMode"||Name=="SDL_SyncWindow"||Name=="SDL_GetWindowID"||Name=="SDL_GetWindowProperties"||Name=="SDL_GetWindowSize"||Name=="SDL_GetWindowSizeInPixels"||Name=="SDL_GL_CreateContext"||Name=="SDL_GL_DestroyContext"||Name=="SDL_GL_MakeCurrent"||Name=="SDL_GL_GetCurrentWindow"||Name=="SDL_GL_GetCurrentContext"||Name=="SDL_GL_SwapWindow"||Name=="SDL_GL_SetAttribute"||Name=="SDL_GL_GetAttribute"||Name=="SDL_GL_ResetAttributes"){
     SmallVector<Type *,4> Args;Type *Result=B.getInt1Ty();
     if(Name=="SDL_CreateWindow"){Args={P,I32,I32,I64};Result=P;}
     else if(Name=="SDL_DestroyWindow"){Args={P};Result=B.getVoidTy();}
     else if(Name=="SDL_SetWindowRelativeMouseMode")Args={P,B.getInt1Ty()};
     else if(Name=="SDL_GetWindowID"||Name=="SDL_GetWindowProperties"){Args={P};Result=I32;}
     else if(Name=="SDL_GetWindowSize"||Name=="SDL_GetWindowSizeInPixels")Args={P,P,P};
     else if(Name=="SDL_GL_CreateContext"){Args={P};Result=P;}
     else if(Name=="SDL_GL_MakeCurrent")Args={P,P};
     else if(Name=="SDL_GL_GetCurrentWindow"||Name=="SDL_GL_GetCurrentContext")Result=P;
     else if(Name=="SDL_GL_SetAttribute")Args={I32,I32};
     else if(Name=="SDL_GL_GetAttribute")Args={I32,P};
     else if(Name=="SDL_GL_ResetAttributes")Result=B.getVoidTy();
     else Args={P};
     Allowed=Allowed&&!Target->isVarArg()&&Call->getType()==Result&&Call->arg_size()==Args.size();
     if(Allowed)for(unsigned i=0;i<Args.size();++i)if(Call->getArgOperand(i)->getType()!=Args[i])Allowed=false;
    }
    if(Name=="SDL_OpenAudioDevice"||Name=="SDL_GetAudioDeviceFormat"||Name=="SDL_PauseAudioDevice"||Name=="SDL_CloseAudioDevice"||Name=="SDL_CreateAudioStream"||Name=="SDL_BindAudioStream"||Name=="SDL_DestroyAudioStream"||Name=="SDL_ResumeAudioStreamDevice"||Name=="SDL_PauseAudioStreamDevice"||Name=="SDL_ClearAudioStream"||Name=="SDL_GetAudioStreamAvailable"||Name=="SDL_GetAudioStreamData"||Name=="SDL_PutAudioStreamData"||Name=="SDL_FlushAudioStream"){
     SmallVector<Type *,3> Args;Type *Result=B.getInt1Ty();
     if(Name=="SDL_OpenAudioDevice"){Args={I32,P};Result=I32;}
     else if(Name=="SDL_GetAudioDeviceFormat")Args={I32,P,P};
     else if(Name=="SDL_PauseAudioDevice")Args={I32};
     else if(Name=="SDL_CloseAudioDevice"){Args={I32};Result=B.getVoidTy();}
     else if(Name=="SDL_CreateAudioStream"){Args={P,P};Result=P;}
     else if(Name=="SDL_BindAudioStream")Args={I32,P};
     else if(Name=="SDL_DestroyAudioStream"){Args={P};Result=B.getVoidTy();}
     else if(Name=="SDL_GetAudioStreamAvailable"){Args={P};Result=I32;}
     else if(Name=="SDL_GetAudioStreamData"){Args={P,P,I32};Result=I32;}
     else if(Name=="SDL_PutAudioStreamData")Args={P,P,I32};
     else Args={P};
     Allowed=Allowed&&!Target->isVarArg()&&Call->getType()==Result&&Call->arg_size()==Args.size();
     if(Allowed)for(unsigned i=0;i<Args.size();++i)if(Call->getArgOperand(i)->getType()!=Args[i])Allowed=false;
    }
    if(Name=="SDL_CreateSurfaceFrom"||Name=="SDL_ConvertSurface"||Name=="SDL_PremultiplySurfaceAlpha"||Name=="SDL_DestroySurface"||Name=="SDL_RemoveSurfaceAlternateImages"||Name=="SDL_CreateColorCursor"||Name=="SDL_SetCursor"||Name=="SDL_DestroyCursor"){
     SmallVector<Type *,5> Args;Type *Result=P;
     if(Name=="SDL_CreateSurfaceFrom")Args={I32,I32,I32,P,I32};
     else if(Name=="SDL_ConvertSurface")Args={P,I32};
     else if(Name=="SDL_PremultiplySurfaceAlpha"){Args={P,B.getInt1Ty()};Result=B.getInt1Ty();}
     else if(Name=="SDL_CreateColorCursor")Args={P,I32,I32};
     else if(Name=="SDL_SetCursor"){Args={P};Result=B.getInt1Ty();}
     else {Args={P};Result=B.getVoidTy();}
     Allowed=Allowed&&!Target->isVarArg()&&Call->getType()==Result&&Call->arg_size()==Args.size();
     if(Allowed)for(unsigned i=0;i<Args.size();++i)if(Call->getArgOperand(i)->getType()!=Args[i])Allowed=false;
    }
    if(Name=="gluErrorString"||Name=="glGetString"||Name=="glGetStringi"||Name=="glGetError"||Name=="glGetIntegerv"||Name=="glGetFloatv"){
     SmallVector<Type *,2> Args;Type *Result=I32;
     if(Name=="glGetString"||Name=="gluErrorString"){Args={I32};Result=P;}
     else if(Name=="glGetStringi"){Args={I32,I32};Result=P;}
     else if(Name=="glGetIntegerv"||Name=="glGetFloatv"){Args={I32,P};Result=B.getVoidTy();}
     Allowed=Allowed&&!Target->isVarArg()&&Call->getType()==Result&&Call->arg_size()==Args.size();
     if(Allowed)for(unsigned i=0;i<Args.size();++i)if(Call->getArgOperand(i)->getType()!=Args[i])Allowed=false;
    }
    if(Name=="glIsSync"||Name=="glGetSynciv"||Name=="glFenceSync"||Name=="glClientWaitSync"||Name=="glDeleteSync"){
     SmallVector<Type *,3> Args={I32,I32};Type *Result=P;
     if(Name=="glIsSync"){Args={P};Result=B.getInt8Ty();}
     else if(Name=="glClientWaitSync"){Args={P,I32,I64};Result=I32;}
     else if(Name=="glDeleteSync"){Args={P};Result=B.getVoidTy();}
     else if(Name=="glGetSynciv"){Args={P,I32,I32,P,P};Result=B.getVoidTy();}
     Allowed=Allowed&&!Target->isVarArg()&&Call->getType()==Result&&Call->arg_size()==Args.size();
     if(Allowed)for(unsigned i=0;i<Args.size();++i)if(Call->getArgOperand(i)->getType()!=Args[i])Allowed=false;
    }
    if(Name=="glClearColor"||Name=="glBindFramebuffer"||Name=="glClear"||Name=="glDrawBuffer"||Name=="glFlush"||Name=="glFinish"||Name=="glPixelStorei"||Name=="SDL_GetNumberProperty"){
     SmallVector<Type *,4> Args;Type *Result=B.getVoidTy();
     if(Name=="glClearColor")Args={B.getFloatTy(),B.getFloatTy(),B.getFloatTy(),B.getFloatTy()};
     else if(Name=="glBindFramebuffer"||Name=="glPixelStorei")Args={I32,I32};
     else if(Name=="glClear"||Name=="glDrawBuffer")Args={I32};
     else if(Name=="SDL_GetNumberProperty"){Args={I32,P,I64};Result=I64;}
     Allowed=Allowed&&!Target->isVarArg()&&Call->getType()==Result&&Call->arg_size()==Args.size();
     if(Allowed)for(unsigned i=0;i<Args.size();++i)if(Call->getArgOperand(i)->getType()!=Args[i])Allowed=false;
    }
    if(Name=="glGenTextures"||Name=="glDeleteTextures"||Name=="glBindTexture"||Name=="glTexParameteri"||Name=="glTexParameterf"||Name=="glTexImage2D"||Name=="glTexSubImage2D"||Name=="glGenerateMipmap"||Name=="glGetTexParameteriv"){
     SmallVector<Type *,9> Args={I32,I32};
     if(Name=="glGenTextures"||Name=="glDeleteTextures")Args={I32,P};
     else if(Name=="glTexParameteri")Args.push_back(I32);
     else if(Name=="glTexParameterf")Args.push_back(B.getFloatTy());
     else if(Name=="glTexImage2D"||Name=="glTexSubImage2D")Args={I32,I32,I32,I32,I32,I32,I32,I32,P};
     else if(Name=="glGenerateMipmap")Args={I32};
     else if(Name=="glGetTexParameteriv")Args={I32,I32,P};
     Allowed=Allowed&&!Target->isVarArg()&&Call->getType()->isVoidTy()&&Call->arg_size()==Args.size();
     if(Allowed)for(unsigned i=0;i<Args.size();++i)if(Call->getArgOperand(i)->getType()!=Args[i])Allowed=false;
    }
    if(Name=="glTexImage3D"||Name=="glTexSubImage3D"){
     SmallVector<Type *,11> Args(9,I32);if(Name=="glTexSubImage3D")Args.push_back(I32);Args.push_back(P);
     Allowed=Allowed&&!Target->isVarArg()&&Call->getType()->isVoidTy()&&Call->arg_size()==Args.size();
     if(Allowed)for(unsigned i=0;i<Args.size();++i)if(Call->getArgOperand(i)->getType()!=Args[i])Allowed=false;
    }
    if(Name=="glGenBuffers"||Name=="glDeleteBuffers"||Name=="glBindBuffer"||Name=="glBufferData"){
     SmallVector<Type *,4> Args={I32,P};
     if(Name=="glBindBuffer")Args={I32,I32};
     else if(Name=="glBufferData")Args={I32,I64,P,I32};
     Allowed=Allowed&&!Target->isVarArg()&&Call->getType()->isVoidTy()&&Call->arg_size()==Args.size();
     if(Allowed)for(unsigned i=0;i<Args.size();++i)if(Call->getArgOperand(i)->getType()!=Args[i])Allowed=false;
    }
    if(Name=="glFrustum"){
     SmallVector<Type *,6> Args(6,B.getDoubleTy());
     Allowed=Allowed&&!Target->isVarArg()&&Call->getType()==B.getVoidTy()&&Call->arg_size()==Args.size();
     if(Allowed)for(unsigned i=0;i<Args.size();++i)if(Call->getArgOperand(i)->getType()!=Args[i])Allowed=false;
    }
    if(Name=="glDepthRange"){
     SmallVector<Type *,7> Args={B.getDoubleTy(),B.getDoubleTy()};
     Allowed=Allowed&&!Target->isVarArg()&&Call->getType()==B.getVoidTy()&&Call->arg_size()==Args.size();
     if(Allowed)for(unsigned i=0;i<Args.size();++i)if(Call->getArgOperand(i)->getType()!=Args[i])Allowed=false;
    }
    if(Name=="glClearDepth"){
     SmallVector<Type *,7> Args={B.getDoubleTy()};
     Allowed=Allowed&&!Target->isVarArg()&&Call->getType()==B.getVoidTy()&&Call->arg_size()==Args.size();
     if(Allowed)for(unsigned i=0;i<Args.size();++i)if(Call->getArgOperand(i)->getType()!=Args[i])Allowed=false;
    }
    if(Name=="glDepthRangef"){
     SmallVector<Type *,7> Args={B.getFloatTy(),B.getFloatTy()};
     Allowed=Allowed&&!Target->isVarArg()&&Call->getType()==B.getVoidTy()&&Call->arg_size()==Args.size();
     if(Allowed)for(unsigned i=0;i<Args.size();++i)if(Call->getArgOperand(i)->getType()!=Args[i])Allowed=false;
    }
    if(Name=="glClearDepthf"){
     SmallVector<Type *,7> Args={B.getFloatTy()};
     Allowed=Allowed&&!Target->isVarArg()&&Call->getType()==B.getVoidTy()&&Call->arg_size()==Args.size();
     if(Allowed)for(unsigned i=0;i<Args.size();++i)if(Call->getArgOperand(i)->getType()!=Args[i])Allowed=false;
    }
    if(Name=="glDepthFunc"){
     SmallVector<Type *,7> Args={I32};
     Allowed=Allowed&&!Target->isVarArg()&&Call->getType()==B.getVoidTy()&&Call->arg_size()==Args.size();
     if(Allowed)for(unsigned i=0;i<Args.size();++i)if(Call->getArgOperand(i)->getType()!=Args[i])Allowed=false;
    }
    if(Name=="glCreateShader"){
     SmallVector<Type *,7> Args={I32};
     Allowed=Allowed&&!Target->isVarArg()&&Call->getType()==I32&&Call->arg_size()==Args.size();
     if(Allowed)for(unsigned i=0;i<Args.size();++i)if(Call->getArgOperand(i)->getType()!=Args[i])Allowed=false;
    }
    if(Name=="glShaderSource"){
     SmallVector<Type *,7> Args={I32,I32,P,P};
     Allowed=Allowed&&!Target->isVarArg()&&Call->getType()==B.getVoidTy()&&Call->arg_size()==Args.size();
     if(Allowed)for(unsigned i=0;i<Args.size();++i)if(Call->getArgOperand(i)->getType()!=Args[i])Allowed=false;
    }
    if(Name=="glCompileShader"){
     SmallVector<Type *,7> Args={I32};
     Allowed=Allowed&&!Target->isVarArg()&&Call->getType()==B.getVoidTy()&&Call->arg_size()==Args.size();
     if(Allowed)for(unsigned i=0;i<Args.size();++i)if(Call->getArgOperand(i)->getType()!=Args[i])Allowed=false;
    }
    if(Name=="glGetShaderiv"){
     SmallVector<Type *,7> Args={I32,I32,P};
     Allowed=Allowed&&!Target->isVarArg()&&Call->getType()==B.getVoidTy()&&Call->arg_size()==Args.size();
     if(Allowed)for(unsigned i=0;i<Args.size();++i)if(Call->getArgOperand(i)->getType()!=Args[i])Allowed=false;
    }
    if(Name=="glGetShaderInfoLog"){
     SmallVector<Type *,7> Args={I32,I32,P,P};
     Allowed=Allowed&&!Target->isVarArg()&&Call->getType()==B.getVoidTy()&&Call->arg_size()==Args.size();
     if(Allowed)for(unsigned i=0;i<Args.size();++i)if(Call->getArgOperand(i)->getType()!=Args[i])Allowed=false;
    }
    if(Name=="glDeleteShader"){
     SmallVector<Type *,7> Args={I32};
     Allowed=Allowed&&!Target->isVarArg()&&Call->getType()==B.getVoidTy()&&Call->arg_size()==Args.size();
     if(Allowed)for(unsigned i=0;i<Args.size();++i)if(Call->getArgOperand(i)->getType()!=Args[i])Allowed=false;
    }
    if(Name=="glCreateProgram"){
     SmallVector<Type *,7> Args={};
     Allowed=Allowed&&!Target->isVarArg()&&Call->getType()==I32&&Call->arg_size()==Args.size();
     if(Allowed)for(unsigned i=0;i<Args.size();++i)if(Call->getArgOperand(i)->getType()!=Args[i])Allowed=false;
    }
    if(Name=="glAttachShader"){
     SmallVector<Type *,7> Args={I32,I32};
     Allowed=Allowed&&!Target->isVarArg()&&Call->getType()==B.getVoidTy()&&Call->arg_size()==Args.size();
     if(Allowed)for(unsigned i=0;i<Args.size();++i)if(Call->getArgOperand(i)->getType()!=Args[i])Allowed=false;
    }
    if(Name=="glLinkProgram"){
     SmallVector<Type *,7> Args={I32};
     Allowed=Allowed&&!Target->isVarArg()&&Call->getType()==B.getVoidTy()&&Call->arg_size()==Args.size();
     if(Allowed)for(unsigned i=0;i<Args.size();++i)if(Call->getArgOperand(i)->getType()!=Args[i])Allowed=false;
    }
    if(Name=="glGetProgramiv"){
     SmallVector<Type *,7> Args={I32,I32,P};
     Allowed=Allowed&&!Target->isVarArg()&&Call->getType()==B.getVoidTy()&&Call->arg_size()==Args.size();
     if(Allowed)for(unsigned i=0;i<Args.size();++i)if(Call->getArgOperand(i)->getType()!=Args[i])Allowed=false;
    }
    if(Name=="glGetProgramInfoLog"){
     SmallVector<Type *,7> Args={I32,I32,P,P};
     Allowed=Allowed&&!Target->isVarArg()&&Call->getType()==B.getVoidTy()&&Call->arg_size()==Args.size();
     if(Allowed)for(unsigned i=0;i<Args.size();++i)if(Call->getArgOperand(i)->getType()!=Args[i])Allowed=false;
    }
    if(Name=="glDeleteProgram"){
     SmallVector<Type *,7> Args={I32};
     Allowed=Allowed&&!Target->isVarArg()&&Call->getType()==B.getVoidTy()&&Call->arg_size()==Args.size();
     if(Allowed)for(unsigned i=0;i<Args.size();++i)if(Call->getArgOperand(i)->getType()!=Args[i])Allowed=false;
    }
    if(Name=="glGetActiveUniform"){
     SmallVector<Type *,7> Args={I32,I32,I32,P,P,P,P};
     Allowed=Allowed&&!Target->isVarArg()&&Call->getType()==B.getVoidTy()&&Call->arg_size()==Args.size();
     if(Allowed)for(unsigned i=0;i<Args.size();++i)if(Call->getArgOperand(i)->getType()!=Args[i])Allowed=false;
    }
    if(Name=="glGetActiveAttrib"){
     SmallVector<Type *,7> Args={I32,I32,I32,P,P,P,P};
     Allowed=Allowed&&!Target->isVarArg()&&Call->getType()==B.getVoidTy()&&Call->arg_size()==Args.size();
     if(Allowed)for(unsigned i=0;i<Args.size();++i)if(Call->getArgOperand(i)->getType()!=Args[i])Allowed=false;
    }
    if(Name=="glGetUniformLocation"){
     SmallVector<Type *,7> Args={I32,P};
     Allowed=Allowed&&!Target->isVarArg()&&Call->getType()==I32&&Call->arg_size()==Args.size();
     if(Allowed)for(unsigned i=0;i<Args.size();++i)if(Call->getArgOperand(i)->getType()!=Args[i])Allowed=false;
    }
    if(Name=="glGetAttribLocation"){
     SmallVector<Type *,7> Args={I32,P};
     Allowed=Allowed&&!Target->isVarArg()&&Call->getType()==I32&&Call->arg_size()==Args.size();
     if(Allowed)for(unsigned i=0;i<Args.size();++i)if(Call->getArgOperand(i)->getType()!=Args[i])Allowed=false;
    }
    if(Name=="glUniform1i"){
     SmallVector<Type *,7> Args={I32,I32};
     Allowed=Allowed&&!Target->isVarArg()&&Call->getType()==B.getVoidTy()&&Call->arg_size()==Args.size();
     if(Allowed)for(unsigned i=0;i<Args.size();++i)if(Call->getArgOperand(i)->getType()!=Args[i])Allowed=false;
    }
    if(Name=="glUniform4fv"){
     SmallVector<Type *,3> Args={I32,I32,P};
     Allowed=Allowed&&!Target->isVarArg()&&Call->getType()==B.getVoidTy()&&Call->arg_size()==Args.size();
     if(Allowed)for(unsigned i=0;i<Args.size();++i)if(Call->getArgOperand(i)->getType()!=Args[i])Allowed=false;
    }
    if(Name=="glUniform1f"){
     SmallVector<Type *,7> Args={I32,B.getFloatTy()};
     Allowed=Allowed&&!Target->isVarArg()&&Call->getType()==B.getVoidTy()&&Call->arg_size()==Args.size();
     if(Allowed)for(unsigned i=0;i<Args.size();++i)if(Call->getArgOperand(i)->getType()!=Args[i])Allowed=false;
    }
    if(Name=="glPushMatrix"||Name=="glPopMatrix"||Name=="glScalef"){
     SmallVector<Type *,3> Args;if(Name=="glScalef")Args={B.getFloatTy(),B.getFloatTy(),B.getFloatTy()};
     Allowed=Allowed&&!Target->isVarArg()&&Call->getType()->isVoidTy()&&Call->arg_size()==Args.size();
     if(Allowed)for(unsigned i=0;i<Args.size();++i)if(Call->getArgOperand(i)->getType()!=Args[i])Allowed=false;
    }
    if(Name=="glTexGeni"||Name=="glTexGenfv"||Name=="glPolygonMode"||Name=="glFogi"||Name=="glFogf"||Name=="glAlphaFunc"||Name=="glFogfv"||Name=="glLightfv"||Name=="glLightf"||Name=="glLightModelfv"||Name=="glFrontFace"||Name=="glDepthMask"||Name=="glPolygonOffset"||Name=="glIsEnabled"||Name=="glColor4fv"||Name=="glMaterialfv"||Name=="glTexEnvfv"||Name=="glTexEnvf"||Name=="glNormalPointer"||Name=="glFogCoordPointerEXT"){
     SmallVector<Type *,3> Args={I32};Type *Result=B.getVoidTy();
     if(Name=="glDepthMask")Args={B.getInt8Ty()};
     else if(Name=="glPolygonOffset")Args={B.getFloatTy(),B.getFloatTy()};
     else if(Name=="glIsEnabled")Result=B.getInt8Ty();
     else if(Name=="glColor4fv")Args={P};
     else if(Name=="glTexGeni")Args={I32,I32,I32};
     else if(Name=="glFogi"||Name=="glPolygonMode")Args={I32,I32};
     else if(Name=="glFogf"||Name=="glAlphaFunc")Args={I32,B.getFloatTy()};
     else if(Name=="glFogfv")Args={I32,P};
     else if(Name=="glTexGenfv"||Name=="glMaterialfv"||Name=="glLightfv"||Name=="glTexEnvfv"||Name=="glNormalPointer"||Name=="glFogCoordPointerEXT")Args={I32,I32,P};
     else if(Name=="glLightModelfv")Args={I32,P};
     else if(Name=="glTexEnvf"||Name=="glLightf")Args={I32,I32,B.getFloatTy()};
     Allowed=Allowed&&!Target->isVarArg()&&Call->getType()==Result&&Call->arg_size()==Args.size();
     if(Allowed)for(unsigned i=0;i<Args.size();++i)if(Call->getArgOperand(i)->getType()!=Args[i])Allowed=false;
    }
    if(Name=="glPrimitiveRestartIndex"||Name=="glRotatef"||Name=="glTexCoord2f"||Name=="glTranslatef"||Name=="glColor3ub"||Name=="glVertex3f"||Name=="glBlendFunc"||Name=="glBegin"||Name=="glVertex2f"||Name=="glVertex2i"||Name=="glColor4f"||Name=="glEnd"||Name=="glTexEnvi"||Name=="glBlendFuncSeparate"||Name=="glColor4ub"||Name=="glEnableClientState"||Name=="glDisableClientState"||Name=="glColorPointer"||Name=="glVertexPointer"||Name=="glTexCoordPointer"||Name=="glUseProgram"||Name=="glUniform2i"||Name=="glUniform4i"||Name=="glDrawElements"){
     SmallVector<Type *,5> Args={I32};
     if(Name=="glTexEnvi"||Name=="glUniform2i")Args={I32,I32,I32};
     else if(Name=="glBlendFunc")Args={I32,I32};
     else if(Name=="glColor3ub")Args={B.getInt8Ty(),B.getInt8Ty(),B.getInt8Ty()};
     else if(Name=="glVertex3f"||Name=="glTranslatef")Args={B.getFloatTy(),B.getFloatTy(),B.getFloatTy()};
     else if(Name=="glVertex2f"||Name=="glTexCoord2f")Args={B.getFloatTy(),B.getFloatTy()};
     else if(Name=="glVertex2i")Args={I32,I32};
     else if(Name=="glColor4f"||Name=="glRotatef")Args={B.getFloatTy(),B.getFloatTy(),B.getFloatTy(),B.getFloatTy()};
     else if(Name=="glEnd")Args.clear();
     else if(Name=="glBlendFuncSeparate")Args={I32,I32,I32,I32};
     else if(Name=="glUniform4i")Args={I32,I32,I32,I32,I32};
     else if(Name=="glColorPointer"||Name=="glVertexPointer"||Name=="glTexCoordPointer"||Name=="glDrawElements")Args={I32,I32,I32,P};
     else if(Name=="glColor4ub")Args={B.getInt8Ty(),B.getInt8Ty(),B.getInt8Ty(),B.getInt8Ty()};
     Allowed=Allowed&&!Target->isVarArg()&&Call->getType()->isVoidTy()&&Call->arg_size()==Args.size();
     if(Allowed)for(unsigned i=0;i<Args.size();++i)if(Call->getArgOperand(i)->getType()!=Args[i])Allowed=false;
    }
    if(Name=="glMatrixMode"||Name=="glLoadIdentity"||Name=="glLoadMatrixf"||Name=="glLoadMatrixd"||Name=="glMultMatrixf"||Name=="glMultMatrixd"||Name=="glViewport"||Name=="glDisable"||Name=="glEnable"){
     SmallVector<Type *,4> Args={I32};
     if(Name=="glLoadIdentity")Args.clear();
     else if(Name=="glLoadMatrixf"||Name=="glLoadMatrixd"||Name=="glMultMatrixf"||Name=="glMultMatrixd")Args={P};
     else if(Name=="glViewport")Args={I32,I32,I32,I32};
     Allowed=Allowed&&!Target->isVarArg()&&Call->getType()->isVoidTy()&&Call->arg_size()==Args.size();
     if(Allowed)for(unsigned i=0;i<Args.size();++i)if(Call->getArgOperand(i)->getType()!=Args[i])Allowed=false;
    }
    if(Name=="glBeginQueryARB"||Name=="glEndQueryARB"||Name=="glGetQueryObjectui64v"){
     SmallVector<Type *,4> Args={I32};
     if(Name!="glEndQueryARB")Args.push_back(I32);
     if(Name=="glGetQueryObjectui64v")Args.push_back(P);
     Allowed=Allowed&&!Target->isVarArg()&&Call->getType()->isVoidTy()&&Call->arg_size()==Args.size();
     if(Allowed)for(unsigned i=0;i<Args.size();++i)if(Call->getArgOperand(i)->getType()!=Args[i])Allowed=false;
    }
    if(Name=="glHint"||Name=="glGenQueriesARB"){
     Allowed=Allowed&&!Target->isVarArg()&&Call->getType()->isVoidTy()&&Call->arg_size()==2;
     if(Allowed)Allowed=Call->getArgOperand(0)->getType()==I32&&Call->getArgOperand(1)->getType()==(Name=="glHint"?static_cast<Type *>(I32):static_cast<Type *>(P));
    }
    if(Name=="glActiveTexture"||Name=="glClientActiveTexture"||Name=="glMultiTexCoord2f"||Name=="glMultiTexCoord3f"){
     SmallVector<Type *,4> Args={I32};
     if(Name=="glMultiTexCoord2f"||Name=="glMultiTexCoord3f"){Args.push_back(B.getFloatTy());Args.push_back(B.getFloatTy());}
     if(Name=="glMultiTexCoord3f")Args.push_back(B.getFloatTy());
     Allowed=Allowed&&!Target->isVarArg()&&Call->getType()->isVoidTy()&&Call->arg_size()==Args.size();
     if(Allowed)for(unsigned i=0;i<Args.size();++i)if(Call->getArgOperand(i)->getType()!=Args[i])Allowed=false;
    }
    if(Name=="SDL_CreateProperties"||Name=="SDL_DestroyProperties"||Name=="SDL_SetStringProperty"||Name=="SDL_SetBooleanProperty"||Name=="SDL_SetNumberProperty"||Name=="SDL_CreateWindowWithProperties"){
     SmallVector<Type *,4> Args;Type *Result=B.getInt1Ty();
     if(Name=="SDL_CreateProperties")Result=I32;
     else if(Name=="SDL_DestroyProperties"){Args={I32};Result=B.getVoidTy();}
     else if(Name=="SDL_SetStringProperty")Args={I32,P,P};
     else if(Name=="SDL_SetBooleanProperty")Args={I32,P,B.getInt1Ty()};
     else if(Name=="SDL_SetNumberProperty")Args={I32,P,I64};
     else if(Name=="SDL_CreateWindowWithProperties"){Args={I32};Result=P;}
     Allowed=Allowed&&!Target->isVarArg()&&Call->getType()==Result&&Call->arg_size()==Args.size();
     if(Allowed)for(unsigned i=0;i<Args.size();++i)if(Call->getArgOperand(i)->getType()!=Args[i])Allowed=false;
    }
    if(Name=="SDL_SetError"){
     StringRef Format;Allowed=Allowed&&Target->isVarArg()&&Target->arg_size()==1&&Call->getType()->isIntegerTy(1)&&Call->arg_size()==2;
     if(Allowed)Allowed=Call->getArgOperand(0)->getType()==P&&Call->getArgOperand(1)->getType()==P&&getConstantStringInfo(Call->getArgOperand(0),Format)&&Format=="%s";
    }
    if(Name=="SDL_CleanupTLS")Allowed=Allowed&&!Target->isVarArg()&&Call->getType()->isVoidTy()&&Call->arg_size()==0;
    if(Name=="SDL_SetLogOutputFunction"){
     Allowed=Allowed&&!Target->isVarArg()&&Call->getType()->isVoidTy()&&Call->arg_size()==2;
     if(Allowed)for(unsigned i=0;i<2;++i)if(Call->getArgOperand(i)->getType()!=P)Allowed=false;
     if(Allowed)B.CreateCall(Callback,{Call->getArgOperand(0),B.getInt32(1)});
    }
    // Finite native test/logging path: one host-owned message with literal %s.
    // No arbitrary printf format, vararg ABI, or guest string is admitted.
    if(Name=="SDL_LogMessage"){
     StringRef Format;
     Allowed=Allowed&&Target->isVarArg()&&Target->arg_size()==3&&Call->getType()->isVoidTy()&&Call->arg_size()==4;
     if(Allowed)Allowed=Target->getFunctionType()->getParamType(0)==I32&&Target->getFunctionType()->getParamType(1)==I32&&Target->getFunctionType()->getParamType(2)==P;
     if(Allowed)Allowed=Call->getArgOperand(0)->getType()==I32&&Call->getArgOperand(1)->getType()==I32&&Call->getArgOperand(2)->getType()==P&&Call->getArgOperand(3)->getType()==P&&getConstantStringInfo(Call->getArgOperand(2),Format)&&Format=="%s";
    }
    if(Name=="SDL_SetMemoryFunctions"){
     Allowed=Allowed&&!Target->isVarArg()&&Call->getType()->isIntegerTy(1)&&Call->arg_size()==4;
     if(Allowed)for(unsigned i=0;i<4;++i)if(Call->getArgOperand(i)->getType()!=P)Allowed=false;
     if(Allowed)for(unsigned i=0;i<4;++i)B.CreateCall(Callback,{Call->getArgOperand(i),B.getInt32(0)});
    }
    if(Name=="SDL_GetHint"||Name=="SDL_GetAppMetadataProperty"){
     Allowed=Allowed&&!Target->isVarArg()&&Call->getType()==P&&Call->arg_size()==1&&Call->getArgOperand(0)->getType()==P;
    }
    if(Name=="SDL_SetAppMetadata"||Name=="SDL_SetAppMetadataProperty"){
     unsigned N=Name=="SDL_SetAppMetadata"?3:2;
     Allowed=Allowed&&!Target->isVarArg()&&Call->getType()->isIntegerTy(1)&&Call->arg_size()==N;
     if(Allowed)for(unsigned i=0;i<N;++i)if(Call->getArgOperand(i)->getType()!=P)Allowed=false;
    }
    if(Name=="SDL_GetEnvironment"||Name=="SDL_Quit"||Name=="SDL_SetMainReady"){
     Allowed=Allowed&&!Target->isVarArg()&&Call->arg_size()==0&&
             (Name=="SDL_GetEnvironment"?Call->getType()==P:Call->getType()->isVoidTy());
    }
    if(Name=="SDL_SetEnvironmentVariable"||Name=="SDL_SetHintWithPriority"||Name=="SDL_ResetHint"){
     unsigned N=Name=="SDL_SetEnvironmentVariable"?4:Name=="SDL_SetHintWithPriority"?3:1;
     Allowed=Allowed&&!Target->isVarArg()&&Call->getType()->isIntegerTy(1)&&Call->arg_size()==N;
     if(Allowed)for(unsigned i=0;i<N;++i){
      Type *Expected=P;
      if(i==3)Expected=Type::getInt1Ty(C);
      else if(N==3&&i==2)Expected=I32;
      if(Call->getArgOperand(i)->getType()!=Expected)Allowed=false;
     }
    }
    if(Name=="poll")Allowed=Allowed&&!Target->isVarArg()&&Call->arg_size()==3&&Call->getType()==I32&&Call->getArgOperand(0)->getType()==P&&Call->getArgOperand(1)->getType()==I32&&Call->getArgOperand(2)->getType()==I32;
    if(Name=="curl_global_init_mem"){
     Allowed=Allowed&&!Target->isVarArg()&&Call->getType()==I32&&Call->arg_size()==6&&Call->getArgOperand(0)->getType()==I64;
     if(Allowed)for(unsigned i=1;i<6;++i)if(Call->getArgOperand(i)->getType()!=P)Allowed=false;
     if(Allowed)for(unsigned i=1;i<6;++i)B.CreateCall(Callback,{Call->getArgOperand(i),B.getInt32(0)});
    }
    if(Name=="curl_url"||Name=="curl_multi_init"||Name=="curl_easy_init"||Name=="curl_version"||Name=="curl_global_cleanup"){
     Allowed=Allowed&&!Target->isVarArg()&&Call->arg_size()==0&&
             (Name=="curl_global_cleanup"?Call->getType()->isVoidTy():Call->getType()==P);
    }
    if(Name=="curl_multi_cleanup"||Name=="curl_easy_perform"){
     Allowed=Allowed&&!Target->isVarArg()&&Call->arg_size()==1&&Call->getArgOperand(0)->getType()==P&&Call->getType()==I32;
    }
    if(Name=="curl_url_set")Allowed=Allowed&&!Target->isVarArg()&&Call->arg_size()==4&&Call->getType()==I32&&Call->getArgOperand(0)->getType()==P&&Call->getArgOperand(1)->getType()==I32&&Call->getArgOperand(2)->getType()==P&&Call->getArgOperand(3)->getType()==I32;
    if(Name=="curl_slist_append")Allowed=Allowed&&!Target->isVarArg()&&Call->arg_size()==2&&Call->getType()==P&&Call->getArgOperand(0)->getType()==P&&Call->getArgOperand(1)->getType()==P;
    if(Name=="curl_slist_free_all"||Name=="curl_url_cleanup")Allowed=Allowed&&!Target->isVarArg()&&Call->arg_size()==1&&Call->getType()->isVoidTy()&&Call->getArgOperand(0)->getType()==P;
    if(Name=="curl_easy_cleanup"||Name=="curl_easy_reset"){
     Allowed=Allowed&&!Target->isVarArg()&&Call->arg_size()==1&&Call->getArgOperand(0)->getType()==P&&Call->getType()->isVoidTy();
    }
    if(Name=="curl_multi_add_handle"||Name=="curl_multi_remove_handle"){
     Allowed=Allowed&&!Target->isVarArg()&&Call->arg_size()==2&&Call->getArgOperand(0)->getType()==P&&Call->getArgOperand(1)->getType()==P&&Call->getType()==I32;
    }
    if(Name=="curl_multi_info_read"){
     Allowed=Allowed&&!Target->isVarArg()&&Call->arg_size()==2&&Call->getType()==P&&
             Call->getArgOperand(0)->getType()==P&&Call->getArgOperand(1)->getType()==P;
    }
    if(Name=="curl_multi_setopt"){
     // Only the four reviewed socket/timer options. Require a constant selector and
     // exact native variadic ABI, never a blanket name-based exemption.
     Allowed=Allowed&&Target->isVarArg()&&Target->arg_size()==2&&
             Target->getFunctionType()->getParamType(0)==P&&Target->getFunctionType()->getParamType(1)==I32&&
             Call->getType()==I32&&Call->arg_size()==3&&Call->getArgOperand(0)->getType()==P&&
             Call->getArgOperand(1)->getType()==I32&&Call->getArgOperand(2)->getType()==P;
     auto *Option=Allowed?dyn_cast<ConstantInt>(Call->getArgOperand(1)):nullptr;
     Allowed=Allowed&&Option&&(Option->getZExtValue()==20004||Option->getZExtValue()==10005||Option->getZExtValue()==20001||Option->getZExtValue()==10002);
     if(Allowed&&(Option->getZExtValue()==20004||Option->getZExtValue()==20001))B.CreateCall(Callback,{Call->getArgOperand(2),B.getInt32(1)});
    }
    if(Name=="curl_easy_setopt"||Name=="curl_easy_getinfo"){
     Allowed=Allowed&&Target->isVarArg()&&Target->arg_size()==2&&
             Target->getFunctionType()->getParamType(0)==P&&Target->getFunctionType()->getParamType(1)==I32&&
             Call->getType()==I32&&Call->arg_size()==3&&Call->getArgOperand(0)->getType()==P&&Call->getArgOperand(1)->getType()==I32;
     auto *Option=Allowed?dyn_cast<ConstantInt>(Call->getArgOperand(1)):nullptr;
     uint64_t Value=Option?Option->getZExtValue():0;
     if(Name=="curl_easy_getinfo")Allowed=Allowed&&Option&&(Value==0x100015||Value==0x200002||Value==0x200019||Value==0x10003d)&&Call->getArgOperand(2)->getType()==P;
     else {
      bool PointerOption=Value==10010||Value==10029||Value==20079||Value==10057||Value==20219||Value==10282||Value==10023||Value==20208||Value==10209||Value==10002||Value==10177||Value==10103||Value==10065||Value==10018||Value==20011||Value==10001;
      Allowed=Allowed&&Option&&(PointerOption||Value==43||Value==52||Value==113||Value==155||Value==99||Value==13||Value==33||Value==75||Value==216||Value==30270)&&Call->getArgOperand(2)->getType()==(PointerOption?static_cast<Type *>(P):static_cast<Type *>(I64));
      if(Allowed&&(Value==20208||Value==20011||Value==20079||Value==20219))B.CreateCall(Callback,{Call->getArgOperand(2),B.getInt32(1)});
     }
    }
    if(Name=="curl_multi_assign"||Name=="curl_multi_socket_action"){
     unsigned N=Name=="curl_multi_assign"?3:4;
     Allowed=Allowed&&!Target->isVarArg()&&Call->getType()==I32&&Call->arg_size()==N&&
             Call->getArgOperand(0)->getType()==P&&Call->getArgOperand(1)->getType()==I32&&Call->getArgOperand(N-1)->getType()==P;
     if(Allowed&&N==4)Allowed=Call->getArgOperand(2)->getType()==I32;
    }
    bool Float32=Name=="f32_add"||Name=="f32_sub"||Name=="f32_mul"||Name=="f32_div";
    bool Float64=Name=="f64_add"||Name=="f64_sub"||Name=="f64_mul"||Name=="f64_div";
    if(Float32||Float64){
     // Apple ARM64 passes these one-member structs in i64 argument slots.
     // binary32 returns i32; binary64 returns i64. No pointers or callbacks.
     Allowed=Allowed&&Call->getType()->isIntegerTy(Float32?32:64)&&Call->arg_size()==2;
     for(auto &A:Call->args())if(!A->getType()->isIntegerTy(64))Allowed=false;
    }
    if(Name=="f32_sqrt"||Name=="f64_sqrt"){
     // The same scalar-struct ABI, with exactly one by-value input. Reject
     // mismatched declarations, varargs and calling conventions before entry.
     Type *Result=Name=="f32_sqrt"?I32:I64;
     Allowed=Allowed&&!Target->isVarArg()&&Target->arg_size()==1&&
             Target->getReturnType()==Result&&Target->getFunctionType()->getParamType(0)==I64&&
             Target->getCallingConv()==CallingConv::C&&Call->getCallingConv()==CallingConv::C&&
             Call->getType()==Result&&Call->arg_size()==1&&Call->getArgOperand(0)->getType()==I64;
    }
    bool ToExt=Name=="f32_to_extF80"||Name=="f64_to_extF80"||Name=="i64_to_extF80"||Name=="ui64_to_extF80";
    bool FromExt=Name=="extF80_to_f32"||Name=="extF80_to_f64";
    auto ExtValue=[](Type *T){auto *A=dyn_cast<ArrayType>(T);return A&&A->getNumElements()==2&&A->getElementType()->isIntegerTy(64);};
    if(ToExt)Allowed=Allowed&&ExtValue(Call->getType())&&Call->arg_size()==1&&Call->getArgOperand(0)->getType()->isIntegerTy(64);
    if(FromExt)Allowed=Allowed&&Call->getType()->isIntegerTy(Name=="extF80_to_f32"?32:64)&&Call->arg_size()==1&&ExtValue(Call->getArgOperand(0)->getType());
    if(Name=="fcntl"){
     Allowed=false;
     if(Call->arg_size()>=2&&Call->getArgOperand(1)->getType()->isIntegerTy(32))if(auto *Command=dyn_cast<ConstantInt>(Call->getArgOperand(1))){
      auto Found=ScalarFcntl.find(Command->getZExtValue());
      if(Found!=ScalarFcntl.end()&&Call->arg_size()==Found->second){Allowed=true;for(auto &A:Call->args())if(!A->getType()->isIntegerTy(32))Allowed=false;}
     }
    }
    if(Callbacks.contains(Name)){
     unsigned N=Name=="SDL_SetTLS"?3:Name=="pthread_create"?4:2,Index=(Name=="SDL_SetTLS"||Name=="pthread_create")?2:1;
     if((Name!="SDL_SetTLS"&&Name!="pthread_once"&&Name!="pthread_key_create"&&Name!="pthread_create")||Target->isVarArg()||(Name=="SDL_SetTLS"?!Call->getType()->isIntegerTy(1):Call->getType()!=I32)||Call->arg_size()!=N)report_fatal_error("Unsupported callback contract ABI");
     for(unsigned i=0;i<N;++i)if(Call->getArgOperand(i)->getType()!=P)report_fatal_error("Unsupported callback pointer ABI");
     B.CreateCall(Callback,{Call->getArgOperand(Index),B.getInt32((Name=="pthread_key_create"||Name=="SDL_SetTLS")?1:0)});Allowed=true;
    }
    for(auto &A:Call->args())if(!A->getType()->isPointerTy()&&containsPointer(A->getType()))Allowed=false;
    auto *Label=B.CreateGlobalStringPtr(Name);B.CreateCall(Gate,{Label,B.getInt32(Allowed?1:0)});
    if(Allowed){++HostCalls;for(auto &A:Call->args())if(A->getType()->isPointerTy())B.CreateCall(Pointer,{Label,A.get()});}else ++Blocked;
   }
  }
  uint64_t GroupedAccesses=groupAccessBrackets(M);
  SmallVector<Constant *,128> Targets;for(auto *F:Functions)Targets.push_back(F);
  auto *Array=ConstantArray::get(ArrayType::get(P,Targets.size()),Targets);auto *Table=new GlobalVariable(M,Array->getType(),true,GlobalValue::PrivateLinkage,Array,"tb_checked_targets");
  auto *Ctor=Function::Create(FunctionType::get(Void,false),GlobalValue::PrivateLinkage,"tb_register_targets",M);auto *Entry=BasicBlock::Create(C,"entry",Ctor);IRBuilder<> B(Entry);B.CreateCall(Register,{Table,B.getInt64(Targets.size())});B.CreateRetVoid();appendToGlobalCtors(M,Ctor,65535);
  M.addModuleFlag(Module::Error,"tribes.checked.externals",1);
  json::Object Summary;Summary["registered_targets"]=Targets.size();Summary["host_only_calls"]=HostCalls;Summary["blocked_calls"]=Blocked;Summary["indirect_checks"]=IndirectCalls;Summary["range_checked_calls"]=RangeCalls;Summary["memory_manager_calls"]=ManagerCalls;Summary["barrier_asm_calls"]=BarrierCalls;
  Summary["grouped_accesses"]=GroupedAccesses;
  errs()<<"TRIBES_EXTERNAL_PASS "<<formatv("{0}",json::Value(std::move(Summary)))<<"\n";return PreservedAnalyses::none();
 }
 static bool isRequired(){return true;}
};
}
extern "C" LLVM_ATTRIBUTE_WEAK PassPluginLibraryInfo llvmGetPassPluginInfo(){return {LLVM_PLUGIN_API_VERSION,"TribesExternalMemory","1",[](PassBuilder &PB){PB.registerPipelineParsingCallback([](StringRef N,ModulePassManager &PM,ArrayRef<PassBuilder::PipelineElement>){if(N!="tribes-external-memory")return false;PM.addPass(ExternalMemoryPass());return true;});}};}
