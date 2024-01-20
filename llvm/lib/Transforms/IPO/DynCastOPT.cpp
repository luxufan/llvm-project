#include "llvm/Transforms/IPO/DynCastOPT.h"
#include "llvm/ADT/Statistic.h"
#include "llvm/IR/Constants.h"
#include "llvm/IR/Instructions.h"
#include "llvm/ADT/SetVector.h"
#include "llvm/IR/IRBuilder.h"

#define DEBUG_TYPE "dyncastopt"

STATISTIC(NumOptDynCast, "Number of optimized dynamic_cast call site");
STATISTIC(NumOptDynCastOffsetToTopMustZero, "Number of optimized dynamic_cast call site that has must zero offset to top value");

namespace llvm {

const int64_t VirtualMask = 1;
const int64_t PublicMaks = 2;
const int64_t ShiftToOffset = 8;

void DynCastOPTPass::invalidateExternalClass(const GlobalVariable *RTTI) {
  assert(!RTTI->isInternalLinkage(RTTI->getLinkage()) &&
         "This is a internal class");
  SmallVector<const Value *> WorkList;
  WorkList.push_back(RTTI);
  while (!WorkList.empty()) {
    const Value *Current = WorkList.pop_back_val();
    if (CHA.contains(Current)) {
      for (auto &Base : CHA[Current])
        WorkList.push_back(Base.first);
    }
    Invalid.insert(Current);
  }
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
        invalidateExternalClass(&GV);
    }
  }
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

bool DynCastOPTPass::hasPrevailingVTables(const SetVector<const Value *> &RTTIs) {
  return all_of(RTTIs, [this](const Value *RTTI) {
    return VTables.contains(RTTI);
  });
}

bool DynCastOPTPass::handleDynCastCallSite(CallInst *CI) {
  LLVMContext &Context = CI->getContext();
  Function *Called = CI->getCalledFunction();
  assert(Called->hasName() && Called->getName() == "__dynamic_cast");
  Value *DestType = CI->getArgOperand(2);
  Value *StaticPtr = CI->getArgOperand(0);

  Type *PTy =
      PointerType::get(CI->getContext(), CI->getFunction()->getAddressSpace());
  // TODO: Split block to enable optimization for case that
  // static ptr and __dynamic_cast are in the same block
  if (auto *I = dyn_cast<Instruction>(StaticPtr)) {
    if (I->getParent() == CI->getParent())
      return false;
  }
  if (invalidToOptimize(DestType) || !isUniqueBaseInFullCHA(DestType))
    return false;

  SetVector<const Value *> Supers;
  getSuperClasses(DestType, Supers);

  // If not all super classes have the prevailing vritual table definition,
  // then the optimization can not be performed.
  // TODO: don't eliminate virtual table global variable in compilation if
  // -flto is enabled.
  if (!hasPrevailingVTables(Supers))
    return false;

  // FIXME: Reduce super classes by judge if there is prevailing virtual tables is wrong.
  // Although no prevailing virtual tables means there is no allocation site for corresponding
  // classes, allication sites may still exist in other linkage unit.
  // TODO: If the virtual table will not be elimiated, then we can reduce the
  // superclasses by if the virtual table is used by other regular object file.
  // If it is not used and also not used in current LTO unit, then we can remove the
  // check for this class type.


  if (Supers.size() > 2)
    return false;

  if (Supers.empty()) {
    CI->replaceAllUsesWith(ConstantInt::getNullValue(PTy));
    return true;
  }

  BasicBlock *LoadBlock = BasicBlock::Create(
      CI->getContext(), "load_block", CI->getFunction(), CI->getParent());

  IRBuilder<> IRBLoadB(LoadBlock);

  Value *RuntimePtr;
  if (!isOffsetToTopMustZero(Supers)) {
  // offset-to-top is always placed in vptr - 16. FIXME: -16 is incorrect for 32
  // bit address space.
    Value *VPtr = IRBLoadB.CreateLoad(PTy, StaticPtr, "vptr");
    Type *ByteType = Type::getInt8Ty(Context);
    Value *Idx = ConstantInt::get(Type::getInt64Ty(Context), -16);
    Value *AddrOffsetToTop = IRBLoadB.CreateInBoundsGEP(ByteType, VPtr, Idx, "add_offset_to_top");


    Value *OffsetToTop = IRBLoadB.CreateLoad(Type::getInt64Ty(Context), AddrOffsetToTop,
                                    "offset_to_top");
    RuntimePtr = IRBLoadB.CreateInBoundsGEP(
        ByteType, StaticPtr, OffsetToTop, "runtime_object");
  } else {
    NumOptDynCastOffsetToTopMustZero++;
    RuntimePtr = StaticPtr;
  }

  Value *RuntimeVPtr = new LoadInst(PTy, RuntimePtr, "runtime_vptr", LoadBlock);
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

  SmallVector<Instruction *> ToMove;
  for (auto &Phi : CI->getParent()->phis())
    ToMove.push_back(&Phi);
  for_each(ToMove, [LoadBlock](Instruction *Phi){
    Phi->moveBefore(*LoadBlock, LoadBlock->getFirstInsertionPt());
  });

  // Replace all of the branch to dynamic_cast.not_null to the first check
  // block. Only replace branch since replacing Phi node is incorrect.
  CI->getParent()->replaceUsesWithIf(
      LoadBlock, [](Use &U) { return isa<Instruction>(U.getUser()) && cast<Instruction>(U.getUser())->isTerminator(); });

  BranchInst::Create(BBs[0], LoadBlock);

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
  PHINode *ResultPhi =
      PHINode::Create(PTy, 2, "", &*CI->getParent()->begin());
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

void DynCastOPTPass::collectVirtualTables(Module &M) {
  for (GlobalVariable &GV : M.globals()) {
    if (GV.hasName() && GV.getName().starts_with("_ZTV") &&
        GV.hasInitializer()) {
      ConstantStruct *VTable = cast<ConstantStruct>(GV.getInitializer());
      ConstantArray *SubTable = cast<ConstantArray>(VTable->getOperand(0));
      uint64_t Offset = 0;
      for (Value *Element : SubTable->operand_values()) {
        Offset += 1;
        if (CHA.contains(Element)) {
          Constant *Idx[] = {
              ConstantInt::get(Type::getInt32Ty(M.getContext()), 0),
              ConstantInt::get(Type::getInt32Ty(M.getContext()), 0),
              ConstantInt::get(Type::getInt64Ty(M.getContext()), Offset)};

          Constant *AddressPointer =
              ConstantExpr::getGetElementPtr(VTable->getType(), &GV, Idx);
          assert(AddressPointer->getType()->isPointerTy());
          VTables.insert(std::make_pair(Element, AddressPointer));
          break;
        }
      }
    }
  }
}

bool DynCastOPTPass::isOffsetToTopMustZero(SetVector<const Value *> &SuperClasses) {
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
    if (!F.isInternalLinkage(F.getLinkage()))
      continue;
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

} // namespace llvm
