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
STATISTIC(NumLeafNodes, "Number of dynamic_cast optimization that is leaf node");
STATISTIC(NumTwoCandidates, "Number of dynamic_cast optimization that has two candidates");
STATISTIC(NumThreeCandidates, "Number of dynamic_cast optimization that has three candidates");
STATISTIC(NumMoreThanThreeCandidates, "Number of dyncast_cast optimization that has more than three candidates");

using namespace llvm;

static cl::opt<unsigned> MaxSuperChecks(
    "max-super-checks", cl::init(10), cl::Hidden, cl::value_desc("N"),
    cl::desc("Only check supers with less or equal than N supers"));

bool DynCastOPTPass::doesAllAddressPointHaveDifferentVTable(std::vector<AddressPoint> AddressPoints) {
  DenseSet<StringRef> Visited;
  for (auto &I : AddressPoints) {
    if (Visited.contains(I.VTableName))
      return false;
    Visited.insert(I.VTableName);
  }
  return true;
}

bool DynCastOPTPass::allCompatibleAddressPointPrevailing(StringRef Class) {
  auto VTableInfo = getTypeIdCompatibleVTableInfo(Class);
  if (!VTableInfo)
    return false;
  for (auto &VTable : *VTableInfo) {
    const GlobalVariable *VT = M->getNamedGlobal(VTable.VTableName);
    if (!VT)
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

  if (invalidToOptimize(DestTypeIdName))
    return false;

  auto StaticTypeIdName = convertFromZTIToZTS(StaticType->getName());

  Type *PTy =
      PointerType::get(CI->getContext(), CI->getFunction()->getAddressSpace());
  auto CompatibleAddressPoints = getTypeIdCompatibleVTableInfo(DestTypeIdName);
  if (!CompatibleAddressPoints)
    return false;
  std::vector<AddressPoint> NecessaryAddressPoints = *CompatibleAddressPoints;

  if (NecessaryAddressPoints.empty()) {
    CI->replaceAllUsesWith(ConstantInt::getNullValue(PTy));
    return true;
  }

  switch (NecessaryAddressPoints.size()) {
    case 1:
      NumLeafNodes++;
      break;
    case 2:
      NumTwoCandidates++;
      break;
    case 3:
      NumThreeCandidates++;
      break;
    default:
      NumMoreThanThreeCandidates++;
  }

  if (!allCompatibleAddressPointPrevailing(DestTypeIdName))
    return false;

  if (!doesAllAddressPointHaveDifferentVTable(NecessaryAddressPoints))
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
  if (NecessaryAddressPoints.size() > MaxSuperChecks)
    return false;

  for (unsigned I = 0; I < NecessaryAddressPoints.size(); I++) {
    GlobalVariable *VTableGV = M->getGlobalVariable(NecessaryAddressPoints[I].VTableName, true);
    assert(VTableGV->hasInitializer());

    SmallVector<Constant *> Pointers;
    if (!getSplatPointers(VTableGV->getInitializer(), Pointers))
      assert(false);

    std::string TypeIDName = convertFromZTVToZTS(NecessaryAddressPoints[I].VTableName);
    uint64_t VTableOffset = getPrimaryAddressPoint(NecessaryAddressPoints[I].VTableName);
    Constant *AddressPoint = ConstantExpr::getGetElementPtr(Type::getInt8Ty(Context), VTableGV,
                                ConstantInt::get(Type::getInt64Ty(Context), VTableOffset));

    Constant *OffsetToTop = getOffsetToTop(VTableGV, NecessaryAddressPoints[I].Offset);
    CheckPoints.push_back(std::make_pair(
        AddressPoint, OffsetToTop));
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

Constant *DynCastOPTPass::getOffsetToTop(GlobalVariable *VTable, uint64_t Offset) {
  assert(VTable->hasInitializer());
  SmallVector<Constant *> Pointers;
  getSplatPointers(VTable->getInitializer(), Pointers);
  Constant *OffsetToTopField = Pointers[(Offset - 2 * Layout->getPointerSize()) / Layout->getPointerSize()];
  return OffsetToTopField;
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
        insertTypeIdCompatibleAddressPoint(TypeId->getString(), GV.getName(), Offset);
        if (GV.getVCallVisibility() == GlobalObject::VCallVisibilityPublic)
          Invalids.insert(TypeId->getString());
      }
    }
  }
}

bool DynCastOPTPass::isOffsetToTopMustZero(StringRef Class) {
  if (!allCompatibleAddressPointPrevailing(Class))
    return false;

  auto Result = getTypeIdCompatibleVTableInfo(Class);
  if (!Result)
    return false;

  for (auto &VTable : *Result) {
    GlobalVariable *VTableGV = M->getGlobalVariable(VTable.VTableName, true);
    assert(VTableGV->hasInitializer());
    Constant *OffsetToTop = getOffsetToTop(VTableGV, VTable.Offset);
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
  GlobalValue *DyncastDecl = M.getNamedValue("__dynamic_cast");
  if (!DyncastDecl)
    return PreservedAnalyses::all();
  for (auto User : DyncastDecl->users()) {
    if (CallInst *C = dyn_cast<CallInst>(User)) {
      if (handleDynCastCallSite(C))
        Deleted.push_back(C);
    }
  }

  if (Deleted.empty())
    return PreservedAnalyses::all();

  for (auto *CI : Deleted) {
    CI->eraseFromParent();
    NumOptDynCast++;
  }

  // Metadata vcall_visibility will no longer be used.
  for (GlobalVariable &GV : M.globals())
    GV.eraseMetadata(LLVMContext::MD_vcall_visibility);

  return PreservedAnalyses::none();
}
