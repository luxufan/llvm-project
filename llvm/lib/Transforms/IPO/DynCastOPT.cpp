#include "llvm/Transforms/IPO/DynCastOPT.h"
#include "llvm/ADT/SetVector.h"
#include "llvm/ADT/Statistic.h"
#include "llvm/IR/Constants.h"
#include "llvm/IR/IRBuilder.h"
#include "llvm/IR/Instructions.h"

#define DEBUG_TYPE "dyncastopt"

STATISTIC(NumOptDynCast, "Number of optimized dynamic_cast call site");
STATISTIC(NumDynCast, "Number of dynamic_cast call site");
STATISTIC(NumOptDynCastOffsetToTopMustZero,
          "Number of optimized dynamic_cast call site that has must zero "
          "offset to top value");

using namespace llvm;

static cl::opt<unsigned> MaxSuperChecks(
    "max-super-checks", cl::init(10), cl::Hidden, cl::value_desc("N"),
    cl::desc("Only check supers with less or equal than N supers"));
const int64_t VirtualMask = 1;
const int64_t PublicMaks = 2;
const int64_t ShiftToOffset = 8;

void DynCastOPTPass::invalidateExternalClass() {
  SmallVector<const Value *> WorkList;
  for_each(ExternalLinkageRTTIs, [&WorkList](const Value *V) { WorkList.push_back(V); });

  while (!WorkList.empty()) {
    const Value *Current = WorkList.pop_back_val();
    if (CHA.contains(Current)) {
      for (auto &Base : CHA[Current])
        WorkList.push_back(Base.first);
    }
    Invalids.insert(Current);
  }
}

void DynCastOPTPass::recordExternalClass(const GlobalVariable *RTTI) {
  assert(!RTTI->isInternalLinkage(RTTI->getLinkage()) &&
         "This is a internal class");
  ExternalLinkageRTTIs.insert(RTTI);
}

void DynCastOPTPass::buildTypeInfoGraph(Module &M) {
  for (GlobalVariable &GV : M.globals()) {
    if (GV.hasName() && GV.getName().starts_with("_ZTI") &&
        GV.hasInitializer()) {
      if (ConstantStruct *Initializer =
              dyn_cast<ConstantStruct>(GV.getInitializer())) {
        // __class_type_info has type { ptr, ptr }
        // __si_class_type_info has type { ptr, ptr, ptr }
        // __vmi_class_type_info has type { ptr, ptr, i32, i32, ptr, i64 ... }
        if (Initializer->getNumOperands() == 2) {
          CHA.getOrInsertDefault(cast<Value>(&GV));
        } else if (Initializer->getNumOperands() == 3) {
          Value *Base = Initializer->getOperand(2);
          CHA[&GV].push_back(std::make_pair(Base, 0));
          SuperClasses[Base].push_back(&GV);
        } else if (Initializer->getNumOperands() > 4) {
          unsigned NumBase =
              cast<ConstantInt>(Initializer->getOperand(3))->getZExtValue();
          for (unsigned I = 0; I < NumBase; I++) {
            int64_t OffsetFlag =
                cast<ConstantInt>(Initializer->getOperand(I * 2 + 5))
                    ->getSExtValue();
            Value *Base = Initializer->getOperand(I * 2 + 4);
            bool IsVirtual = OffsetFlag & (VirtualMask);
            assert(!IsVirtual && "Virtual inheritance");
            int64_t Offset = OffsetFlag >> ShiftToOffset;
            bool IsPublic = OffsetFlag & (PublicMaks);
            if (!IsPublic)
              continue;
            CHA[cast<Value>(&GV)].push_back(std::make_pair(Base, Offset));
            SuperClasses[Base].push_back(&GV);
          }
        }
      } else {
        assert(false && "Initializer is not a constant struct");
      }
      if (!GV.isInternalLinkage(GV.getLinkage()))
        recordExternalClass(&GV);
    }
  }
  invalidateExternalClass();
}

void DynCastOPTPass::getMostDerivedClasses(
    const Value *Base, SetVector<const Value *> &MostDerivedClasses) {
  SetVector<const Value *> Supers;
  getSuperClasses(Base, Supers);
  for (auto *Super : Supers) {
    const SmallVector<const Value *> SuperSupers = SuperClasses[Super];
    if (SuperSupers.empty())
      MostDerivedClasses.insert(Super);
  }
}

