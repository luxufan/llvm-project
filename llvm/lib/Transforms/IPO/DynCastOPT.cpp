#include "llvm/Transforms/IPO/DynCastOPT.h"
#include "llvm/ADT/SetVector.h"
#include "llvm/ADT/Statistic.h"
#include "llvm/Analysis/ConstantFolding.h"
#include "llvm/IR/Constants.h"
#include "llvm/IR/IRBuilder.h"
#include "llvm/IR/Instructions.h"
#include "llvm/Bitcode/BitcodeReader.h"
#include "llvm/Bitcode/BitcodeWriter.h"
#include "llvm/Support/FileSystem.h"
#include "llvm/Support/MemoryBuffer.h"
#include "llvm/Support/YAMLTraits.h"
#include "llvm/IR/ModuleSummaryIndex.h"
#include "llvm/IR/ModuleSummaryIndexYAML.h"
#include "llvm/Transforms/IPO.h"
#include "llvm/Analysis/ValueTracking.h"

#define DEBUG_TYPE "dyncastopt"

STATISTIC(NumZTV, "");
STATISTIC(NumTotalInstr, "");
STATISTIC(NumMultiDynamic, "");
STATISTIC(NumHeightSum, "");
STATISTIC(NumWidthSum, "");
STATISTIC(NumMaxWidth, "");
STATISTIC(NumDAG, "");
STATISTIC(NumNonTree, "");
STATISTIC(NumNonClosed, "");
STATISTIC(NumNonInternalizedClass, "");
STATISTIC(NumUsedByDyncast, "");
STATISTIC(NumUsedByEH, "");
STATISTIC(NumTotalClasses, "");
STATISTIC(NumFinalKeyword, "");
STATISTIC(NumGraph, "");
STATISTIC(NumHeight1, "");
STATISTIC(NumHeight2, "");
STATISTIC(NumHeight3, "");
STATISTIC(NumHeight4, "");
STATISTIC(NumHeight5, "");
STATISTIC(NumHeight6, "");
STATISTIC(NumHeight7, "");
STATISTIC(NumHeight8, "");
STATISTIC(NumHeight9, "");
STATISTIC(NumHeight10, "");
STATISTIC(NumHeightMoreThan10, "");

STATISTIC(NumWidth1, "");
STATISTIC(NumWidth2, "");
STATISTIC(NumWidth3, "");
STATISTIC(NumWidth4, "");
STATISTIC(NumWidth5, "");
STATISTIC(NumWidth6, "");
STATISTIC(NumWidth7, "");
STATISTIC(NumWidth8, "");
STATISTIC(NumWidth9, "");
STATISTIC(NumWidth10, "");
STATISTIC(NumWidth11, "");
STATISTIC(NumWidth12, "");
STATISTIC(NumWidth13, "");
STATISTIC(NumWidth14, "");
STATISTIC(NumWidth15, "");
STATISTIC(NumWidth16, "");
STATISTIC(NumWidth17, "");
STATISTIC(NumWidth18, "");
STATISTIC(NumWidth19, "");
STATISTIC(NumWidth20, "");
STATISTIC(NumWidth21, "");
STATISTIC(NumWidth22, "");
STATISTIC(NumWidth23, "");
STATISTIC(NumWidth24, "");
STATISTIC(NumWidth25, "");
STATISTIC(NumWidth26, "");
STATISTIC(NumWidth27, "");
STATISTIC(NumWidth28, "");
STATISTIC(NumWidth29, "");
STATISTIC(NumWidth30, "");
STATISTIC(NumWidth31, "");
STATISTIC(NumWidth32, "");
STATISTIC(NumWidth33, "");
STATISTIC(NumWidth34, "");
STATISTIC(NumWidth35, "");
STATISTIC(NumWidth36, "");
STATISTIC(NumWidth37, "");
STATISTIC(NumWidth38, "");
STATISTIC(NumWidth39, "");
STATISTIC(NumWidth40, "");
STATISTIC(NumWidth41, "");
STATISTIC(NumWidth42, "");
STATISTIC(NumWidth43, "");
STATISTIC(NumWidth44, "");
STATISTIC(NumWidth45, "");
STATISTIC(NumWidth46, "");
STATISTIC(NumWidth47, "");
STATISTIC(NumWidth48, "");
STATISTIC(NumWidth49, "");
STATISTIC(NumWidth50, "");
STATISTIC(NumWidthMoreThan50, "");

STATISTIC(NumWidthRange1, "");
STATISTIC(NumWidthRange2, "");
STATISTIC(NumWidthRange3, "");
STATISTIC(NumWidthRange4, "");
STATISTIC(NumWidthRange5, "");


STATISTIC(NumNode1, "");
STATISTIC(NumNode2, "");
STATISTIC(NumNode3, "");
STATISTIC(NumNode4, "");
STATISTIC(NumNode5, "");
STATISTIC(NumNode6, "");
STATISTIC(NumNode7, "");
STATISTIC(NumNode8, "");
STATISTIC(NumNode9, "");
STATISTIC(NumNode10, "");
STATISTIC(NumNode11, "");
STATISTIC(NumNode12, "");
STATISTIC(NumNode13, "");
STATISTIC(NumNode14, "");
STATISTIC(NumNode15, "");
STATISTIC(NumNode16, "");
STATISTIC(NumNode17, "");
STATISTIC(NumNode18, "");
STATISTIC(NumNode19, "");
STATISTIC(NumNode20, "");
STATISTIC(NumNode21, "");
STATISTIC(NumNode22, "");
STATISTIC(NumNode23, "");
STATISTIC(NumNode24, "");
STATISTIC(NumNode25, "");
STATISTIC(NumNode26, "");
STATISTIC(NumNode27, "");
STATISTIC(NumNode28, "");
STATISTIC(NumNode29, "");
STATISTIC(NumNode30, "");
STATISTIC(NumNode31, "");
STATISTIC(NumNode32, "");
STATISTIC(NumNode33, "");
STATISTIC(NumNode34, "");
STATISTIC(NumNode35, "");
STATISTIC(NumNode36, "");
STATISTIC(NumNode37, "");
STATISTIC(NumNode38, "");
STATISTIC(NumNode39, "");
STATISTIC(NumNode40, "");
STATISTIC(NumNode41, "");
STATISTIC(NumNode42, "");
STATISTIC(NumNode43, "");
STATISTIC(NumNode44, "");
STATISTIC(NumNode45, "");
STATISTIC(NumNode46, "");
STATISTIC(NumNode47, "");
STATISTIC(NumNode48, "");
STATISTIC(NumNode49, "");
STATISTIC(NumNode50, "");
STATISTIC(NumNodeMoreThan50, "");

STATISTIC(NumNodeRange1, "");
STATISTIC(NumNodeRange2, "");
STATISTIC(NumNodeRange3, "");
STATISTIC(NumNodeRange4, "");
STATISTIC(NumNodeRange5, "");

STATISTIC(NumPrivate, "");
STATISTIC(NumPublic, "");
STATISTIC(NumMulti, "");
STATISTIC(NumVirtual, "");


STATISTIC(NumOptDynCast, "Number of optimized dynamic_cast call site");
STATISTIC(NumDynCast, "Number of dynamic_cast call site");
STATISTIC(NumOffsetToTopMustBeZero,
          "Number of optimized dynamic_cast call site that has must zero "
          "offset to top value");
STATISTIC(NumHasLeafNodes,
          "Number of dynamic_cast optimization that is leaf node");
STATISTIC(NumHasTwoCandidates,
          "Number of dynamic_cast optimization that has two candidates");
STATISTIC(NumHasThreeCandidates,
          "Number of dynamic_cast optimization that has three candidates");
