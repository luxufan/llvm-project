#include "llvm/Transforms/IPO/RTTIClean.h"
#include "llvm/Transforms/IPO/DynCastOPT.h"
#include "llvm/ADT/Statistic.h"
#include "llvm/Analysis/ClassHierarchyAnalysis.h"
#include "llvm/Analysis/ModuleSummaryAnalysis.h"
#include "llvm/Analysis/TypeMetadataUtils.h"
#include "llvm/Demangle/Demangle.h"
#include "llvm/IR/Constants.h"
#include "llvm/IR/Instructions.h"
#include "llvm/IR/Intrinsics.h"
#include "llvm/IR/Operator.h"
#include "llvm/IR/ValueHandle.h"
#include "llvm/IR/ModuleSummaryIndex.h"
#include "llvm/Bitcode/BitcodeReader.h"
#include "llvm/Bitcode/BitcodeWriter.h"
#include "llvm/Support/FileSystem.h"
#include "llvm/Support/MemoryBuffer.h"
#include "llvm/Support/Error.h"
#include "llvm/Support/YAMLTraits.h"
#include "llvm/IR/ModuleSummaryIndexYAML.h"

#include <set>

#define DEBUG_TYPE "rtti-clean"

STATISTIC(NumThin, "");
STATISTIC(NumOpenAP, "");
STATISTIC(NumMergedAp, "");
STATISTIC(NumZTV, "");
STATISTIC(NumTotalAP, "");
STATISTIC(NumMergedAP, "");
STATISTIC(NumAliveAPs, "");
STATISTIC(NumLocalVTable, "");
STATISTIC(NumWeakVtable, "");
STATISTIC(NumDeadAPs, "");
STATISTIC(NumDeadVTables, "");
STATISTIC(NumRemovedVTableSlot, "Number of optimized dynamic_cast call site");
STATISTIC(NumSlotsAccess, "Number of optimized dynamic_cast call site");
STATISTIC(NumDynCastAccess, "Number of optimized dynamic_cast call site");
STATISTIC(NumRemovedRTTIs, "");
STATISTIC(NumRemovedRTTIBytes, "");
STATISTIC(NumUsedByTypeId, "");
STATISTIC(NumSameVTables, "");
STATISTIC(NumVTableBytes, "");
STATISTIC(NumUsedByTypeIdOp, "");
STATISTIC(NumOffsetToTop, "");

using namespace llvm;

static cl::opt<std::string> ValidVTablesFile("vtable-file",
                                             cl::desc("vtable file"));

static cl::opt<bool> OutputVTables("output-vtables", cl::init(false),
                                   cl::Hidden, cl::desc("output vtables"));

static cl::opt<std::string> ClReadImportSummary(
    "rtti-clean-read-import-summary",
    cl::desc(
        "Read summary from given bitcode or YAML file before running pass"),
    cl::Hidden);

static cl::opt<std::string> ClReadExportSummary(
    "rtti-clean-read-export-summary",
    cl::desc(
        "Read summary from given bitcode or YAML file before running pass"),
    cl::Hidden);

static cl::opt<std::string> ClWriteSummary(
    "rtti-clean-write-summary",
    cl::desc("Write summary to given bitcode or YAML file after running pass. "
             "Output file format is deduced from extension: *.bc means writing "
             "bitcode, otherwise YAML"),
    cl::Hidden);
// FIXME: This class assumes the following things:
// 1. The first two pointers in vtable are Offset-To-Top and RTTI pointer.
// 2. The type of virtual table group is StructType
// 3. The type of virtual table is ArrayType
class VTableUpdater {

  GlobalVariable *OldVTableGroup;
  const DataLayout *DL;
  Module *M;
  LLVMContext *Context;
  ModuleSummaryIndex *ExportSummary;