void DynCastOPTPass::getSuperClasses(const Value *Class,
                                     SetVector<const Value *> &Supers) {
  SmallVector<const Value *> WorkList;
  Supers.insert(Class);
  WorkList.push_back(Class);
  while (!WorkList.empty()) {
    const Value *V = WorkList.pop_back_val();
    for (auto *Super : SuperClasses[V]) {
      WorkList.push_back(Super);
      Supers.insert(Super);
    }
  }
}

bool DynCastOPTPass::isUniqueBaseInFullCHA(const Value *C) {
  SetVector<const Value *> MostDerivedClasses;
  getMostDerivedClasses(C, MostDerivedClasses);

  for (const Value *Derived : MostDerivedClasses) {
    if (!isUniqueBaseForSuper(C, Derived))
      return false;
  }

  return true;
}

bool DynCastOPTPass::isUniqueBaseForSuper(const Value *Base,
                                          const Value *Super) {
  if (Base == Super)
    return true;

  unsigned ReachCount = 0;
  SmallVector<const Value *> WorkList;
  WorkList.push_back(Super);
  while (!WorkList.empty()) {
    const Value *Current = WorkList.pop_back_val();
    for (const auto &BasePair : CHA[Current]) {
      const Value *BaseP = BasePair.first;
      if (BaseP == Base)
        ReachCount++;
      else
        WorkList.push_back(BaseP);
    }
  }
  assert(ReachCount != 0 && "Base is not a base of Super");
  return ReachCount < 2;
}

bool DynCastOPTPass::hasPrevailingVTables(const Value *RTTI) {
  return VTables.contains(RTTI);
}

Value *DynCastOPTPass::loadRuntimePtr(Value *StaticPtr, IRBuilder<> &IRB,
                                      unsigned AddressSpace) {
  LLVMContext &Context = IRB.getContext();
  Type *PTy = PointerType::get(IRB.getContext(), AddressSpace);
  // offset-to-top is always placed in vptr - 16. FIXME: -16 is incorrect for 32
  // bit address space.
  Value *VPtr = IRB.CreateLoad(PTy, StaticPtr, "vptr");
  Type *ByteType = Type::getInt8Ty(Context);
  Value *Idx = ConstantInt::get(Type::getInt64Ty(Context), -16);
  Value *AddrOffsetToTop =
      IRB.CreateInBoundsGEP(ByteType, VPtr, Idx, "add_offset_to_top");

  Value *OffsetToTop = IRB.CreateLoad(Type::getInt64Ty(Context),
                                      AddrOffsetToTop, "offset_to_top");
  return IRB.CreateInBoundsGEP(ByteType, StaticPtr, OffsetToTop,
                               "runtime_object");
}