STATISTIC(NumHasFourCandidates, "");
STATISTIC(NumHasFiveCandidates, "");
STATISTIC(NumHasSixCandidates, "");
STATISTIC(NumHasSevenCandidates, "");
STATISTIC(
    NumHasMoreThanSevenCandidates,
    "Number of dyncast_cast optimization that has more than three candidates");
STATISTIC(NumLeafNodes,
          "Number of dynamic_cast optimization that is leaf node");
STATISTIC(NumTwoCandidates,
          "Number of dynamic_cast optimization that has two candidates");
STATISTIC(NumThreeCandidates,
          "Number of dynamic_cast optimization that has three candidates");
STATISTIC(
    NumMoreThanThreeCandidates,
    "Number of dyncast_cast optimization that has more than three candidates");
STATISTIC(NumSpeculatableAddress,
          "Number of speculatable returned addresses if dynamic_cast success");
STATISTIC(NumNegativeTest, "Number of negative test");
STATISTIC(NumZeroCheck, "Number of optimization that don't need to check");
STATISTIC(NumRangeCheck, "Number of optimization that using range check");
STATISTIC(NumLayoutVTables, "Number of virtual tables that are layouted");
STATISTIC(NumNoHint, "Number of no hint __dynamic_cast");
STATISTIC(NumPrivateBase, "Number of private base");
STATISTIC(NumMultiBase, "Number of multiple base type");
STATISTIC(NumZeroHint, "");
STATISTIC(NumPositiveHint, "");
STATISTIC(NumNoAddressPoints, "Number of dyncast that has no address points");
STATISTIC(NumNonFixedInLTO, "Number of dyncast that has no address points");
STATISTIC(NumLayoutCandidates, "Number of dyncast that has no address points");
STATISTIC(NumNullPtr, "");
STATISTIC(NumFinal, "");
STATISTIC(NumClasses, "");

using namespace llvm;

static cl::opt<std::string> DestTypeFile("dest-type-file",
                                         cl::desc("vtable file"));

static cl::opt<bool> ExportMergedVTables("export-merged-vtables",
                                         cl::init(true),
                                         cl::desc("vtable file"));

static cl::opt<std::string> ExtractType("extract", cl::desc("vtable name"));

static cl::opt<bool> OutputDestType("output-dest-type", cl::init(false),
                                    cl::Hidden, cl::desc("output vtables"));

static cl::opt<std::string> ClReadSummary(
  "dyncastopt-read-summary",
  cl::desc("Read summary from given bitcode or YAML file before running pass"),
  cl::Hidden);

static cl::opt<std::string> ClWriteSummary(
    "dyncastopt-write-summary",
    cl::desc("Write summary to given bitcode or YAML file after running pass. "
             "Output file format is deduced from extension: *.bc means writing "
             "bitcode, otherwise YAML"),
    cl::Hidden);

static cl::opt<unsigned> MaxSuperChecks(
    "max-super-checks", cl::init(100), cl::Hidden, cl::value_desc("N"),
    cl::desc("Only check supers with less or equal than N supers"));

static cl::opt<bool> EnableNegativeTest("enable-negative-test", cl::init(false),
                                        cl::Hidden,
                                        cl::desc("Enable negative test"));

static cl::opt<bool> EnableRangeCheck("enable-range-check", cl::init(true),
                                      cl::Hidden,
                                      cl::desc("Enable range check"));

static cl::opt<unsigned> RangeCheckThreshold(
    "range-check-threshold", cl::init(2), cl::Hidden, cl::value_desc("N"),
    cl::desc("Do range check if the number of candidates greater than N"));

static cl::opt<PassSummaryAction> ClSummaryAction(
    "dyncastopt-summary-action",
    cl::desc("What to do with the summary when running this pass"),
    cl::values(clEnumValN(PassSummaryAction::None, "none", "Do nothing"),
               clEnumValN(PassSummaryAction::Import, "import",
                          "Import type metadata from summary and globals"),
               clEnumValN(PassSummaryAction::Export, "export",
                          "Export type metadata to summary and globals")),
    cl::Hidden);

Value *DynCastOPTPass::loadRuntimeVPtr(Value *RuntimePtr, IRBuilder<> &IRB,
                                       Type *PTy) {
  return IRB.CreateLoad(PTy, RuntimePtr, "runtime_vptr");
}

Value *DynCastOPTPass::loadRuntimePtr(Value *StaticPtr, IRBuilder<> &IRB,
                                      Type *PTy, StringRef StaticTypeId,
                                      bool OffsetToTopMustBeZero) {
  if (OffsetToTopMustBeZero) {
    NumOffsetToTopMustBeZero++;
    return StaticPtr;
  }

  Value *StaticVPtr = IRB.CreateLoad(PTy, StaticPtr, "vptr");
  Metadata *TypeId = MDString::get(*Context, StaticTypeId);

  Value *Args[] = {StaticVPtr, MetadataAsValue::get(*Context, TypeId)};
  Value *Test = IRB.CreateIntrinsic(Int1Ty, Intrinsic::type_test, Args);
  IRB.CreateIntrinsic(VoidTy, Intrinsic::assume, Test);
  Value *Idx =
      ConstantInt::getSigned(Int64Ty, -OffsetFromOffsetToTopToAddressPoint *
                                          (int)DL->getPointerSize());
  Value *AddrOfOffsetToTop =
      IRB.CreateInBoundsGEP(Int8Ty, StaticVPtr, Idx, "add_offset_to_top");

  Value *OffsetToTop =
      IRB.CreateLoad(Int64Ty, AddrOfOffsetToTop, "offset_to_top");
  return IRB.CreateInBoundsGEP(Int8Ty, StaticPtr, OffsetToTop,
                               "runtime_object");
}

Value *DynCastOPTPass::speculateAddress(ConstantInt *Src2DstHint,
                                        Value *StaticPtr,
                                        StringRef DestTypeIdName, Type *PTy,
                                        IRBuilder<> &IRB) {
  int64_t Hint = Src2DstHint->getSExtValue();

  if (Hint == 0)
    return StaticPtr;

  if (Hint > 0)
    return IRB.CreateInBoundsGEP(Type::getInt8Ty(*Context), StaticPtr,
                                 IRB.CreateNeg(Src2DstHint));

  return nullptr;
}

BasicBlock *DynCastOPTPass::compareAddressPoint(
    Constant *AddressPoint, Value *RuntimeVPtr, BasicBlock *CheckSuccess,
    BasicBlock *CheckFail, BasicBlock *InsertPt, ICmpInst::Predicate Pred) {

  BasicBlock *CheckBlock =
      BasicBlock::Create(*Context, "", InsertPt->getParent(), InsertPt);

  // Make sure jump to the first check block.
  Value *Result = CmpInst::Create(Instruction::ICmp, Pred, RuntimeVPtr,
                                  AddressPoint, "", CheckBlock);

  BranchInst::Create(CheckSuccess, CheckFail, Result, CheckBlock);
  return CheckBlock;
}

std::pair<BasicBlock *, BasicBlock *> DynCastOPTPass::compareAddressPoints(
    SmallVectorImpl<AddressPoint> &AddressPoints, Value *RuntimeVPtr,
    BasicBlock *InsertPt, BasicBlock *CheckSuccess, BasicBlock *Landing,
    bool NegativeTest) {
  BasicBlock *CheckBlock = nullptr;
  BasicBlock *FirstCheck = nullptr;
  BranchInst *LastBranch = nullptr;
  for (unsigned I = 0; I < AddressPoints.size(); I++) {
    GlobalValue *VTableGV = M->getNamedValue(AddressPoints[I].VTableName);
    assert(VTableGV && "Virtual table group are expected to exist");

    std::string TypeIDName =
        ABIManager::GetTypeIdFromVTable(AddressPoints[I].VTableName);
    Constant *AddressExpr = getAddressPointExpr(AddressPoints[I]);

    CheckBlock = compareAddressPoint(
        AddressExpr, RuntimeVPtr, CheckSuccess, Landing, InsertPt,
        NegativeTest ? ICmpInst::Predicate::ICMP_NE
                     : ICmpInst::Predicate::ICMP_EQ);
    CheckBlock->setName("check_point." + Twine(I));

    // Make sure jump to the first check block.
    if (I == 0)
      FirstCheck = CheckBlock;

    // Make sure jump into this check block if last check fail.
    if (LastBranch)
      LastBranch->setOperand(1, CheckBlock);

    LastBranch = cast<BranchInst>(CheckBlock->getTerminator());
  }
  return std::make_pair(FirstCheck, CheckBlock);
}