  using ABIManager = CXXABIManager<Itanium>;
  template <class T>
  void createIndicesValues(LLVMContext &Context,
                           const SmallVectorImpl<APInt> &Indices,
                           SmallVectorImpl<T *> &NewOperands);

public:
  VTableUpdater(GlobalVariable *GV, const DataLayout *DL, ModuleSummaryIndex *ExportSummary, ClassHierarchyInfo *CHAInfo)
      : OldVTableGroup(GV), DL(DL), ExportSummary(ExportSummary) {
    assert(GV->getName().starts_with(ABIManager::GetVTablePrefix()) &&
           "Not a virtual table");
    M = GV->getParent();
    Context = &M->getContext();
  }

  /// Get GEP indices to access Offset inside the type of the old virtual table
  /// group variable.
  SmallVector<APInt> getGEPIndicesForOffset(Type *VTableGroupTy,
                                            uint64_t Offset) {
    APInt APOffset(64, Offset);
    SmallVector<APInt> Indices =
        DL->getGEPIndicesForOffset(VTableGroupTy, APOffset);
    assert(Indices.back().getZExtValue() >= 2 && "Last index less than 2");
    return Indices;
  }

  // Get the virtual table where address point AP points to.
  ConstantArray *getVTable(GlobalVariable *VTableGroup, uint64_t Offset) {
    SmallVector<APInt> Indices =
        getGEPIndicesForOffset(VTableGroup->getValueType(), Offset);
    Constant *C = VTableGroup->getInitializer();
    for (const APInt &Index : drop_end(drop_begin(Indices))) {
      assert(!Index.isNegative() && "Negative index is invalid");
      assert(Index.getActiveBits() < 32 && "Invalid indexes");
      C = C->getAggregateElement(Index.getZExtValue());
    }
    ConstantArray *VTable = cast<ConstantArray>(C);
    return VTable;
  }

  SmallVector<Constant *> getVTableComponents(ConstantArray *VTable,
                                              bool DropOTTAndRTTIOp = false) {
    SmallVector<Constant *> Components;
    unsigned I = DropOTTAndRTTIOp ? 2 : 0;
    for (; I < VTable->getNumOperands(); I++)
      Components.push_back(VTable->getOperand(I));
    assert(!Components.empty() && "Virtual table has no components");
    return Components;
  }

  /// Create a virtual table group global variable for virtual tables
  /// in VTables
  GlobalVariable *createVTableGroup(ArrayRef<Constant *> VTables) {
    SmallVector<Type *> VTableTypes;
    for (unsigned I = 0; I < VTables.size(); I++)
      VTableTypes.push_back(VTables[I]->getType());

    StructType *VTableGroupTy =
        StructType::get(OldVTableGroup->getContext(), VTableTypes);

    Constant *VTableGroupInitializer =
        cast<ConstantStruct>(ConstantStruct::get(VTableGroupTy, VTables));
    return new GlobalVariable(*M, VTableGroupTy, true,
                              OldVTableGroup->getLinkage(),
                              VTableGroupInitializer, "", OldVTableGroup);
  }

