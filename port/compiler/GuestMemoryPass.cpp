// Build-time checks for precompiled guest accesses, not a JIT or interpreter.
// Run after optimization, then code-generate without another optimization pass.
#include "llvm/IR/IRBuilder.h"
#include "llvm/IR/InstIterator.h"
#include "llvm/IR/IntrinsicInst.h"
#include "llvm/IR/Module.h"
#include "llvm/Passes/PassBuilder.h"
#include "llvm/Passes/PassPlugin.h"
#include "llvm/Support/ErrorHandling.h"
#include "llvm/Support/JSON.h"
#include "llvm/Support/raw_ostream.h"
#include <map>
using namespace llvm;
namespace {
struct GuestMemoryPass : PassInfoMixin<GuestMemoryPass> {
 PreservedAnalyses run(Module &M,ModuleAnalysisManager &) {
  if(M.getModuleFlag("tribes.checked.memory"))report_fatal_error("Tribes memory pass applied twice");
  for(auto &F:M)if(!F.isDeclaration()&&F.getName().starts_with("tribes_memory_"))report_fatal_error("Do not instrument the trusted memory-check runtime");
  auto &C=M.getContext();auto *I32=Type::getInt32Ty(C);auto *I64=Type::getInt64Ty(C);auto *P=PointerType::getUnqual(C);
  auto Begin=M.getOrInsertFunction("tribes_memory_access_begin",I32,P,I64,I32);
  auto Transfer=M.getOrInsertFunction("tribes_memory_transfer_begin",I32,P,P,I64);
  auto End=M.getOrInsertFunction("tribes_memory_access_end",Type::getVoidTy(C),I32);
  uint64_t Loads=0,Stores=0,Atomics=0,Transfers=0,Sets=0;std::map<std::string,uint64_t> Calls;
  auto bytes=[&](Type *T)->uint64_t {auto N=M.getDataLayout().getTypeStoreSize(T);if(N.isScalable())report_fatal_error("Scalable memory access requires lane-aware checks");return N.getFixedValue();};
  for(auto &F:M){
   if(F.isDeclaration())continue;
   SmallVector<Instruction *,128> Work;for(auto &I:instructions(F))Work.push_back(&I);
   // Checks read shared permission metadata and may wait or fault. Do not retain
   // a source function/call's pure-memory, nosync or speculative attributes.
   for(auto A:{Attribute::Memory,Attribute::NoSync,Attribute::Speculatable,Attribute::WillReturn})F.removeFnAttr(A);
   for(auto *I:Work){
    IRBuilder<> B(I);Value *Address=nullptr,*Length=nullptr,*Source=nullptr;unsigned Mode=0;
    if(auto *L=dyn_cast<LoadInst>(I)){Address=L->getPointerOperand();Length=B.getInt64(bytes(L->getType()));Mode=1;++Loads;}
    else if(auto *S=dyn_cast<StoreInst>(I)){Address=S->getPointerOperand();Length=B.getInt64(bytes(S->getValueOperand()->getType()));Mode=2;++Stores;}
    else if(auto *A=dyn_cast<AtomicRMWInst>(I)){Address=A->getPointerOperand();Length=B.getInt64(bytes(A->getValOperand()->getType()));Mode=3;++Atomics;}
    else if(auto *A=dyn_cast<AtomicCmpXchgInst>(I)){Address=A->getPointerOperand();Length=B.getInt64(bytes(A->getCompareOperand()->getType()));Mode=3;++Atomics;}
    else if(auto *T=dyn_cast<MemTransferInst>(I)){Address=T->getDest();Source=T->getSource();Length=T->getLength();++Transfers;}
    else if(auto *S=dyn_cast<MemSetInst>(I)){Address=S->getDest();Length=S->getLength();Mode=2;++Sets;}
    else if(auto *Call=dyn_cast<CallBase>(I)){
     Function *Target=Call->getCalledFunction();
     if(Target&&Target->isIntrinsic()){
      auto Name=Target->getName();
      if(Name=="llvm.objc.autoreleasePoolPush"||Name=="llvm.objc.autoreleasePoolPop"||Name=="llvm.objc.release"||Name=="llvm.objc.retainAutoreleasedReturnValue"||Name=="llvm.objc.clang.arc.noop.use"){
       // These lower to platform object-runtime calls, not a load/store with a
       // known guest extent. Keep them explicit in the external coverage audit.
       ++Calls[Name.str()];
      }else if(Name=="llvm.aarch64.hint"){
       auto *Hint=Call->arg_size()==1?dyn_cast<ConstantInt>(Call->getArgOperand(0)):nullptr;
       if(!Call->getType()->isVoidTy()||!Hint||!Hint->getType()->isIntegerTy(32)||Hint->getZExtValue()!=1)
        report_fatal_error("Only state-preserving AArch64 YIELD is covered");
      }else if(Call->mayReadOrWriteMemory()&&!isa<LifetimeIntrinsic>(Call)&&!isa<DbgInfoIntrinsic>(Call)&&Target->getIntrinsicID()!=Intrinsic::assume)report_fatal_error(Twine("Uncovered memory intrinsic: ")+Name);
     }else{
      for(auto A:{Attribute::Memory,Attribute::NoSync,Attribute::Speculatable,Attribute::WillReturn})Call->removeFnAttr(A);
      StringRef Name=Target?Target->getName():Call->isInlineAsm()?StringRef("<inline-asm>"):StringRef("<indirect>");if(!Target||Target->isDeclaration())++Calls[Name.str()];
      StringRef Normal=Name;
      if(Normal.starts_with("\1")){Normal=Normal.drop_front();if(Normal.starts_with("_"))Normal=Normal.drop_front();}
      if(Normal=="memcpy"||Normal=="memmove"||Normal=="__memcpy_chk"||Normal=="__memmove_chk"){
       unsigned N=Normal.starts_with("__")?4:3;
       if(!isa<CallInst>(Call)||Call->arg_size()!=N)report_fatal_error("Unsupported transfer ABI");
       Address=Call->getArgOperand(0);Source=Call->getArgOperand(1);Length=Call->getArgOperand(2);++Transfers;
      }else if(Normal=="memset"||Normal=="bzero"||Normal=="__memset_chk"){
       unsigned N=Normal=="bzero"?2:Normal=="__memset_chk"?4:3;
       if(!isa<CallInst>(Call)||Call->arg_size()!=N)report_fatal_error("Unsupported memset ABI");
       Address=Call->getArgOperand(0);Length=Call->getArgOperand(Normal=="bzero"?1:2);Mode=2;++Sets;
      }
     }
    }
    if(!Address){if(I->mayReadOrWriteMemory()&&!isa<CallBase>(I)&&!isa<FenceInst>(I))report_fatal_error("Uncovered memory instruction");continue;}
    if(!Length->getType()->isIntegerTy()||Length->getType()->getIntegerBitWidth()>64)report_fatal_error("Unsupported memory length");
    if(!Address->getType()->isPointerTy()||Address->getType()->getPointerAddressSpace()||(Source&&(!Source->getType()->isPointerTy()||Source->getType()->getPointerAddressSpace())))report_fatal_error("Nondefault memory address space");
    if(auto *Call=dyn_cast<CallInst>(I)){if(Call->isMustTailCall())report_fatal_error("Cannot bracket a musttail memory library call");Call->setTailCallKind(CallInst::TCK_None);}
    Length=B.CreateZExtOrTrunc(Length,I64);
    Value *Token=Source?B.CreateCall(Transfer,{Address,Source,Length}):B.CreateCall(Begin,{Address,Length,B.getInt32(Mode)});
    B.SetInsertPoint(I->getNextNode());B.CreateCall(End,{Token});
   }
  }
  M.addModuleFlag(Module::Error,"tribes.checked.memory",1);
  json::Object O;O["loads"]=Loads;O["stores"]=Stores;O["atomics"]=Atomics;O["transfers"]=Transfers;O["sets"]=Sets;json::Object External;
  for(auto &[Name,Count]:Calls)External[Name]=Count;O["calls_requiring_link_audit"]=std::move(External);
  errs()<<"TRIBES_MEMORY_PASS "<<formatv("{0}",json::Value(std::move(O)))<<"\n";
  return PreservedAnalyses::none();
 }
 static bool isRequired(){return true;}
};
}
extern "C" LLVM_ATTRIBUTE_WEAK PassPluginLibraryInfo llvmGetPassPluginInfo(){return {LLVM_PLUGIN_API_VERSION,"TribesGuestMemory","1",[](PassBuilder &PB){PB.registerPipelineParsingCallback([](StringRef Name,ModulePassManager &MPM,ArrayRef<PassBuilder::PipelineElement>){if(Name!="tribes-guest-memory")return false;MPM.addPass(GuestMemoryPass());return true;});}};}