bool DynCastOPTPass::getDynCastInfo(CallInst *CI, DynCastInfo *Info) {
  Info->Block = CI->getParent();
  Info->StaticPtr = CI->getArgOperand(0);
  Info->Src2DstHint = cast<ConstantInt>(CI->getArgOperand(3));
  Value *DestType = CI->getArgOperand(2);
  Value *StaticType = CI->getArgOperand(1);
  if (!ABIManager::IsTypeInfo(DestType->getName()) ||
      !ABIManager::IsTypeInfo(StaticType->getName()))
    return false;
  Info->DestTypeIdName = ABIManager::GetTypeIdFromTypeInfo(DestType->getName());
  Info->StaticTypeIdName =
      ABIManager::GetTypeIdFromTypeInfo(StaticType->getName());
  return true;
}

bool DynCastOPTPass::determineLayoutOrder(
    StringRef DestTypeIdName, SmallVectorImpl<StringRef> &VTables,
    StringMap<std::pair<unsigned, unsigned>> &Ranges) {

  assert(CHAInfo->hasCompatibleAddressPoints(DestTypeIdName) &&
         "Dest type does not have compatible address points");
  auto &AP = CHAInfo->getTypeIdCompatibleVTableInfo(DestTypeIdName);

  std::set<AddressPoint> AddressPoints = AP;
  unsigned StartRange = VTables.size();

  Ranges.insert(std::make_pair(DestTypeIdName, std::make_pair(StartRange, 0)));

  SmallVector<StringRef, 8> LayoutOrder;
  for (auto &Iter : AddressPoints) {
    // Use starts_with because for anonymous class, the vtable name has a hash code
    // suffix. But the type id identifier does not have.
    if (Iter.VTableName.starts_with(
        ABIManager::GetVTableNameFromTypeId(DestTypeIdName))) {
      VTables.push_back(Iter.VTableName);
      AddressPoints.erase(Iter);
      break;
    }
  }

  std::set<std::string> TypeIds;
  std::set<AddressPoint> Rest = AddressPoints;

  while (!Rest.empty()) {
    TypeIds.clear();
    for (auto &AP : Rest) {
      // TODO: support deal with type id that has not corresponding vtable.
      StringRef VTable = AP.VTableName.substr(0, AP.VTableName.find('.'));
      TypeIds.insert(ABIManager::GetTypeIdFromVTable(VTable));
    }
    StringRef TypeIdWithMaxAPs;
    unsigned MaxAPs = 0;
    for (auto &TypeId : TypeIds) {
      auto &Compatibles = CHAInfo->getTypeIdCompatibleVTableInfo(TypeId);
      if (Compatibles.size() > MaxAPs) {
        MaxAPs = Compatibles.size();
        TypeIdWithMaxAPs = CHAInfo->getTypeIdMap().find(TypeId)->first;
      }
    }
    auto &SubClassCompatibles = CHAInfo->getTypeIdCompatibleVTableInfo(TypeIdWithMaxAPs);
    for (auto &AP : SubClassCompatibles)
      Rest.erase(AP);
    TypeIds.erase(TypeIdWithMaxAPs.str());
    LayoutOrder.push_back(TypeIdWithMaxAPs);
  }

  for (auto Iter : LayoutOrder) {
    determineLayoutOrder(Iter, VTables, Ranges);
  }

  Ranges[DestTypeIdName.str()].second = VTables.size();
  return true;
}

static bool readVTableFile(std::set<std::string> &VTableSet) {
  if (DestTypeFile.empty())
    return false;

  ErrorOr<std::unique_ptr<MemoryBuffer>> Text =
      MemoryBuffer::getFileAsStream(DestTypeFile);
  if (std::error_code EC = Text.getError()) {
    llvm::errs() << "Can't read " << DestTypeFile << " " << EC.message()
                 << "\n";
    llvm_unreachable("Expect correct file name!");
  }
  StringRef Content = Text->get()->getBuffer();
  SmallVector<StringRef, 128> Lines;
  Content.split(Lines, "\n");
  for (StringRef Line : Lines)
    VTableSet.insert(Line.str());
  return true;
}

static std::optional<std::unique_ptr<raw_fd_ostream>> outputDestTypeFile() {
  if (OutputDestType) {
    std::error_code EC;

    std::unique_ptr<raw_fd_ostream> OutputVTables =
        std::make_unique<raw_fd_ostream>("dest-type-output.txt", EC,
                                         sys::fs::OpenFlags::OF_None);
    if (EC)
      llvm_unreachable("Can not create dest-type-output.txt file");
    return OutputVTables;
  }
  return std::nullopt;
}

bool DynCastOPTPass::layoutVTables() {
  assert(UseCommandLine || ExportSummary);
  std::vector<std::pair<std::string, unsigned>> Candidates;
  if (ExportSummary && ExportSummary->Thin) {
    for (auto &Iter : ExportSummary->dynCastDstMap()) {
        if (!CHAInfo->hasCompatibleAddressPoints(Iter.first().str()))
          continue;

        if (!CHAInfo->isFixedInLinkTime(Iter.first()))
          continue;

        auto &DestAddressPoints =
            CHAInfo->getTypeIdCompatibleVTableInfo(Iter.first().str());
        if (DestAddressPoints.size() > RangeCheckThreshold) {
          Candidates.push_back(
              std::make_pair(Iter.first().str(), DestAddressPoints.size()));
          NumLayoutCandidates++;
        }
    }
  } else {
    GlobalValue *DyncastDecl = M->getNamedValue("__dynamic_cast");
    if (DyncastDecl) {
      for (auto *User : DyncastDecl->users()) {
        if (auto *CI = dyn_cast<CallInst>(User)) {
          DynCastInfo Info;
          getDynCastInfo(CI, &Info);
          if (!CHAInfo->hasCompatibleAddressPoints(Info.StaticTypeIdName))
            continue;

          if (!CHAInfo->isFixedInLinkTime(Info.StaticTypeIdName))
            continue;

          auto &DestAddressPoints = CHAInfo->getTypeIdCompatibleVTableInfo(Info.StaticTypeIdName);
          if (DestAddressPoints.size() > RangeCheckThreshold) {
            Candidates.push_back(std::make_pair(Info.StaticTypeIdName, DestAddressPoints.size()));
            NumLayoutCandidates++;
          }
        }
      }
    }
  }

  std::sort(Candidates.begin(), Candidates.end(),
            [](const std::pair<std::string, unsigned> &Lhs,
               const std::pair<std::string, unsigned> &Rhs) {
              return Lhs.second > Rhs.second;
            });

  auto CreateFile = [&]() -> std::optional<std::unique_ptr<raw_fd_ostream>> {
    if (!ExportMergedVTables)
      return std::nullopt;
    std::error_code EC;
    std::unique_ptr<raw_fd_ostream> File = std::make_unique<raw_fd_ostream>(
        "merged-vtables.txt", EC, sys::fs::OpenFlags::OF_None);
    if (EC)
      llvm_unreachable("Can not create merged-vtables.txt file");
    return File;
  };
  auto File = CreateFile();

  for (auto Pair : Candidates) {
    if (layoutVTableImpl(Pair.first, File ? File->get() : nullptr))
      NumLayoutVTables++;
  }
  return true;
}