  void cleanAddressPoints(ArrayRef<AddressPoint> AddressPoints, bool UseCommandLine) {
    LLVM_DEBUG(dbgs() << "Start clean virtual table group "
                      << OldVTableGroup->getName() << "\n");

    if (!UseCommandLine && ExportSummary) {
      auto VI = ExportSummary->getValueInfo(GlobalValue::getGUID(OldVTableGroup->getName()));
      if (!VI)
        return;
    }

    // Maps from old vtable to new vtable
    DenseMap<ConstantArray *, ConstantArray *> VTablesMap;

    for (auto AP : AddressPoints) {
      ConstantArray *OldVTable = getVTable(OldVTableGroup, AP.Offset);
      SmallVector<Constant *, 4> Components =
          getVTableComponents(OldVTable, true);
      ArrayType *NewVTableTy = ArrayType::get(
          OldVTable->getType()->getElementType(), Components.size());
      ConstantArray *NewVTable =
          cast<ConstantArray>(ConstantArray::get(NewVTableTy, Components));
      NumRemovedVTableSlot += 2;
      VTablesMap[OldVTable] = NewVTable;
    }

    SmallVector<Constant *, 2> NewVTables;
    Constant *OldInitializer = OldVTableGroup->getInitializer();
    for (unsigned I = 0; I < OldInitializer->getNumOperands(); I++) {
      ConstantArray *OldVTable =
          cast<ConstantArray>(OldInitializer->getOperand(I));
      ConstantArray *NewVTable = OldVTable;
      if (VTablesMap.contains(OldVTable))
        NewVTable = VTablesMap[OldVTable];
      NewVTables.push_back(NewVTable);
    }

    GlobalVariable *NewVTableGroup = createVTableGroup(NewVTables);
    StringRef VTableName = OldVTableGroup->getName();

    if (ExportSummary) {
      auto VI = ExportSummary->getValueInfo(OldVTableGroup->getGUID());
      assert(VI || UseCommandLine && "The summary index of vtable does not exist");

      if (VI) {
        SetVector<ValueInfo, std::vector<ValueInfo>> RefEdges;
        findRefEdges(*ExportSummary, NewVTableGroup, RefEdges, /*PerModule*/false);
        for (auto &S : VI.getSummaryList())
          S->updateRefs(RefEdges.takeVector());
      }

      for (auto &AP : AddressPoints) {
        SmallVector<APInt> Indices = getGEPIndicesForOffset(OldVTableGroup->getValueType(), AP.Offset);

        // FIXME: this is so error-prone.
        Indices[2] -= 2;

        SmallVector<Value *> IndicesOps;
        createIndicesValues(OldVTableGroup->getContext(), Indices, IndicesOps);
        APInt Offset(64, 0);
        GEPOperator::accumulateConstantOffset(NewVTableGroup->getValueType(), IndicesOps, *DL, Offset);
        LLVM_DEBUG(dbgs() << "Summary " << *NewVTableGroup << "\n");
        LLVM_DEBUG(dbgs() << "    Old offset: " << AP.Offset << ", new offset: " << Offset.getZExtValue());
        ExportSummary->addOffsetAdjusts(GlobalValue::getGUID(VTableName), AP.Offset, Offset.getZExtValue());
      }
    }

    SmallVector<TrackingVH<Constant>, 8> Consts;
    SmallPtrSet<Constant *, 8> Visited;
    for (auto &Use : make_early_inc_range(OldVTableGroup->uses())) {
      LLVM_DEBUG(dbgs() << "Replacing uses of " << *Use.getUser() << "\n");
      User *U = Use.getUser();

      unsigned IndexBits;
      if (U->getType()->isPtrOrPtrVectorTy())
        IndexBits = DL->getIndexTypeSizeInBits(U->getType());
      else
        IndexBits = 64;
      APInt Offset(IndexBits, 0);
      Value *UnderlyingObject = U->stripAndAccumulateConstantOffsets(
          *DL, Offset, /* AllowNonInbound */ false);

      if (!isa<GEPOperator>(U) || Offset.isZero()) {
        // TODO: Explore why set + handleOperandChange triggers the assert.
        // Use.set(NewVTableGroup);
        // TrackingVH<ConstantArray>(C)->handleOperandChange(OldVTableGroup,
        // NewVTableGroup);
        if (auto *C = dyn_cast<Constant>(U))
          if (!isa<GlobalValue>(C)) {
            if (Visited.insert(C).second)
              Consts.push_back(C);
            continue;
          }
        Use.set(NewVTableGroup);
        continue;
      }

      auto *GEPOfOldVTableGroup = cast<GEPOperator>(U);
      if (UnderlyingObject == NewVTableGroup)
        continue;
      assert(UnderlyingObject == OldVTableGroup &&
             "UnderlyingObject is not the old virtual table group");

      ConstantArray *VTable = getVTable(OldVTableGroup, Offset.getZExtValue());
      SmallVector<APInt> Indices = getGEPIndicesForOffset(
          OldVTableGroup->getValueType(), Offset.getZExtValue());
      assert(Indices.size() == 3 &&
             "Unknown gep index of virtual table component");
      assert(Indices.back().getZExtValue() >= 2 &&
             "the last index is less than 2");
      // FIXME: this is a hack to set the last index value to 0
      if (VTablesMap.contains(VTable)) {
        // The offset to top and RTTI pointer have been removed, so set the
        // index from 2 to 0.
        Indices[2] -= 2;
      }

      SmallVector<Constant *> IndicesOps;
      createIndicesValues(OldVTableGroup->getContext(), Indices, IndicesOps);

      Constant *GEPOfNewVTable = ConstantExpr::getInBoundsGetElementPtr(
          NewVTableGroup->getValueType(), NewVTableGroup, IndicesOps);
      GEPOfOldVTableGroup->replaceAllUsesWith(GEPOfNewVTable);
      cast<Constant>(GEPOfOldVTableGroup)->destroyConstant();
    }

    // The reason why calling handleOperandChange out of make_early_inc_range
    // the loop is because handleOperandChange updates all the uses in a given
    // Constant. Similar to what replaceUsesWithIf do.
    while (!Consts.empty())
      Consts.pop_back_val()->handleOperandChange(OldVTableGroup,
                                                 NewVTableGroup);

    // Adjust the type id offset
    SmallVector<MDNode *> Types;
    OldVTableGroup->getMetadata(LLVMContext::MD_type, Types);
    for (MDNode *Type : Types) {
      auto TypeId = Type->getOperand(1).get();
      ConstantInt *Offset = cast<ConstantInt>(
          cast<ConstantAsMetadata>(Type->getOperand(0))->getValue());
      ConstantArray *VTable = getVTable(OldVTableGroup, Offset->getZExtValue());
      SmallVector<APInt> Indices = getGEPIndicesForOffset(
          OldVTableGroup->getValueType(), Offset->getZExtValue());

      if (VTablesMap.contains(VTable)) {
        Indices[2] -= 2;
      }

      SmallVector<Value *> NewIndices;

      createIndicesValues(OldVTableGroup->getContext(), Indices, NewIndices);
      APInt NewOffset(64, 0);
      bool Success = GEPOperator::accumulateConstantOffset(
          NewVTableGroup->getValueType(), NewIndices, *DL, NewOffset);
      if (!Success)
        llvm_unreachable("Update type metadata fails");

      NewVTableGroup->addTypeMetadata(NewOffset.getZExtValue(), TypeId);
    }

    // Update vcall_visibility metadata
    if (MDNode *VCallVisibility =
            OldVTableGroup->getMetadata(LLVMContext::MD_vcall_visibility)) {
      NewVTableGroup->addMetadata(LLVMContext::MD_vcall_visibility,
                                  *VCallVisibility);
    }
    NewVTableGroup->takeName(OldVTableGroup);
    OldVTableGroup->eraseFromParent();
  }
};