bool DynCastOPTPass::handleDynCastCallSite(CallInst *CI) {
  NumDynCast++;
  Value *StaticPtr = CI->getArgOperand(0);
  ConstantInt *Src2DstHint = cast<ConstantInt>(CI->getArgOperand(3));

  if (isa<ConstantPointerNull>(StaticPtr)) {
    CI->replaceAllUsesWith(StaticPtr);
    return true;
  }
  if (!CI->getFunction()->isInternalLinkage(CI->getFunction()->getLinkage()))
    return false;

  LLVMContext &Context = CI->getContext();
  Function *Called = CI->getCalledFunction();
  assert(Called->hasName() && Called->getName() == "__dynamic_cast");
  Value *DestType = CI->getArgOperand(2);

  Type *PTy =
      PointerType::get(CI->getContext(), CI->getFunction()->getAddressSpace());

  if (invalidToOptimize(DestType) || !isUniqueBaseInFullCHA(DestType))
    return false;

  SetVector<const Value *> Supers;
  getSuperClasses(DestType, Supers);
  bool IsOffsetToTopMustZero = Src2DstHint->getSExtValue() == 0 && isOffsetToTopMustZero(Supers);

  // If not all super classes have the prevailing vritual table definition,
  // then the optimization can not be performed.
  // TODO: don't eliminate virtual table global variable in compilation if
  // -flto is enabled.
  Supers.remove_if(
      [this](const Value *RTTI) { return !this->hasPrevailingVTables(RTTI); });

  // FIXME: Reduce super classes by judge if there is prevailing virtual tables
  // is wrong. Although no prevailing virtual tables means there is no
  // allocation site for corresponding classes, allication sites may still exist
  // in other linkage unit.
  // TODO: If the virtual table will not be elimiated, then we can reduce the
  // superclasses by if the virtual table is used by other regular object file.
  // If it is not used and also not used in current LTO unit, then we can remove
  // the check for this class type.

  if (Supers.size() > MaxSuperChecks)
    return false;

  if (Supers.empty()) {
    CI->replaceAllUsesWith(ConstantInt::getNullValue(PTy));
    return true;
  }

  BasicBlock *LoadBlock =
      CI->getParent()->splitBasicBlock(CI, "load_block", /* Before */ true);

  Instruction *LBTerm = LoadBlock->getTerminator();
  assert(isa<BranchInst>(LBTerm));
  BranchInst *BrOfLB = dyn_cast<BranchInst>(LBTerm);
  assert(BrOfLB->isUnconditional());
  IRBuilder<> IRBLoadB(LoadBlock);
  IRBLoadB.SetInsertPoint(BrOfLB);

  Value *RuntimePtr;
  if (!IsOffsetToTopMustZero) {
    // offset-to-top is always placed in vptr - 16. FIXME: -16 is incorrect for
    // 32 bit address space.
    RuntimePtr = loadRuntimePtr(StaticPtr, IRBLoadB,
                                CI->getFunction()->getAddressSpace());
  } else {
    NumOptDynCastOffsetToTopMustZero++;
    RuntimePtr = StaticPtr;
  }

  Value *RuntimeVPtr = IRBLoadB.CreateLoad(PTy, RuntimePtr, "runtime_vptr");
  SmallVector<BasicBlock *> BBs;
  for (unsigned I = 0; I < Supers.size(); I++) {
    BBs.push_back(BasicBlock::Create(CI->getContext(),
                                     "check_super." + Twine(I),
                                     CI->getFunction(), CI->getParent()));
  }

  // Check fails
  BBs.push_back(CI->getParent());

  // Offset handle block
  BBs.push_back(BasicBlock::Create(CI->getContext(), "handle_offset",
                                   CI->getFunction(), CI->getParent()));

  PHINode *Phi =
      PHINode::Create(Type::getInt64Ty(Context), Supers.size(), "", BBs.back());

  BrOfLB->setSuccessor(0, BBs[0]);

  SmallVector<const Value *> SupersVector;
  for (const Value *Super : Supers) {
    SupersVector.push_back(Super);
  }
  for (unsigned I = 0; I < SupersVector.size(); I++) {
    assert(VTables.contains(SupersVector[I]));
    Value *Result =
        CmpInst::Create(Instruction::ICmp, ICmpInst::Predicate::ICMP_EQ,
                        RuntimeVPtr, VTables[SupersVector[I]], "", BBs[I]);
    BranchInst::Create(BBs.back(), BBs[I + 1], Result, BBs[I]);
    Phi->addIncoming(ConstantInt::get(Type::getInt64Ty(Context),
                                      computeOffset(DestType, SupersVector[I])),
                     BBs[I]);
  }

  // Add the base offset
  Value *DestPtr = GetElementPtrInst::Create(Type::getInt8Ty(Context),
                                             RuntimePtr, Phi, "", BBs.back());
  BranchInst::Create(CI->getParent(), BBs.back());
  PHINode *ResultPhi = PHINode::Create(PTy, 2, "", &*CI->getParent()->begin());
  ResultPhi->addIncoming(Constant::getNullValue(PTy), BBs[BBs.size() - 3]);
  ResultPhi->addIncoming(DestPtr, BBs.back());
  CI->replaceAllUsesWith(ResultPhi);
  return true;
}