bool DynCastOPTPass::layoutVTableImpl(std::string DestTypeIdName,
                                      raw_fd_ostream *OS) {

  if (!CHAInfo->hasCompatibleAddressPoints(DestTypeIdName))
    return false;
  auto &DestAddressPoints =
      CHAInfo->getTypeIdCompatibleVTableInfo(DestTypeIdName);

  for (auto AddressPoint : DestAddressPoints) {
    if (RangeMap.count(
            ABIManager::GetTypeIdFromVTable(AddressPoint.VTableName)))
      return false;

    // TODO: this is temparary restrict, should be relaxed after.
    if (AddressPoint.Offset != 2 * DL->getPointerSize())
      return false;

    if (ExportSummary) {

      GlobalVariable *VTable = M->getNamedGlobal(AddressPoint.VTableName);
      if (!VTable)
        return false;

      if (VTable->getVCallVisibility() == GlobalValue::VCallVisibilityPublic) {
        if (auto *GVS = dyn_cast<GlobalVarSummary>(ExportSummary->getGlobalValueSummary(*VTable, false)))
          if (GVS->getVCallVisibility() == GlobalValue::VCallVisibilityPublic) {
            // If the virtual table has non-internal linkage like weak_odr,
            // then its definition may be replaced at link time. Merging them
            // and performing range check is not sound.
            return false;
          }
      }
    } else {
      GlobalVariable *VTable = M->getNamedGlobal(AddressPoint.VTableName);
      if (!VTable) {
        LLVM_DEBUG(dbgs() << "Virtual table " << AddressPoint.VTableName << "is not prevailing\n");
        return false;
      }

      GlobalVarSummary *GVS = dyn_cast<GlobalVarSummary>(ExportSummary->
        getGlobalValueSummary(GlobalValue::getGUID(AddressPoint.VTableName), false));
      assert(GVS);
      if (GVS->getVCallVisibility() == GlobalObject::VCallVisibilityPublic)
        return false;
    }
  }

  SmallVector<Type *> VTableTypes;
  SmallVector<Constant *> Initializers;
  DenseMap<Value *, unsigned> IndexInMergedVTable;
  StringMap<std::pair<unsigned, unsigned>> RangeIndexes;
  SmallVector<StringRef, 8> Layout;
  LLVM_DEBUG(dbgs() << "Start layout " << DestTypeIdName << ":\n");
  determineLayoutOrder(DestTypeIdName, Layout, RangeIndexes);
  // Output merged virtual tables for debug
  for (auto VTable : Layout) {
    if (OS)
      *OS << VTable << "\n";
  }
  LLVM_DEBUG(dbgs() << "  Merging virtual tables: ");
  for (auto VTableName : Layout) {
    LLVM_DEBUG(dbgs() << VTableName << " ");
    GlobalVariable *VTable = M->getNamedGlobal(VTableName);
    Constant *Initializer = VTable->getInitializer();
    Initializers.push_back(Initializer);
    VTableTypes.push_back(VTable->getValueType());
  }
  LLVM_DEBUG(dbgs() << "\n");

  StructType *MergedType = StructType::create(VTableTypes);
  Constant *MergedInitializer = ConstantStruct::get(MergedType, Initializers);
  GlobalVariable *MergedVTable = new GlobalVariable(
      MergedType, true, GlobalValue::ExternalLinkage, MergedInitializer,
      ABIManager::GetVTableNameFromTypeId(DestTypeIdName) + ".merged");
  M->insertGlobalVariable(MergedVTable);


  std::string MergedVTableName = MergedVTable->getName().str();

  for (auto &Iter : RangeIndexes) {
    unsigned BitWidth = DL->getIndexTypeSizeInBits(MergedVTable->getType());
    APInt BeginOffset(BitWidth, 0), EndOffset(BitWidth, 0);
    GEPOperator::accumulateConstantOffset(MergedType, {ConstantInt::get(Int32Ty, 0),
      ConstantInt::get(Int32Ty, Iter.second.first)}, *DL, BeginOffset);
    if (Iter.second.second == Layout.size())
      EndOffset = APInt(BitWidth, DL->getTypeStoreSize(MergedInitializer->getType()));
    else
      GEPOperator::accumulateConstantOffset(MergedType, {ConstantInt::get(Int32Ty, 0),
        ConstantInt::get(Int32Ty, Iter.second.second)}, *DL, EndOffset);

    if (!ExportSummary)
      RangeMap.insert(std::make_pair(Iter.first(),
                                   std::make_pair(MergedVTableName,
            std::make_pair(BeginOffset.getZExtValue(), EndOffset.getZExtValue()))));
    else
      ExportSummary->RangeMap.insert(std::make_pair(Iter.first(),
                                   std::make_pair(MergedVTableName,
            std::make_pair(BeginOffset.getZExtValue(), EndOffset.getZExtValue()))));
  }

  for (unsigned I = 0; I < Layout.size(); I++) {
    GlobalVariable *VTable = M->getNamedGlobal(Layout[I]);
    Constant *Initializer = VTable->getInitializer();
    Constant *Indexes[] = {
        ConstantInt::get(Int32Ty, 0),
        ConstantInt::get(Int32Ty, I),
    };
    Constant *GEPOfVTable = ConstantExpr::getInBoundsGetElementPtr(
        MergedType, MergedVTable, Indexes);
    APInt Offset(DL->getIndexTypeSizeInBits(MergedVTable->getType()),
                 0);
    GEPOfVTable->stripAndAccumulateConstantOffsets(*DL, Offset,
                                                   false);

    GlobalAlias *VTableAlias = GlobalAlias::create(
        Initializer->getType(), 0,
        GlobalValue::WeakODRLinkage, "", GEPOfVTable, M);

    VTableAlias->setVisibility(GlobalValue::HiddenVisibility);
    VTableAlias->setDSOLocal(true);

    MergedVTable->copyMetadata(VTable, Offset.getZExtValue());
    VTableAlias->takeName(VTable);
    VTable->replaceAllUsesWith(VTableAlias);
    VTable->eraseFromParent();
  }

  return true;
}