static bool readVTableFile(std::set<std::string> &VTableSet) {
  if (ValidVTablesFile.empty())
    return false;

  ErrorOr<std::unique_ptr<MemoryBuffer>> Text =
      MemoryBuffer::getFileAsStream(ValidVTablesFile);
  if (std::error_code EC = Text.getError()) {
    llvm::errs() << "Can't read " << ValidVTablesFile << " " << EC.message()
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

static std::optional<std::unique_ptr<raw_fd_ostream>> outputVTableFiles() {
  if (OutputVTables) {
    std::error_code EC;

    std::unique_ptr<raw_fd_ostream> OutputVTables =
        std::make_unique<raw_fd_ostream>("vtable-output.txt", EC,
                                         sys::fs::OpenFlags::OF_None);
    if (EC)
      llvm_unreachable("Can not create vtable-output.txt file");
    return OutputVTables;
  }
  return std::nullopt;
}

static ModuleSummaryIndex *readSummary(const Twine &SummaryFile) {
  ExitOnError ExitOnErr("read-summary: " + SummaryFile.getSingleStringRef().str() +
                            ": ");
  auto SummaryBuffer = ExitOnErr(errorOrToExpected(MemoryBuffer::getFile(SummaryFile)));
  ModuleSummaryIndex *SummaryIndex;
  if (Expected<std::unique_ptr<ModuleSummaryIndex>> SummaryOrErr = getModuleSummaryIndex(*SummaryBuffer)) {
    SummaryIndex = SummaryOrErr->release();
    // TODO: validating the correctness of summary
  } else {
    consumeError(SummaryOrErr.takeError());
    yaml::Input In(SummaryBuffer->getBuffer());
    SummaryIndex = new ModuleSummaryIndex(/*HaveGVs*/false);
    In >> *SummaryIndex;
    ExitOnErr(errorCodeToError(In.error()));
  }

  return SummaryIndex;
}

PreservedAnalyses RTTICleanPass::run(Module &M, ModuleAnalysisManager &MAM) {
  // If only some of the modules were split, we cannot correctly perform
  // this transformation. We already checked for the presense of type tests
  // with partially split modules during the thin link, and would have emitted
  // an error if any were found, so here we can simply return.

  this->M = &M;
  DL = &M.getDataLayout();
  FAM = &MAM.getResult<FunctionAnalysisManagerModuleProxy>(M).getManager();

  if (UseCommandLine) {
    if (!ClReadImportSummary.empty())
      ImportSummary = readSummary(ClReadImportSummary);

    if (!ClReadExportSummary.empty())
      ExportSummary = readSummary(ClReadExportSummary);

    if (!ClWriteSummary.empty() && !ExportSummary)
      ExportSummary = new ModuleSummaryIndex(/*HaveGVs*/false);

 }

  if (ImportSummary) {
    adjustOffsets();
    if (UseCommandLine)
      delete ImportSummary;
    return PreservedAnalyses::all();
  }


  this->CHAInfo = &MAM.getResult<ClassHierarchyAnalysis>(M);
  CHAInfo->init(ImportSummary ? ImportSummary : nullptr);

  std::set<AddressPoint> DeadAddressPoints;
  for (auto It : CHAInfo->getTypeIdMap()) {
    if (!CHAInfo->isFixedInLinkTime(It.first)) {
      NumOpenAP += It.second.size();
      continue;
    }

    if (It.first.ends_with(".merged")) {
      NumMergedAP += It.second.size();
      continue;
    }

    for (auto &AP : It.second)
      DeadAddressPoints.insert(AP);
  }

  if (ExportSummary && (ExportSummary->Thin || UseCommandLine)) {
    checkSummary(DeadAddressPoints);
    NumThin = 1;
  } else {
    checkTypeTest(DeadAddressPoints);
    checkDynamicCast(DeadAddressPoints);
  }

  for (auto &AP : DeadAddressPoints) {
    GlobalVariable *VTable = M.getNamedGlobal(AP.VTableName);
    assert(VTable && "Don't have this virtual table");
    VTable2APs[VTable].push_back(AP);
    NumDeadAPs += 1;
  }

  NumDeadVTables = VTable2APs.size();

  std::set<std::string> ValidVTables;
  bool HasVTableFile = readVTableFile(ValidVTables);

  std::optional<std::unique_ptr<raw_fd_ostream>> OutputVTableFile =
      outputVTableFiles();

  for (auto It : VTable2APs) {

    if (It.getFirst()->isWeakForLinker()) {
      // If the definition of vtable may be redefined in link time,
      // it is illegal to optimize it.
      if (!ExportSummary)
        continue;

      // The reason why here we check GlobalVariable::getVCallVisibility instead of
      // GlobalVarSummary::getVCallVisibility is in this code point, we are performing
      // Full regular LTO or the regular LTO stage for the split module,
      // updateVCallVisibilityInIndexboth in LTO.cpp has not been called before both of
      // these two cases.
      if (It.getFirst()->getVCallVisibility() == GlobalValue::VCallVisibilityPublic)
        continue;

    }

    if (HasVTableFile && !ValidVTables.count(It.getFirst()->getName().str()))
      continue;

    if (OutputVTableFile)
      **OutputVTableFile << It.getFirst()->getName() << "\n";

    if (It.getFirst()->getName().ends_with(".merged")) {
      // FIXME: .merged vtable is emitted by DynCastOPT pass,
      // In theory, we can make merged vtable anonymous.
      NumAliveAPs += It.getSecond().size();
      continue;
    }

    if (It.getFirst()->getName().contains('.')) {
      // These symbols are created when there are more than one tranlation unit local
      // symbols with same name. To avoid the name confilict in the combined module (LTO)
      // or split module (ThinLTO), the compiler add module id for these symbols. And the
      // numbers following '.' are the module id of its original module. Currently the pass
      // can not deal with these symbols correctly.
      // In this pass, we assume vtable name, type info name and tyep id name are same except
      // their prefix, "_ZTV", "_ZTI" and "_ZTS". However, for symbols with '.', this
      // assumption is broken.
      // TODO: add corresponding test cases.
      continue;
    }

    NumTotalAP += It.getSecond().size();

    VTableUpdater Parser(It.getFirst(), DL, ExportSummary, CHAInfo);
    Parser.cleanAddressPoints(It.getSecond(), UseCommandLine);
  }

  if (!ClWriteSummary.empty()) {
    ExitOnError ExitOnErr("-rtti-clean-write-summary: " + ClWriteSummary + ": ");
    std::error_code EC;
    if (StringRef(ClWriteSummary).ends_with(".bc")) {
      raw_fd_ostream OS(ClWriteSummary, EC, sys::fs::OF_None);
      ExitOnErr(errorCodeToError(EC));
      writeIndexToFile(*ExportSummary, OS);
    } else {
      raw_fd_ostream OS(ClWriteSummary, EC, sys::fs::OF_TextWithCRLF);
      ExitOnErr(errorCodeToError(EC));
      yaml::Output Out(OS);
      Out << *ExportSummary;
    }
  }

  if (ExportSummary) {
    // Here we iterate all of the global value symbols in the module.
    // In general, the split module in ThinLTO only contains the vtable
    // and functions that can be performed the virtual constant propagation.
    // And these functions are marked with AvailableExternally which means
    // these symbols would be dropped by EliminateAvailableExternallyPass.
    // However, there are exceptions. splitAndWriteThinLTOBitcode is the function
    // that splits the module into split module and a regualr ThinLTO module.
    // However, if the getUniqueModuleId returns an empty string, the split fails.
    // And these non-split modules would be merged into the split module in link stage.
    // Therefore, we should iterate all of the global value in the split module.
    for (auto &GV : M.global_values()) {
      if (GV.isDeclaration())
        continue;

      if (!GV.hasName()) {
        // GV.getGUID in the below assume the string is not empty.
        continue;
      }

      // Here we use GlobalValue::getGUID instead of GV.getGUID. That's because
      // GV.getGUID would return a different GUID if a symbol has internal linkage.
      // For example, the GUID of the non-internalized symbol before linking may be
      // different with the GUID of symbols that are internalized after symbol resolution.
      // That makes getValueInfo can not find the correct value summary.
      auto VI = ExportSummary->getValueInfo(GlobalValue::getGUID(GV.getName()));
      if (VI) {
        // With ExportSummary, there are two states: LTO or ThinLTO for SplitModule.
        // There is no impact on LTO since it does not use the live flags
        // to do dead code elimination.
        // However, in ThinLTO, LTO pipeline would be firstly applied in SplitModule,
        // and this module would not be run in ThinLTO pipeline. And since symbols
        // that are internalized in LTO unit but not internalized in current module
        // have non-internal linage, LTO pipeline's DCE does't not eliminate these
        // LTO internalized symbols which means these symbols are equvalent with
        // the GUIDPreserved symbol in LTO.cpp. Therefore, here we mark these symbols
        // as live.
        for (auto &GVS : VI.getSummaryList()) {
          GVS->setLive(true);
          ExportSummary->MayLive.erase(GVS.get());
        }
      }
    }
  }

  if (UseCommandLine && ExportSummary)
    delete ExportSummary;


  return PreservedAnalyses::all();
}

void RTTICleanPass::analysisSameVTables() {
  if (ImportSummary)
    return;

  for (auto &GV : M->globals()) {
    if (!GV.hasInitializer())
      continue;

    if (!GV.hasName() || !GV.getName().starts_with("_ZTV"))
      continue;

    Constant *Initializer = GV.getInitializer();
    bool Succ = Initializers.insert(Initializer).second;
    if (!Succ)
      NumSameVTables++;

    uint64_t Size = M->getDataLayout().getTypeAllocSize(Initializer->getType());
    NumVTableBytes += Size;
  }
}

void RTTICleanPass::adjustOffsets() {
  assert(ImportSummary);
  LLVM_DEBUG(dbgs() << "Perform adjusting offsets: \n");
  for (auto &GV : M->globals()) {
    if (ImportSummary->needsAdjustOffset(GV.getGUID())) {
      LLVM_DEBUG(dbgs() << "  Adjust offset for " << GV.getName() << "\n");
      assert(GV.isDeclaration());
      auto &AdjustOffsets = ImportSummary->getAdjustOffsets(GV.getGUID());
      for (auto *U : make_early_inc_range(GV.users())) {
        unsigned IndexBits;
        if (U->getType()->isPtrOrPtrVectorTy())
          IndexBits = DL->getIndexTypeSizeInBits(U->getType());
        else
          IndexBits = 64;
        APInt Offset(IndexBits, 0);
        Value *UnderlyingObject = U->stripAndAccumulateConstantOffsets(
            *DL, Offset, /* AllowNonInbound */ false);
        if (!isa<GEPOperator>(U) || Offset.isZero())
          continue;

        uint64_t NewOffset;
        if (!AdjustOffsets.second.count(Offset.getZExtValue())) {
          auto Iter = AdjustOffsets.second.upper_bound(Offset.getZExtValue());
          if (Iter != AdjustOffsets.second.begin()) {
            Iter--;
            NewOffset = Offset.getZExtValue() - (Iter->first - Iter->second);
          } else
            continue;
        } else
          NewOffset = AdjustOffsets.second.find(Offset.getZExtValue())->second;

        LLVM_DEBUG(dbgs() << "    Replace " << *U << ", Old offset: " << Offset.getZExtValue() << ", New offset: "
                   << NewOffset << "\n");

        Constant *GEPOfNewVTable = ConstantExpr::getInBoundsGetElementPtr(IntegerType::getInt8Ty(GV.getContext()),
                    &GV, ConstantInt::get(IntegerType::getInt64Ty(GV.getContext()), NewOffset));
        U->replaceAllUsesWith(GEPOfNewVTable);
      }
    }
  }


}

static bool isGuaranteedToBeOptimized(StringRef DestTypeId, ClassHierarchyInfo &CHAInfo) {

  if (!CHAInfo.hasCompatibleAddressPoints(DestTypeId)) {
    return false;
  }

  if (!CHAInfo.isFixedInLinkTime(DestTypeId)) {
    return false;
  }

  auto &CompatibleAddressPoints = CHAInfo.getTypeIdCompatibleVTableInfo(DestTypeId);

  // FIXME: 100 is the value of MaxSuperChecks
  if (CompatibleAddressPoints.size() > 100)
    return false;

  return true;
}

void RTTICleanPass::checkSummary(std::set<AddressPoint> &DeadSet) {
  assert(ExportSummary);
  for (auto &Iter : ExportSummary->vtableAccesses()) {
    if (!CHAInfo->hasCompatibleAddressPoints(Iter.first()))
      continue;

    NumSlotsAccess++;
    auto &AddressPoints = CHAInfo->getTypeIdCompatibleVTableInfo(Iter.first());
    for (auto &AP : AddressPoints)
      DeadSet.erase(AP);
  }

  for (auto &Iter : ExportSummary->dynCastDstMap()) {
    if (!isGuaranteedToBeOptimized(Iter.first(), *CHAInfo)) {
      if (!CHAInfo->hasCompatibleAddressPoints(Iter.first()))
        continue;
      NumDynCastAccess++;
      auto &AddressPoints = CHAInfo->getTypeIdCompatibleVTableInfo(Iter.first());
      for (auto &AP : AddressPoints)
        DeadSet.erase(AP);
    }
  }

  for (auto &Iter : ExportSummary->dynCastSrcMap()) {
    if (!isGuaranteedToBeOptimized(Iter.first(), *CHAInfo)) {
      if (!CHAInfo->hasCompatibleAddressPoints(Iter.first()))
        continue;
      NumDynCastAccess++;
      auto &AddressPoints = CHAInfo->getTypeIdCompatibleVTableInfo(Iter.first());
      for (auto &AP : AddressPoints)
        DeadSet.erase(AP);
    }
  }

  for (auto &TypeInfo : ExportSummary->rttisUsedByNonDyncast()) {
    std::string TypeId =
          ABIManager::GetTypeIdFromTypeInfo(TypeInfo);
    if (!CHAInfo->hasCompatibleAddressPoints(TypeId))
      continue;
    auto &AddressPoints = CHAInfo->getTypeIdCompatibleVTableInfo(TypeId);
    for (auto &AP : AddressPoints)
      DeadSet.erase(AP);
  }
}

void RTTICleanPass::checkTypeTest(std::set<AddressPoint> &DeadSet) {
  Function *TypeTestFunc =
      M->getFunction(Intrinsic::getName(Intrinsic::type_test));

  if (TypeTestFunc) {
    for (User *U : TypeTestFunc->users()) {
      CallInst *TypeTestCall = cast<CallInst>(U);
      MetadataAsValue *TypeIdMd =
          cast<MetadataAsValue>(TypeTestCall->getArgOperand(1));
      auto *TypeIdStr = dyn_cast<MDString>(TypeIdMd->getMetadata());
      if (!TypeIdStr)
        continue;
      StringRef TypeId = TypeIdStr->getString();

      auto &DT =
          FAM->getResult<DominatorTreeAnalysis>(*TypeTestCall->getFunction());

      SmallVector<DevirtCallSite, 4> DevirtCalls;
      SmallVector<int64_t, 1> NonCallOffsets;
      SmallVector<CallInst *, 4> Assumes;
      findDevirtualizableCallsForTypeTest(DevirtCalls, NonCallOffsets, Assumes,
                                          TypeTestCall, DT);

      if (!NonCallOffsets.empty()) {
        if (CHAInfo->hasCompatibleAddressPoints(TypeId)) {
          auto &AddressPoints = CHAInfo->getTypeIdCompatibleVTableInfo(TypeId);
          for (auto &AP : AddressPoints) {
            NumUsedByTypeId++;
            DeadSet.erase(AP);
          }

        }

      }

      for (auto Offset : NonCallOffsets) {
        if (Offset == -DL->getPointerSize())
          NumUsedByTypeIdOp++;
        else if (Offset == -DL->getPointerSize() * 2)
          NumOffsetToTop++;
      }

    }
  }
}

void RTTICleanPass::checkDynamicCast(std::set<AddressPoint> &DeadSet) {
  for (GlobalVariable &GV : M->globals()) {
    if (!GV.getName().starts_with("_ZTI"))
      continue;
    for (User *U : GV.users()) {
      if (isa<Constant>(U))
        continue;

      std::string TypeId =
          ABIManager::GetTypeIdFromTypeInfo(GV.getName());
      if (!CHAInfo->hasCompatibleAddressPoints(TypeId))
        continue;
      auto &AddressPoints = CHAInfo->getTypeIdCompatibleVTableInfo(TypeId);
      for (auto &AP : AddressPoints)
        DeadSet.erase(AP);
    }
  }
}

template <class T>
void VTableUpdater::createIndicesValues(LLVMContext &Context,
                                        const SmallVectorImpl<APInt> &Indices,
                                        SmallVectorImpl<T *> &NewOperands) {
  for (unsigned I = 0; I < Indices.size(); I++) {
    NewOperands.push_back(ConstantInt::get(
        IntegerType::get(Context, Indices[I].getBitWidth()), Indices[I]));
  }
}
