// Build-time checks for precompiled guest accesses, not a JIT or interpreter.
// Run after optimization, then code-generate without another optimization pass.
#include "llvm/IR/IRBuilder.h"
#include "llvm/ADT/SmallPtrSet.h"
#include "llvm/Analysis/ValueTracking.h"
#include "llvm/Analysis/LazyValueInfo.h"
#include "llvm/IR/ConstantRange.h"
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
 bool CPUHost;
 explicit GuestMemoryPass(bool CPUHost=false):CPUHost(CPUHost){}
 PreservedAnalyses run(Module &M,ModuleAnalysisManager &AM) {
  if(M.getModuleFlag("tribes.checked.memory")||M.getModuleFlag("tribes.checked.cpu-host"))report_fatal_error("Tribes memory pass applied twice");
  for(auto &F:M)if(!F.isDeclaration()&&F.getName().starts_with("tribes_memory_"))report_fatal_error("Do not instrument the trusted memory-check runtime");
  auto &C=M.getContext();auto *I32=Type::getInt32Ty(C);auto *I64=Type::getInt64Ty(C);auto *P=PointerType::getUnqual(C);
  auto Begin=M.getOrInsertFunction("tribes_memory_access_begin",I32,P,I64,I32);
  auto Transfer=M.getOrInsertFunction("tribes_memory_transfer_begin",I32,P,P,I64);
  auto End=M.getOrInsertFunction("tribes_memory_access_end",Type::getVoidTy(C),I32);
  uint64_t Loads=0,Stores=0,Atomics=0,Transfers=0,Sets=0,HostAccesses=0,HostSpans=0,LocalAccesses=0;std::map<std::string,uint64_t> Calls;
  // Only the original translator's fixed register prefix is admitted. The
  // runtime checks this whole host span at block entry; pointed-to guest data
  // and accesses beyond the prefix continue through ordinary permission checks.
  auto *CPU=StructType::getTypeByName(C,"_cpu");
  bool CPUShape=CPUHost&&CPU&&!CPU->isOpaque()&&!CPU->isPacked()&&CPU->getNumElements()==14&&M.getDataLayout().isLittleEndian()&&M.getDataLayout().getPointerSizeInBits()==64;
  if(CPUShape){
   for(unsigned n=0;n<12;++n)CPUShape&=CPU->getElementType(n)->isIntegerTy(32);
   auto *FP=dyn_cast<ArrayType>(CPU->getElementType(12));
   CPUShape&=FP&&FP->getNumElements()==8&&FP->getElementType()->isDoubleTy()&&CPU->getElementType(13)->isIntegerTy(64);
   if(CPUShape)CPUShape=M.getDataLayout().getTypeAllocSize(CPU)==120;
  }
  auto bytes=[&](Type *T)->uint64_t {auto N=M.getDataLayout().getTypeStoreSize(T);if(N.isScalable())report_fatal_error("Scalable memory access requires lane-aware checks");return N.getFixedValue();};
  SmallPtrSet<Function *,32> NativeCPUs;
  if(CPUHost)if(auto *Annotations=M.getNamedGlobal("llvm.global.annotations"))if(auto *Rows=dyn_cast_or_null<ConstantArray>(Annotations->getInitializer()))for(auto &Row:Rows->operands()){
   auto *Entry=dyn_cast<ConstantStruct>(Row);if(!Entry||Entry->getNumOperands()<2)continue;
   auto *Label=dyn_cast<GlobalVariable>(Entry->getOperand(1)->stripPointerCasts());
   auto *Text=Label&&Label->hasInitializer()?dyn_cast<ConstantDataArray>(Label->getInitializer()):nullptr;
   if(!Text||!Text->isCString()||Text->getAsCString()!="tribes.native-cpu")continue;
   auto *F=dyn_cast<Function>(Entry->getOperand(0)->stripPointerCasts());
   if(!M.getDataLayout().isLittleEndian()||M.getDataLayout().getPointerSizeInBits()!=64||!F||F->isDeclaration()||F->isVarArg()||F->arg_empty()||F->getArg(0)->getType()!=P)report_fatal_error("Invalid native CPU contract");
   NativeCPUs.insert(F);
  }
  auto &FAM=AM.getResult<FunctionAnalysisManagerModuleProxy>(M).getManager();
  for(auto &F:M){
   if(F.isDeclaration())continue;
   SmallVector<Instruction *,128> Work;for(auto &I:instructions(F))Work.push_back(&I);
   SmallPtrSet<Instruction *,32> Host,Local;
   auto Name=F.getName();bool Block=Name.consume_front("loc_")&&!Name.empty();
   for(char Ch:Name)Block&=(Ch>='0'&&Ch<='9')||(Ch>='a'&&Ch<='f')||(Ch>='A'&&Ch<='F');
   bool Eligible=CPUShape&&Block&&F.getCallingConv()==CallingConv::Fast&&F.hasHiddenVisibility()&&!F.isVarArg()&&F.getReturnType()->isVoidTy()&&F.arg_size()==1&&F.getArg(0)->getType()==P;
   auto *Ranges=NativeCPUs.contains(&F)?&FAM.getResult<LazyValueAnalysis>(F):nullptr;
   // The annotation requests a runtime proof, never an unchecked exemption.
   // Admit only nonnegative, bounded GEP chains rooted in that exact argument.
   // A loaded pointer, integer-derived address or unbounded index is not a CPU
   // register. Bounds are queried at the access, including dominating checks.
   auto NativeAccess=[&](Value *Address,uint64_t Size,Instruction *At){
    if(!Ranges||Size>440)return false;
    uint64_t Maximum=0;
    while(auto *G=dyn_cast<GEPOperator>(Address)){
     if(G->getPointerAddressSpace())return false;
     SmallMapVector<Value *,APInt,4> Terms;APInt Constant(64,0);
     if(!G->collectOffset(M.getDataLayout(),64,Terms,Constant)||Constant.isNegative()||Constant.ugt(440))return false;
     Maximum+=Constant.getZExtValue();if(Maximum>440-Size)return false;
     for(auto &[Index,Scale]:Terms){
      if(Index->getType()->getIntegerBitWidth()>64||Scale.isNegative()||Scale.ugt(440))return false;
      auto Range=Ranges->getConstantRange(Index,At,false);
      if(Range.getSignedMin().isNegative()||Range.getUnsignedMax().ugt(440))return false;
      Maximum+=Range.getUnsignedMax().getZExtValue()*Scale.getZExtValue();
      if(Maximum>440-Size)return false;
     }
     Address=G->getPointerOperand();
    }
    return Address==F.getArg(0);
   };
   for(auto *I:Work){
    if(I->getMetadata("tribes.checked.host.cpu"))report_fatal_error("Unverified CPU host metadata");
    if(I->getMetadata("tribes.checked.host.local"))report_fatal_error("Unverified local host metadata");
    if(!CPUHost)continue;
    Value *Address=nullptr;Type *T=nullptr;
    if(auto *L=dyn_cast<LoadInst>(I)){if(L->isSimple()){Address=L->getPointerOperand();T=L->getType();}}
    if(auto *S=dyn_cast<StoreInst>(I)){if(S->isSimple()){Address=S->getPointerOperand();T=S->getValueOperand()->getType();}}
    if(!Address||M.getDataLayout().getTypeStoreSize(T).isScalable())continue;
    int64_t Offset=0;auto *Root=GetPointerBaseWithConstantOffset(Address,Offset,M.getDataLayout());
    if(Eligible&&Root==F.getArg(0)&&Offset>=0&&Offset<120&&bytes(T)<=120-(uint64_t)Offset)Host.insert(I);
    if(NativeAccess(Address,bytes(T),I))Host.insert(I);
    // Native stack objects and actual LLVM TLS objects cannot alias the guest
    // reservation. Admit only a constant, wholly contained scalar access.
    // A pointer loaded from such an object carries no proof for its pointee.
    // Local globals are similarly owned by this module; ordinary imported or
    // interposable globals, dynamic offsets and transfers remain checked.
    uint64_t Extent=0;
    if(auto *TLS=dyn_cast<IntrinsicInst>(Root))if(TLS->getIntrinsicID()==Intrinsic::threadlocal_address)Root=TLS->getArgOperand(0);
    if(auto *A=dyn_cast<AllocaInst>(Root)){
     if(A->isStaticAlloca()&&A->getAddressSpace()==0){auto Size=A->getAllocationSize(M.getDataLayout());if(Size&&!Size->isScalable())Extent=Size->getFixedValue();}
    }else if(auto *G=dyn_cast<GlobalVariable>(Root)){
     if(G->getAddressSpace()==0&&!G->isExternallyInitialized()&&(G->isThreadLocal()||(G->hasLocalLinkage()&&G->hasInitializer()))&&G->getValueType()->isSized()){
      auto Size=M.getDataLayout().getTypeAllocSize(G->getValueType());if(!Size.isScalable())Extent=Size.getFixedValue();
     }
    }
    if(Extent&&Offset>=0&&(uint64_t)Offset<Extent&&bytes(T)<=Extent-(uint64_t)Offset)Local.insert(I);
   }
   if(!Host.empty()){
    IRBuilder<> B(&*F.getEntryBlock().getFirstInsertionPt());
    auto Check=M.getOrInsertFunction("tribes_memory_access_host_span",B.getVoidTy(),P,I64);
    B.CreateCall(Check,{F.getArg(0),B.getInt64(Ranges?440:120)});++HostSpans;
   }
   // Checks read shared permission metadata and may wait or fault. Do not retain
   // a source function/call's pure-memory, nosync or speculative attributes.
   for(auto A:{Attribute::Memory,Attribute::NoSync,Attribute::Speculatable,Attribute::WillReturn})F.removeFnAttr(A);
   for(auto *I:Work){
    if(Host.contains(I)){I->setMetadata("tribes.checked.host.cpu",MDNode::get(C,{}));++HostAccesses;continue;}
    if(Local.contains(I)){I->setMetadata("tribes.checked.host.local",MDNode::get(C,{}));++LocalAccesses;continue;}
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
  if(CPUHost)M.addModuleFlag(Module::Error,"tribes.checked.cpu-host",1);
  json::Object O;O["loads"]=Loads;O["stores"]=Stores;O["atomics"]=Atomics;O["transfers"]=Transfers;O["sets"]=Sets;O["host_cpu_accesses"]=HostAccesses;O["host_cpu_spans"]=HostSpans;O["host_local_accesses"]=LocalAccesses;json::Object External;
  for(auto &[Name,Count]:Calls)External[Name]=Count;O["calls_requiring_link_audit"]=std::move(External);
  errs()<<"TRIBES_MEMORY_PASS "<<formatv("{0}",json::Value(std::move(O)))<<"\n";
  return PreservedAnalyses::none();
 }
 static bool isRequired(){return true;}
};
}
extern "C" LLVM_ATTRIBUTE_WEAK PassPluginLibraryInfo llvmGetPassPluginInfo(){return {LLVM_PLUGIN_API_VERSION,"TribesGuestMemory","1",[](PassBuilder &PB){PB.registerPipelineParsingCallback([](StringRef Name,ModulePassManager &MPM,ArrayRef<PassBuilder::PipelineElement>){if(Name!="tribes-guest-memory"&&Name!="tribes-guest-memory-cpu-host")return false;MPM.addPass(GuestMemoryPass(Name=="tribes-guest-memory-cpu-host"));return true;});}};}