bool DynCastOPTPass::doRangeTest(CallInst *CI) {
  DynCastInfo Info;
  getDynCastInfo(CI, &Info);

  GlobalVariable *MergedVTable;
  uint64_t BeginOffset, EndOffset;
  if (!ImportSummary) {
    auto Iter = ExportSummary->RangeMap.find(Info.DestTypeIdName);
    if (Iter == ExportSummary->RangeMap.end())
      return false;

    MergedVTable = M->getNamedGlobal(Iter->second.first);
    BeginOffset = Iter->second.second.first;
    EndOffset = Iter->second.second.second;
  } else {
    auto Iter = ImportSummary->RangeMap.find(Info.DestTypeIdName);
    if (Iter == ImportSummary->RangeMap.end())
      return false;

    MergedVTable = M->getNamedGlobal(Iter->second.first);
    if (!MergedVTable) {
      MergedVTable = new GlobalVariable(PTy, false, GlobalValue::ExternalLinkage,
                                        nullptr, Iter->second.first);
      MergedVTable->setVisibility(GlobalValue::HiddenVisibility);
      MergedVTable->setDSOLocal(true);
      M->insertGlobalVariable(MergedVTable);
    }
    BeginOffset = Iter->second.second.first;
    EndOffset = Iter->second.second.second;
  }

  Constant *StartIndices[] = {
      ConstantInt::get(Int64Ty, BeginOffset),
  };
  Constant *RangeBegin = ConstantExpr::getInBoundsGetElementPtr(
      Int8Ty, MergedVTable, StartIndices);
  Constant *RangeEnd = ConstantExpr::getInBoundsGetElementPtr(Int8Ty,
              MergedVTable, ConstantInt::get(Int64Ty, EndOffset));

  IRBuilder<> IRB(Info.Block);
  IRB.SetInsertPoint(CI);
  PointerType *PTy =
      PointerType::get(*Context, CI->getFunction()->getAddressSpace());

  Value *RuntimeVPtr = loadRuntimeVPtr(
      Info.Src2DstHint->getSExtValue() == 0
          ? Info.StaticPtr
          : loadRuntimePtr(Info.StaticPtr, IRB, PTy, Info.StaticTypeIdName,
                           offsetToTopMustBeZero(Info.StaticTypeIdName)),
      IRB, PTy);
  Value *CheckStart = IRB.CreateICmp(ICmpInst::ICMP_UGE, RuntimeVPtr,
                                     RangeBegin, "check_begin");
  Value *CheckEnd = IRB.CreateICmp(ICmpInst::ICMP_ULE, RuntimeVPtr, RangeEnd);
  Value *Succeed = IRB.CreateAnd({CheckStart, CheckEnd});
  Value *Address = speculateAddress(Info.Src2DstHint, Info.StaticPtr,
                                    Info.DestTypeIdName, PTy, IRB);
  Value *Result =
      IRB.CreateSelect(Succeed, Address, ConstantPointerNull::get(PTy));

  CI->replaceAllUsesWith(Result);
  NumRangeCheck++;
  return true;
}

void DynCastOPTPass::createVTables(
    SmallVectorImpl<AddressPoint> &AddressPoints) {
  for (auto &AP : AddressPoints) {
    GlobalValue *GV = M->getNamedValue(AP.VTableName);
    if (!GV) {
      GlobalVariable *Reference = new GlobalVariable(
          PTy, false, GlobalValue::ExternalLinkage, nullptr, AP.VTableName);
      Reference->setDSOLocal(true);
      M->insertGlobalVariable(Reference);
    }
  }
}

static bool isGuaranteedNotToBeOptimized(std::string &DestTypeId, int64_t Hint,
                                         ClassHierarchyInfo &CHAInfo) {
  // src2dst_offset: a static hint about the location of the
  // source subobject with respect to the complete object;
  // special negative values are:
  //    -1: no hint
  //    -2: src is not a public base of dst
  //    -3: src is a multiple public base type but never a
  //        virtual base type
  // otherwise, the src type is a unique public nonvirtual
  // base type of dst at offset src2dst_offset from the
  // origin of dst.
  if (Hint < 0)
    return true;

  if (!CHAInfo.hasCompatibleAddressPoints(DestTypeId)) {
    LLVM_DEBUG(dbgs() << "No compatible address points\n");
    NumNoAddressPoints++;
    return true;
  }

  if (!CHAInfo.isFixedInLinkTime(DestTypeId)) {
    LLVM_DEBUG(
        dbgs() << "The number of address points is not fixed in link time\n");
    NumNonFixedInLTO++;
    return true;
  }

  return false;
}

