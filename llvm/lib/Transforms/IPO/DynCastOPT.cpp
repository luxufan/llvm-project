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
  SmallVector<GUID> WorkList;
  for_each(ExternalLinkageRTTIs,
           [&WorkList](GUID V) { WorkList.push_back(V); });

  while (!WorkList.empty()) {
    GUID Current = WorkList.pop_back_val();
    if (CHA.contains(Current)) {
      for (auto &Base : CHA[Current])
        WorkList.push_back(Base.first);
    }
    Invalids.insert(Current);
  }

  WorkList.clear();
  for_each(ExternalReferenceRTTIs,
           [&WorkList](GUID V) { WorkList.push_back(V); });
  while (!WorkList.empty()) {
    GUID Current = WorkList.pop_back_val();
    if (SuperClasses.contains(Current)) {
      for (auto Super : SuperClasses[Current])
        WorkList.push_back(Super);
    }
    Invalids.insert(Current);
  }
}

void DynCastOPTPass::recordExternalClass(const GlobalVariable *RTTI) {
  assert(!RTTI->isInternalLinkage(RTTI->getLinkage()) &&
         "This is a internal class");
  ExternalLinkageRTTIs.insert(GlobalValue::getGUID(RTTI->getName()));
}

void DynCastOPTPass::buildTypeInfoGraph(Module &M) {
  for (GlobalVariable &GV : M.globals()) {
    if (GV.hasName() && GV.getName().starts_with("_ZTI")) {
      GUID TypeGUID = GlobalValue::getGUID(GV.getName());
      if (!GV.hasInitializer()) {
        if (GV.isExternalLinkage(GV.getLinkage()))
          ExternalReferenceRTTIs.insert(TypeGUID);
        continue;
      }
      if (ConstantStruct *Initializer =
              dyn_cast<ConstantStruct>(GV.getInitializer())) {
        // __class_type_info has type { ptr, ptr }
        // __si_class_type_info has type { ptr, ptr, ptr }
        // __vmi_class_type_info has type { ptr, ptr, i32, i32, ptr, i64 ... }
        if (Initializer->getNumOperands() == 2) {
          CHA.getOrInsertDefault(TypeGUID);
        } else if (Initializer->getNumOperands() == 3) {
          Value *Base = Initializer->getOperand(2);
          GUID BaseGUID = GlobalValue::getGUID(Base->getName());
          CHA[TypeGUID].push_back(std::make_pair(BaseGUID, 0));
          SuperClasses[BaseGUID].push_back(TypeGUID);
        } else if (Initializer->getNumOperands() > 4) {
          unsigned NumBase =
              cast<ConstantInt>(Initializer->getOperand(3))->getZExtValue();
          for (unsigned I = 0; I < NumBase; I++) {
            int64_t OffsetFlag =
                cast<ConstantInt>(Initializer->getOperand(I * 2 + 5))
                    ->getSExtValue();
            Value *Base = Initializer->getOperand(I * 2 + 4);
            GUID BaseGUID = GlobalValue::getGUID(Base->getName());
            bool IsVirtual = OffsetFlag & (VirtualMask);
            assert(!IsVirtual && "Virtual inheritance");
            int64_t Offset = OffsetFlag >> ShiftToOffset;
            bool IsPublic = OffsetFlag & (PublicMaks);
            if (!IsPublic)
              continue;
            CHA[TypeGUID].push_back(std::make_pair(BaseGUID, Offset));
            SuperClasses[BaseGUID].push_back(TypeGUID);
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


void DynCastOPTPass::getSuperClasses(StringRef Class, SetVector<StringRef> &Supers) {
  auto VtableInfo = getTypeIdCompatibleVTableInfo(Class);
  if (!VtableInfo)
    return;
  for (auto &I : *VtableInfo) {
    Supers.insert(I.VTableName);
  }
}

bool DynCastOPTPass::isUniqueBaseInFullCHA(StringRef C) {
  DenseSet<StringRef> Visited;
  auto VTableInfo = getTypeIdCompatibleVTableInfo(C);
  if (!VTableInfo)
    return false;
  for (auto &I : *VTableInfo) {
    if (Visited.contains(I.VTableName))
      return false;
    Visited.insert(I.VTableName);
  }
  return true;
}

bool DynCastOPTPass::isUniqueBaseForSuper(GUID Base, GUID Super) {
  if (Base == Super)
    return true;

  unsigned ReachCount = 0;
  SmallVector<GUID> WorkList;
  WorkList.push_back(Super);
  while (!WorkList.empty()) {
    GUID Current = WorkList.pop_back_val();
    for (const auto &BasePair : CHA[Current]) {
      GUID BaseP = BasePair.first;
      if (BaseP == Base)
        ReachCount++;
      else
        WorkList.push_back(BaseP);
    }
  }
  assert(ReachCount != 0 && "Base is not a base of Super");
  return ReachCount < 2;
}

bool DynCastOPTPass::hasPrevailingVTables(StringRef Class) {
  auto VTableInfo = getTypeIdCompatibleVTableInfo(Class);
  if (!VTableInfo)
    return false;
  for (auto &VTable : *VTableInfo) {
    const GlobalVariable *VT = M->getNamedGlobal(VTable.VTableName);
    if (!VT)
      return false;
    if (!VT->hasLocalLinkage())
      return false;
  }
  return true;
}

static Value *loadRuntimePtr(Value *StaticPtr, IRBuilder<> &IRB,
                             unsigned AddressSpace,
                             bool IsOffsetToTopMustZero) {
  if (IsOffsetToTopMustZero) {
    NumOptDynCastOffsetToTopMustZero++;
    return StaticPtr;
  }

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

static std::string convertFromZTIToZTS(StringRef ZTI) {
  bool Succeed = ZTI.consume_front("_ZTI");
  assert(Succeed && "Consume from failed");
  (void)Succeed;
  return "_ZTS" + ZTI.str();
}

static std::string convertFromZTVToZTS(StringRef ZTV) {
  bool Succeed = ZTV.consume_front("_ZTV");
  assert(Succeed && "Consume from failed");
  (void)Succeed;
  return "_ZTS" + ZTV.str();
}

static bool getSplatPointers(Constant *VTableInit, SmallVectorImpl<Constant *> &Pointers) {
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
    for (Value *Field : VPtrStruct->operand_values()) {
      if (!getSplatPointers(cast<Constant>(Field), Pointers))
        return false;
    }
    return true;
  }

  if (ConstantArray *VPtrArray = dyn_cast<ConstantArray>(VTableInit)) {
    if (!VPtrArray->getType()->getElementType()->isPointerTy())
      return false;

    for (Value *Entry : VPtrArray->operand_values()) {
      Pointers.push_back(cast<Constant>(Entry));
    }
    return true;
  }
  return false;
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
  Value *StaticType = CI->getArgOperand(1);
  std::string DestTypeIdName = convertFromZTIToZTS(DestType->getName());

  auto StaticTypeIdName = convertFromZTIToZTS(StaticType->getName());
  GUID DestGUID = GlobalValue::getGUID(DestType->getName());

  Type *PTy =
      PointerType::get(CI->getContext(), CI->getFunction()->getAddressSpace());
  SetVector<StringRef> Supers;
  getSuperClasses(DestTypeIdName, Supers);

  if (invalidToOptimize(DestGUID))
    return false;

  if (Supers.empty()) {
    CI->replaceAllUsesWith(ConstantInt::getNullValue(PTy));
    return true;
  }

  if (!isUniqueBaseInFullCHA(DestTypeIdName))
    return false;



  SmallVector<std::pair<Constant *, Constant *>> CheckPoints;
  bool IsOffsetToTopMustZero = Src2DstHint->getSExtValue() == 0 && isOffsetToTopMustZero(StaticTypeIdName);

  // If not all super classes have the prevailing vritual table definition,
  // then the optimization can not be performed.
  // TODO: don't eliminate virtual table global variable in compilation if
  // -flto is enabled.

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


  for (unsigned I = 0; I < Supers.size(); I++) {
    GlobalVariable *VTableGV = M->getGlobalVariable(Supers[I], true);
    assert(VTableGV->hasInitializer());

    SmallVector<Constant *> Pointers;
    if (!getSplatPointers(VTableGV->getInitializer(), Pointers))
      assert(false);

    std::string TypeIDName = convertFromZTVToZTS(Supers[I]);
    uint64_t VTableOffset = getPrimaryAddressPoint(TypeIDName, Supers[I]);
    Constant *AddressPoint = ConstantExpr::getGetElementPtr(Type::getInt8Ty(Context), VTableGV,
                                ConstantInt::get(Type::getInt64Ty(Context), VTableOffset));

    Constant *Offset = computeOffset(DestTypeIdName, VTableGV);
    CheckPoints.push_back(std::make_pair(
        AddressPoint, Offset));
  }








  BasicBlock *LoadBlock =
      CI->getParent()->splitBasicBlock(CI, "load_block", /* Before */ true);

  Instruction *LBTerm = LoadBlock->getTerminator();
  assert(isa<BranchInst>(LBTerm));
  BranchInst *BrOfLB = dyn_cast<BranchInst>(LBTerm);
  assert(BrOfLB->isUnconditional());
  IRBuilder<> IRBLoadB(LoadBlock);
  IRBLoadB.SetInsertPoint(BrOfLB);

  Value *RuntimePtr =
      loadRuntimePtr(StaticPtr, IRBLoadB, CI->getFunction()->getAddressSpace(),
                     IsOffsetToTopMustZero);
  Value *RuntimeVPtr = IRBLoadB.CreateLoad(PTy, RuntimePtr, "runtime_vptr");


  SmallVector<BasicBlock *> BBs;
  for (unsigned I = 0; I < CheckPoints.size(); I++) {
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
      PHINode::Create(PTy, CheckPoints.size(), "", BBs.back());

  BrOfLB->setSuccessor(0, BBs[0]);

  for (unsigned I = 0; I < CheckPoints.size(); I++) {
    Value *Result =
        CmpInst::Create(Instruction::ICmp, ICmpInst::Predicate::ICMP_EQ,
                        RuntimeVPtr, CheckPoints[I].first, "", BBs[I]);
    BranchInst::Create(BBs.back(), BBs[I + 1], Result, BBs[I]);
    Phi->addIncoming(CheckPoints[I].second, BBs[I]);
  }

  // Add the base offset
  Value *ToInt = PtrToIntInst::Create(Instruction::PtrToInt, Phi, Type::getInt64Ty(Context), "", BBs.back());
  ToInt = BinaryOperator::CreateNeg(ToInt, "", BBs.back());
  Value *DestPtr = GetElementPtrInst::Create(Type::getInt8Ty(Context),
                                             RuntimePtr, ToInt, "", BBs.back());
  BranchInst::Create(CI->getParent(), BBs.back());
  PHINode *ResultPhi = PHINode::Create(PTy, 2, "", &*CI->getParent()->begin());
  ResultPhi->addIncoming(Constant::getNullValue(PTy), BBs[BBs.size() - 3]);
  ResultPhi->addIncoming(DestPtr, BBs.back());
  CI->replaceAllUsesWith(ResultPhi);
  return true;
}

Constant *DynCastOPTPass::computeOffset(StringRef BaseZTS, GlobalVariable *VTable) {

  uint64_t Offset = getPrimaryAddressPoint(BaseZTS, VTable->getName());
  assert(VTable->hasInitializer());
  SmallVector<Constant *> Pointers;
  getSplatPointers(VTable->getInitializer(), Pointers);
  Constant *OffsetToTopField = Pointers[(Offset - 2 * Layout->getPointerSize()) / Layout->getPointerSize()];
  return OffsetToTopField;
}


static Constant *getIndexesToAddressPoint(const DynCastOPTPass::CHAMapType &CHA,
                                       Constant *VTableInit,
                                       SmallVectorImpl<Constant *> &Idxs,
                                       const DataLayout *Layout) {
  SmallVector<Constant *> FunctionPointers;
  if (!getSplatPointers(VTableInit, FunctionPointers))
    return nullptr;

  uint64_t Offset = 0;
  for (auto *Pointer : FunctionPointers) {
    Offset += Layout->getPointerSize();
    if (CHA.contains(GlobalValue::getGUID(Pointer->getName()))) {
      Idxs.push_back(ConstantInt::get(
          Type::getInt64Ty(VTableInit->getContext()), Offset));
      return cast<Constant>(Pointer);
    }
  }

  return nullptr;
}

void DynCastOPTPass::collectVirtualTables(Module &M) {
  SmallVector<MDNode *> Types;
  for (GlobalVariable &GV : M.globals()) {
    Types.clear();
    GV.getMetadata(LLVMContext::MD_type, Types);
    for (MDNode *Type : Types) {
      auto TypeID = Type->getOperand(1).get();
      uint64_t Offset =
        cast<ConstantInt>(
            cast<ConstantAsMetadata>(Type->getOperand(0))->getValue())
            ->getZExtValue();

      if (auto *TypeId = dyn_cast<MDString>(TypeID)) {
        insertCompatibleAddressPoint(TypeId->getString(), GV.getName(), Offset);
      }
    }
  }
}

bool DynCastOPTPass::isOffsetToTopMustZero(StringRef Class) {
  // TODO: for non-linear, if the desitination type is the primary base class of
  // its super class, then the offset-to-top value is must 0.
  if (!hasPrevailingVTables(Class))
    return false;

  if (!isUniqueBaseInFullCHA(Class))
    return false;

  auto Result = getTypeIdCompatibleVTableInfo(Class);
  if (!Result)
    return false;

  for (auto &VTable : *Result) {
    GlobalVariable *VTableGV = M->getGlobalVariable(VTable.VTableName, true);
    assert(VTableGV->hasInitializer());
    SmallVector<Constant *> Pointers;
    if (!getSplatPointers(VTableGV->getInitializer(), Pointers))
      assert(false);

    unsigned Index = (VTable.Offset - Layout->getPointerSize() * 2) / Layout->getPointerSize();
    Constant *OffsetToTop = Pointers[Index];
    if (!isa<ConstantPointerNull>(OffsetToTop))
      return false;
  }
  return true;
}

PreservedAnalyses DynCastOPTPass::run(Module &M, ModuleAnalysisManager &) {
  Context = &M.getContext();
  this->M = &M;
  Layout = &M.getDataLayout();
  //buildTypeInfoGraph(M);
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