int64_t DynCastOPTPass::computeOffset(const Value *Base, const Value *Super) {
  assert(isUniqueBaseForSuper(Base, Super));
  if (Base == Super)
    return 0;
  using BaseOffsetPair = std::pair<const Value *, int64_t>;
  SmallVector<BaseOffsetPair> WorkList;
  WorkList.push_back(std::make_pair(Super, 0));
  while (!WorkList.empty()) {
    BaseOffsetPair Pair = WorkList.pop_back_val();
    for (auto &B : CHA[Pair.first]) {
      if (B.first != Base) {
        WorkList.push_back(std::make_pair(B.first, B.second + Pair.second));
      } else
        return B.second + Pair.second;
    }
  }
  llvm_unreachable("Base is not a base of super");
}

static Value *getIndexesToAddressPoint(const DynCastOPTPass::CHAMapType &CHA,
                                       Constant *VTableInit,
                                       SmallVectorImpl<Constant *> &Idxs) {
  // With -fwhole-program-vtables, globalsplit pass may split the struct type
  // to array type. For example:
  //@_ZTVZN6recfun3def12contains_defERNS_4utilEP4exprE10def_find_p.0
  //  = internal constant { [5 x ptr] } { [ptr null, ...] }, !type !795, !type
  //  !799
  //
  // optimized to:
  //
  //@_ZTVZN6recfun3def12contains_defERNS_4utilEP4exprE10def_find_p.0
  //  = internal constant [5 x ptr] [ptr null, ...], !type !795, !type !799
  //
  if (ConstantStruct *VPtrStruct = dyn_cast<ConstantStruct>(VTableInit)) {
    Idxs.push_back(
        ConstantInt::get(Type::getInt32Ty(VTableInit->getContext()), 0));
    if (VPtrStruct->getNumOperands() == 0)
      return nullptr;
    VTableInit = VPtrStruct->getOperand(0);
  }

  if (ConstantArray *VPtrArray = dyn_cast<ConstantArray>(VTableInit)) {
    if (!VPtrArray->getType()->getElementType()->isPointerTy()) {
      return nullptr;
    }

    uint64_t Offset = 0;
    for (Value *Entry : VPtrArray->operand_values()) {
      Offset += 1;
      if (CHA.contains(Entry)) {
        Idxs.push_back(ConstantInt::get(
            Type::getInt64Ty(VTableInit->getContext()), Offset));
        return Entry;
      }
    }
  }
  return nullptr;
}

void DynCastOPTPass::collectVirtualTables(Module &M) {
  for (GlobalVariable &GV : M.globals()) {
    if (GV.hasName() && GV.getName().starts_with("_ZTV") &&
        GV.hasInitializer()) {
      SmallVector<Constant *> Idxs;
      Idxs.push_back(ConstantInt::get(Type::getInt32Ty(M.getContext()), 0));
      if (Value *RTTI =
              getIndexesToAddressPoint(CHA, GV.getInitializer(), Idxs)) {
        Constant *AddressPointer = ConstantExpr::getGetElementPtr(
            GV.getInitializer()->getType(), &GV, Idxs);
        VTables.insert(std::make_pair(RTTI, AddressPointer));
      }
    }
  }
}

bool DynCastOPTPass::isOffsetToTopMustZero(
    SetVector<const Value *> &SuperClasses) {
  // TODO: for non-linear, if the desitination type is the primary base class of
  // its super class, then the offset-to-top value is must 0.
  for (auto *Super : SuperClasses) {
    if (CHA[Super].size() > 1)
      return false;
    assert(CHA[Super].size() == 1);
  }
  return true;
}

PreservedAnalyses DynCastOPTPass::run(Module &M, ModuleAnalysisManager &) {
  buildTypeInfoGraph(M);
  collectVirtualTables(M);
  SmallVector<CallInst *> Deleted;
  for (Function &F : M.functions()) {
    for (BasicBlock &BB : F) {
      for (Instruction &I : BB) {
        if (CallInst *CI = dyn_cast<CallInst>(&I)) {
          Function *Called = CI->getCalledFunction();
          if (Called && Called->hasName() && Called->getName() == "__dynamic_cast") {
            if (handleDynCastCallSite(CI))
              Deleted.push_back(CI);
          }
        }
      }
    }
  }
  for (auto *CI : Deleted) {
    CI->eraseFromParent();
    NumOptDynCast++;
  }
  return PreservedAnalyses::none();
}