bool DynCastOPTPass::handleDynCastCallSite(CallInst *CI) {
  NumDynCast++;
  LLVM_DEBUG(dbgs() << "Start optimizing " << *CI << "\n");

  DynCastInfo Info;
  if (!getDynCastInfo(CI, &Info))
    return false;

  int64_t Hint = Info.Src2DstHint->getSExtValue();

  if (Hint == -1)
    NumNoHint++;
  else if (Hint == -2)
    NumPrivateBase++;
  else if (Hint == -3)
    NumMultiBase++;
  else if (Hint == 0)
    NumZeroHint++;
  else if (Hint > 0)
    NumPositiveHint++;

  if (isGuaranteedNotToBeOptimized(Info.DestTypeIdName, Hint, *CHAInfo))
    return false;

  if (isa<ConstantPointerNull>(Info.StaticPtr)) {
    CI->replaceAllUsesWith(Info.StaticPtr);
    NumNullPtr++;
    return true;
  }

  ConstantInt *Src2DstHint = cast<ConstantInt>(CI->getArgOperand(3));
  Function *Called = CI->getCalledFunction();
  assert(Called->hasName() && Called->getName() == "__dynamic_cast");
  (void)Called;

  auto &CompatibleAddressPoints =
      CHAInfo->getTypeIdCompatibleVTableInfo(Info.DestTypeIdName);

  std::set<AddressPoint> NecessaryAddressPoints;
  bool NegativeTest = false;
  if (EnableNegativeTest &&
      CHAInfo->hasCompatibleAddressPoints(Info.StaticTypeIdName)) {
    auto &StaticCompatibleAddressPoints =
        CHAInfo->getTypeIdCompatibleVTableInfo(Info.StaticTypeIdName);

    auto Cmp = [](const AddressPoint &Lhs, const AddressPoint &Rhs) -> bool {
      return llvm::hash_value(Lhs.VTableName) <
             llvm::hash_value(Rhs.VTableName);
    };

#ifndef NDEBUG
    std::set<AddressPoint, decltype(Cmp)> Static(
        StaticCompatibleAddressPoints.begin(),
        StaticCompatibleAddressPoints.end(), Cmp);
    std::set<AddressPoint, decltype(Cmp)> Dest(
        CompatibleAddressPoints.begin(), CompatibleAddressPoints.end(), Cmp);
    assert(std::includes(Static.begin(), Static.end(), Dest.begin(), Dest.end(),
                         Cmp) &&
           "Address points of static type does not include address points of "
           "desination type");
#endif

    std::set<AddressPoint> Difference;
    std::set_difference(StaticCompatibleAddressPoints.begin(),
                        StaticCompatibleAddressPoints.end(),
                        CompatibleAddressPoints.begin(),
                        CompatibleAddressPoints.end(),
                        std::inserter(Difference, Difference.begin()), Cmp);

    if (CompatibleAddressPoints.size() != 1 && Difference.size() == 1) {
      NegativeTest = true;
      NumNegativeTest++;
    }

    if (!CompatibleAddressPoints.empty() && Difference.empty()) {
      IRBuilder<> IRB(CI->getParent());
      IRB.SetInsertPoint(CI->getParent(),
                         CI->getParent()->getFirstInsertionPt());
      Value *Address = speculateAddress(Info.Src2DstHint, Info.StaticPtr,
                                        Info.DestTypeIdName, PTy, IRB);
      if (!Address) {
        assert(offsetToTopMustBeZero(Info.DestTypeIdName));
        Address = loadRuntimePtr(Info.StaticPtr, IRB, PTy,
                                 Info.StaticTypeIdName, true);
      }
      CI->replaceAllUsesWith(Address);
      NumZeroCheck++;
      return true;
    }
    NecessaryAddressPoints =
        NegativeTest ? Difference : CompatibleAddressPoints;
  } else {
    NecessaryAddressPoints = CompatibleAddressPoints;
  }

  if (NecessaryAddressPoints.empty()) {
    CI->replaceAllUsesWith(ConstantInt::getNullValue(PTy));
    return true;
  }

#ifndef NDEBUG
  LLVM_DEBUG(dbgs() << "  Candidates:\n");
  for (auto &AP : NecessaryAddressPoints)
    LLVM_DEBUG(dbgs() << "    " << AP.VTableName << "\n");
#endif
  switch (NecessaryAddressPoints.size()) {
  case 1:
    NumHasLeafNodes++;
    break;
  case 2:
    NumHasTwoCandidates++;
    break;
  case 3:
    NumHasThreeCandidates++;
    break;
  case 4:
    NumHasFourCandidates++;
    break;
  case 5:
    NumHasFiveCandidates++;
    break;
  case 6:
    NumHasSixCandidates++;
    break;
  case 7:
    NumHasSevenCandidates++;
    break;
  default:
    NumHasMoreThanSevenCandidates++;
    break;
  }

  if (NecessaryAddressPoints.size() > RangeCheckThreshold && !NegativeTest)
    if (doRangeTest(CI))
      return true;

  SmallVector<AddressPoint> CheckPoints;

  if (NecessaryAddressPoints.size() > MaxSuperChecks) {
    return false;
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

  for (auto &AP : NecessaryAddressPoints) {
    if (Info.Src2DstHint->getSExtValue() == 0)
      CheckPoints.push_back(AP);
    else
      CheckPoints.push_back(
          AddressPoint(AP.VTableName, getPrimaryVTableOffset()));
  }

  if (ImportSummary)
    createVTables(CheckPoints);

  // TODO: if Src2DstHint is zero, can we just check if DestTypeIdName
  // is its offset to top value must be zero.
  bool OffsetToTopMustBeZero = Src2DstHint->getSExtValue() == 0 &&
                               offsetToTopMustBeZero(Info.StaticTypeIdName);

  BasicBlock *LoadBlock =
      CI->getParent()->splitBasicBlock(CI, "load_block", /* Before */ true);

  Instruction *LBTerm = LoadBlock->getTerminator();
  assert(isa<BranchInst>(LBTerm));
  BranchInst *BrOfLB = dyn_cast<BranchInst>(LBTerm);
  assert(BrOfLB->isUnconditional());
  IRBuilder<> IRBLoadB(LoadBlock);
  IRBLoadB.SetInsertPoint(BrOfLB);

  Value *RuntimeVPtr = loadRuntimeVPtr(
      Info.Src2DstHint->getSExtValue() == 0
          ? Info.StaticPtr
          : loadRuntimePtr(Info.StaticPtr, IRBLoadB, PTy, Info.StaticTypeIdName,
                           OffsetToTopMustBeZero),
      IRBLoadB, PTy);

  BasicBlock *Landing = CI->getParent();

  BasicBlock *HandleOffset = BasicBlock::Create(
      CI->getContext(), "handle_offset", CI->getFunction(), CI->getParent());

  IRBuilder HandleOffsetIRB(HandleOffset);

  auto Result = compareAddressPoints(CheckPoints, RuntimeVPtr, HandleOffset,
                                     HandleOffset, Landing, NegativeTest);
  // Make sure jump to the first check block.
  BrOfLB->setSuccessor(0, Result.first);

  Value *AddressIfSucc = speculateAddress(
      Src2DstHint, Info.StaticPtr, Info.DestTypeIdName, PTy, HandleOffsetIRB);
  NumSpeculatableAddress++;

  HandleOffsetIRB.CreateBr(CI->getParent());

  PHINode *ResultPhi = PHINode::Create(PTy, 2, "", &*CI->getParent()->begin());
  ResultPhi->addIncoming(Constant::getNullValue(PTy), Result.second);
  ResultPhi->addIncoming(AddressIfSucc, HandleOffset);
  CI->replaceAllUsesWith(ResultPhi);
  return true;
}

Constant *DynCastOPTPass::getOffsetToTop(GlobalValue *VTable, uint64_t Offset) {
  Constant *C = ConstantFoldLoadFromConstPtr(
      VTable, PointerType::get(*Context, 0),
      APInt(64, Offset - OffsetFromOffsetToTopToAddressPoint *
                             DL->getPointerSize()),
      *DL);
  return C;
}

bool DynCastOPTPass::offsetToTopMustBeZero(StringRef Class) {
  if (!CHAInfo->hasCompatibleAddressPoints(Class))
    return false;

  auto &Result = CHAInfo->getTypeIdCompatibleVTableInfo(Class);

  for (auto &VTable : Result) {
    GlobalValue *VTableGV = M->getNamedValue(VTable.VTableName);
    if (!VTableGV)
      return false;
    Constant *OffsetToTop = getOffsetToTop(VTableGV, VTable.Offset);
    if (!OffsetToTop || !isa<ConstantPointerNull>(OffsetToTop))
      return false;
  }
  return true;
}

void DynCastOPTPass::reduceTest(StringRef VTableName) {
  GlobalVariable *VT = M->getNamedGlobal(VTableName);
  std::string TypeId = ABIManager::GetTypeIdFromVTable(VTableName);
  DenseSet<Value *> Alive;
  SmallVector<Value *> WorkList;
  for (auto &AP : CHAInfo->getTypeIdCompatibleVTableInfo(TypeId)) {
    Value *VTable = M->getNamedGlobal(AP.VTableName);
    WorkList.push_back(VTable);
  }

  GlobalVariable *Null =
      new GlobalVariable(*M, VT->getType(), true, GlobalValue::InternalLinkage,
                         ConstantPointerNull::get(VT->getType()), "null");

  while (!WorkList.empty()) {
    Value *Curr = WorkList.pop_back_val();
    Alive.insert(Curr);
    for (auto *User : Curr->users()) {
      if (!Alive.contains(User))
        WorkList.push_back(User);
    }

    if (auto *U = dyn_cast<User>(Curr))
      for (auto &Op : U->operands())
        if (!Alive.contains(Op.get()))
          WorkList.push_back(Op.get());
  }

  for (GlobalAlias &GA : make_early_inc_range(M->aliases())) {
    if (Alive.contains(&GA))
      continue;
    GA.replaceAllUsesWith(Null);
    GA.eraseFromParent();
  }

  for (Function &F : *M)
    F.dropAllReferences();

  for (Function &F : make_early_inc_range(*M)) {
    F.replaceAllUsesWith(Null);
    F.eraseFromParent();
  }

  for (GlobalVariable &GV : make_early_inc_range(M->globals())) {
    if (Alive.contains(&GV))
      continue;
    GV.replaceAllUsesWith(Null);
    GV.eraseFromParent();
  }
}

void DynCastOPTPass::recordTypeMetadata(ModuleSummaryIndex *Summary) {
  Function *DynCastDecl = M->getFunction("__dynamic_cast");
  assert(Summary && "ExportSummary must not be null");
  for (GlobalVariable &GV : M->globals()) {
    SmallVector<MDNode *, 16> Types;
    GV.getMetadata(LLVMContext::MD_type, Types);
    if (Types.empty())
      continue;

    // Promote internal linkage to weak linkage since another thinlto module
    // may referece this virtual table in DynCastOPT optimization.
    // TODO: Add test case of that!!!!!!!!!!!!
    if (GV.hasInternalLinkage() && !DynCastDecl)
      continue;

    if (GV.hasMetadata(LLVMContext::MD_virtual_inherit))
      Summary->virtualInherts.insert(GV.getName().str());

    ValueInfo VI = Summary->getValueInfo(GlobalValue::getGUID(GV.getName()));
    StringRef VTableName = GV.getName();

    for (MDNode *Type : Types) {
      auto TypeID = Type->getOperand(1).get();

      uint64_t Offset =
          cast<ConstantInt>(
              cast<ConstantAsMetadata>(Type->getOperand(0))->getValue())
              ->getZExtValue();

      if (auto *TypeId = dyn_cast<MDString>(TypeID)) {
        if (TypeId->getString().ends_with(".virtual"))
          continue;
        Summary->getOrInsertTypeIdCompatibleVtableSummary(TypeId->getString())
            .push_back({Offset,
                        Summary->getOrInsertValueInfo(
                            GlobalValue::getGUID(VTableName), VTableName)});
      }
    }
  }

}

PreservedAnalyses DynCastOPTPass::run(Module &M, ModuleAnalysisManager &MAM) {
  this->M = &M;
  std::unique_ptr<ModuleSummaryIndex> SummaryPtr =
      std::make_unique<ModuleSummaryIndex>(/*HaveGVs=*/false);
  if (!ClReadSummary.empty()) {

    ExitOnError ExitOnErr("-dyncastopt-read-summary: " + ClReadSummary + ": ");
    auto ReadSummaryFile =
        ExitOnErr(errorOrToExpected(MemoryBuffer::getFile(ClReadSummary)));
    if (Expected<std::unique_ptr<ModuleSummaryIndex>> SummaryOrErr =
            getModuleSummaryIndex(*ReadSummaryFile)) {
      SummaryPtr = std::move(*SummaryOrErr);
      ImportSummary = SummaryPtr.get();
    }
  }

  if (ExportSummary || ClSummaryAction == PassSummaryAction::Export) {
    ModuleSummaryIndex *Summary = ClSummaryAction == PassSummaryAction::Export ?
      SummaryPtr.get() : ExportSummary;
    recordTypeMetadata(Summary);
    if (!ClWriteSummary.empty()) {
      ExitOnError ExitOnErr(
        "-wholeprogramdevirt-write-summary: " + ClWriteSummary + ": ");
      std::error_code EC;
      if (StringRef(ClWriteSummary).ends_with(".bc")) {
        raw_fd_ostream OS(ClWriteSummary, EC, sys::fs::OF_None);
        ExitOnErr(errorCodeToError(EC));
        writeIndexToFile(*Summary, OS);
      } else {
        raw_fd_ostream OS(ClWriteSummary, EC, sys::fs::OF_TextWithCRLF);
        ExitOnErr(errorCodeToError(EC));
        yaml::Output Out(OS);
        Out << *Summary;
      }
    }
  }

  CHAInfo = &MAM.getResult<ClassHierarchyAnalysis>(M);
  CHAInfo->init(ImportSummary ? ImportSummary : nullptr);

  //analyzeTypeHierarchy();

  for (auto &Type : CHAInfo->getTypeIdMap()) {
    NumClasses++;
    if (Type.second.size() == 1) {
      NumFinal++;
    }
  }

  Context = &M.getContext();
  Int64Ty = Type::getInt64Ty(*Context);
  Int32Ty = Type::getInt32Ty(*Context);
  Int8Ty = Type::getInt8Ty(*Context);
  Int1Ty = Type::getInt1Ty(*Context);
  VoidTy = Type::getVoidTy(*Context);
  PTy = PointerType::getUnqual(*Context);
  DL = &M.getDataLayout();
  SmallVector<CallInst *> Deleted;

  if (!ExtractType.empty()) {
    reduceTest(ExtractType);
    return PreservedAnalyses::all();
  }

  if ((UseCommandLine || ExportSummary) && EnableRangeCheck)
    layoutVTables();

  GlobalValue *DyncastDecl = M.getNamedValue("__dynamic_cast");
  if (!DyncastDecl)
    return PreservedAnalyses::all();
  bool Changed = false;

  std::set<std::string> ValidDestTypes;
  bool HasDestTypeFile = readVTableFile(ValidDestTypes);
  std::optional<std::unique_ptr<raw_fd_ostream>> OutputRTTIFile =
      outputDestTypeFile();

  DenseSet<Value *> DestTypes;
  for (auto User : make_early_inc_range(DyncastDecl->users())) {
    if (CallInst *C = dyn_cast<CallInst>(User)) {
      if (HasDestTypeFile &&
          !ValidDestTypes.count(C->getOperand(2)->getName().str()))
        continue;
      if (handleDynCastCallSite(C)) {
        if (OutputRTTIFile && DestTypes.insert(C->getOperand(2)).second) {
          **OutputRTTIFile << C->getOperand(2)->getName() << "\n";
        }

        NumOptDynCast++;
        C->eraseFromParent();
        Changed = true;
      }
    }
  }

  if (Changed)
    return PreservedAnalyses::none();

  return PreservedAnalyses::all();
}

void DynCastOPTPass::analyzeTypeHierarchy() {
  for (Function &F : M->functions()) {
    for (BasicBlock &BB : F)
      for (Instruction &II : BB)
        NumTotalInstr++;
  }

  DenseSet<GlobalVariable *> TypeUsedByDyncast;

  Function *DynCastFunc = M->getFunction("__dynamic_cast");
  if (DynCastFunc) {
    for (auto *U : DynCastFunc->users()) {
      if (auto *CB = dyn_cast<CallBase>(U)) {
        GlobalVariable *StaticType = dyn_cast<GlobalVariable>(CB->getArgOperand(1));
        GlobalVariable *DestType = dyn_cast<GlobalVariable>(CB->getArgOperand(2));
        TypeUsedByDyncast.insert(StaticType);
        TypeUsedByDyncast.insert(DestType);
      }
    }
  }

  auto &TypeIdMap = CHAInfo->getTypeIdMap();
  SetVector<GlobalVariable *> Roots;

  for (auto Iter : TypeIdMap) {
    GlobalVariable *TypeInfo = M->getNamedGlobal(CXXABIManager<Itanium>::GetTypeInfoFromTypeId(Iter.first));
    if (!TypeInfo || !TypeInfo->hasInitializer())
      continue;

    NumTotalClasses++;

    if (TypeInfo->hasMetadata(LLVMContext::MD_multiinhert))
      NumMulti++;

    if (TypeInfo->hasMetadata(LLVMContext::MD_virtual_inherit))
      NumVirtual++;

    if (TypeInfo->hasMetadata(LLVMContext::MD_public))
      NumPublic++;

    if (TypeInfo->hasMetadata(LLVMContext::MD_private))
      NumPrivate++;

    if (TypeInfo->hasMetadata(LLVMContext::MD_final))
      NumFinalKeyword++;

    if (TypeInfo->hasMetadata(LLVMContext::MD_multidynamic))
      NumMultiDynamic++;

    Constant *Initializer = TypeInfo->getInitializer();
    Constant *AddressPoint = ConstantFoldLoadFromConst(Initializer, TypeInfo->getType(), M->getDataLayout());
    GlobalVariable *RTTIVTable = cast<GlobalVariable>(getUnderlyingObject(AddressPoint));

    if (RTTIVTable->getName() == "_ZTVN10__cxxabiv117__class_type_infoE") {
      Roots.insert(TypeInfo);
    }
  }

  NumGraph = Roots.size();

  DenseSet<GlobalVariable *> Visited;


  for (auto *Root : Roots) {
    if (!Visited.contains(Root))
      NumDAG++;

    DenseSet<GlobalVariable *> Nodes;
    SmallVector<GlobalVariable *, 10> Worklist = { Root };

    while (!Worklist.empty()) {
      GlobalVariable *Curr = Worklist.pop_back_val();
      if (!Curr)
        continue;
      if (Roots.contains(Curr))
        Visited.insert(Curr);
      for (auto *U : Curr->users()) {
        auto *SubclassInit = dyn_cast<Constant>(U);
        if (!SubclassInit)
          continue;

        for (auto *Parent : SubclassInit->users()) {
          auto *SubClass = dyn_cast<GlobalVariable>(Parent);
          if (!SubClass)
            continue;

          if (!Nodes.contains(SubClass)) {
            Nodes.insert(SubClass);
            Worklist.push_back(SubClass);
          }
        }
      }

      Constant *Initializer = Curr->getInitializer();
      if (!Initializer)
        continue;
      for (auto &Op : Initializer->operands()) {
        StringRef Name = Op.get()->getName();
        if (!Name.starts_with(CXXABIManager<Itanium>::GetTypeInfoPrefix()))
          continue;
        GlobalVariable *GV = M->getGlobalVariable(Name);
        if (!Nodes.contains(GV)) {
          Nodes.insert(GV);
          Worklist.push_back(GV);
        }
      }

    }
  }


  for (auto *Root : Roots) {
    bool IsUsedByDynCast = false;
    bool IsUsedByExceptionHandling = false;
    bool IsNonClosed = false;
    SmallVector<GlobalVariable *, 100> WorkList = { Root };
    DenseMap<GlobalVariable *, SmallVector<GlobalVariable *, 2>> Hierarchy;
    while (!WorkList.empty()) {
      auto *Curr = WorkList.pop_back_val();
      if (!Curr->hasInternalLinkage())
        IsNonClosed = true;

      if (TypeUsedByDyncast.contains(Curr))
        IsUsedByDynCast = true;

      for (auto *U : Curr->users()) {
        if (auto *CB = dyn_cast<CallBase>(U)) {
          if (!CB->getCalledFunction() || (CB->getCalledFunction()->getName() != "__dynamic_cast"))
            IsUsedByExceptionHandling = true;
        }
      }

      Hierarchy[Curr];
      for (auto *U : Curr->users()) {
        auto *SubclassInit = dyn_cast<Constant>(U);
        if (!SubclassInit)
          continue;

        for (auto *Parent : SubclassInit->users()) {
          auto *SubClass = dyn_cast<GlobalVariable>(Parent);
          if (!SubClass)
            continue;

          Hierarchy[Curr].push_back(SubClass);
          WorkList.push_back(SubClass);
        }
      }
    }
    if (IsUsedByDynCast)
      NumUsedByDyncast++;

    if (IsNonClosed)
      NumNonClosed++;

    if (IsUsedByExceptionHandling)
      NumUsedByEH++;

    bool IsTree = true;
    // Check if it is a tree
    for (auto &Node : Hierarchy) {
      GlobalVariable *RTTI = Node.first;
      Constant *Initializer = RTTI->getInitializer();
      for (auto &Op : Initializer->operands()) {
        StringRef Name = Op.get()->getName();
        if (!Name.starts_with(CXXABIManager<Itanium>::GetTypeInfoPrefix()))
          continue;
        GlobalVariable *GV = M->getGlobalVariable(Name);
        if (!Hierarchy.contains(GV)) {
          IsTree = false;
        }
      }
    }

    if (!IsTree)
      NumNonTree++;


    // Compute width
    {
      SmallVector<GlobalVariable *, 100> CurrentLayer = { Root };
      SmallVector<GlobalVariable *, 100> NextLayer;
      unsigned MaxWidth = 1;

      while (!CurrentLayer.empty()) {
        NextLayer.clear();
        for (auto *Node : CurrentLayer) {
          for (auto *N : Hierarchy[Node])
            NextLayer.push_back(N);
        }

        if (NextLayer.size() > MaxWidth)
          MaxWidth = NextLayer.size();

        CurrentLayer = NextLayer;
      }

      #define WidthCase(H) case H: NumWidth##H+=1; break;
      switch (MaxWidth) {
        WidthCase(1);
        WidthCase(2);
        WidthCase(3);
        WidthCase(4);
        WidthCase(5);
        WidthCase(6);
        WidthCase(7);
        WidthCase(8);
        WidthCase(9);
        WidthCase(10);
        WidthCase(11);
        WidthCase(12);
        WidthCase(13);
        WidthCase(14);
        WidthCase(15);
        WidthCase(16);
        WidthCase(17);
        WidthCase(18);
        WidthCase(19);
        WidthCase(20);
        WidthCase(21);
        WidthCase(22);
        WidthCase(23);
        WidthCase(24);
        WidthCase(25);
        WidthCase(26);
        WidthCase(27);
        WidthCase(28);
        WidthCase(29);
        WidthCase(30);
        WidthCase(31);
        WidthCase(32);
        WidthCase(33);
        WidthCase(34);
        WidthCase(35);
        WidthCase(36);
        WidthCase(37);
        WidthCase(38);
        WidthCase(39);
        WidthCase(40);
        WidthCase(41);
        WidthCase(42);
        WidthCase(43);
        WidthCase(44);
        WidthCase(45);
        WidthCase(46);
        WidthCase(47);
        WidthCase(48);
        WidthCase(49);
        WidthCase(50);
        default: NumWidthMoreThan50++; break;
      }

      if (MaxWidth > NumMaxWidth)
        NumMaxWidth = MaxWidth;

      NumWidthSum += MaxWidth;

      if (MaxWidth < 5)
        NumWidthRange1++;
      else if (MaxWidth < 10)
        NumWidthRange2++;
      else if (MaxWidth < 20)
        NumWidthRange3++;
      else if (MaxWidth < 30)
        NumWidthRange4++;
      else
        NumWidthRange5++;
    }

    // Compute height
    std::function<unsigned(GlobalVariable *)> getHeight;
    getHeight = [&](GlobalVariable *Node) -> unsigned {
      if (Hierarchy[Node].empty())
        return 1;

      unsigned MaxHeight = 0;
      for (auto *Base : Hierarchy[Node]) {
        unsigned BaseHeight = getHeight(Base);
        if (BaseHeight > MaxHeight)
          MaxHeight = BaseHeight;
      }
      return MaxHeight + 1;
    };

    unsigned Height = getHeight(Root);
    if (Height == 1) {
      std::error_code EC;
      std::unique_ptr<llvm::raw_fd_ostream> File = std::make_unique<llvm::raw_fd_ostream>(
      "/tmp/height.txt", EC, llvm::sys::fs::OpenFlags::OF_Append);

      *File << Root->getName() << "\n";
    }

    #define HeightCase(H) case H: NumHeight##H+=1; break;
    switch (Height) {
      HeightCase(1);
      HeightCase(2);
      HeightCase(3);
      HeightCase(4);
      HeightCase(5);
      HeightCase(6);
      HeightCase(7);
      HeightCase(8);
      HeightCase(9);
      HeightCase(10);
      default: NumHeightMoreThan10++; break;

    }

    NumHeightSum += Height;

    // Compute # of node in hierarchy
    unsigned NumNodes = Hierarchy.size();

    #define HeightMacro(H) NumNode##H++;

    #define NodesCase(H) case H: HeightMacro(H) break;

    switch (NumNodes) {
      NodesCase(1);
      NodesCase(2);
      NodesCase(3);
      NodesCase(4);
      NodesCase(5);
      NodesCase(6);
      NodesCase(7);
      NodesCase(8);
      NodesCase(9);
      NodesCase(10);
      NodesCase(11);
      NodesCase(12);
      NodesCase(13);
      NodesCase(14);
      NodesCase(15);
      NodesCase(16);
      NodesCase(17);
      NodesCase(18);
      NodesCase(20);
      NodesCase(21);
      NodesCase(22);
      NodesCase(23);
      NodesCase(24);
      NodesCase(25);
      NodesCase(26);
      NodesCase(27);
      NodesCase(28);
      NodesCase(29);
      NodesCase(30);
      NodesCase(31);
      NodesCase(32);
      NodesCase(33);
      NodesCase(34);
      NodesCase(35);
      NodesCase(36);
      NodesCase(37);
      NodesCase(38);
      NodesCase(39);
      NodesCase(40);
      NodesCase(41);
      NodesCase(42);
      NodesCase(43);
      NodesCase(44);
      NodesCase(45);
      NodesCase(46);
      NodesCase(47);
      NodesCase(48);
      NodesCase(49);
      NodesCase(50);
      default: NumNodeMoreThan50++; break;
    }

    if (NumNodes < 5)
      NumNodeRange1++;
    else if (NumNodes < 10)
      NumNodeRange2++;
    else if (NumNodes < 20)
      NumNodeRange3++;
    else if (NumNodes < 30)
      NumNodeRange4++;
    else
      NumNodeRange5++;

  }

}
