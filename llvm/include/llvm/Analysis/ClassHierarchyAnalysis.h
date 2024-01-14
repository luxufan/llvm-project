#ifndef CLASSHIERARCHYANALYSIS_H_
#define CLASSHIERARCHYANALYSIS_H_

#include "llvm/IR/PassManager.h"
#include <set>
#include <map>

namespace llvm {

enum CXXABIKind {
Itanium,
Microsoft,
};

template <CXXABIKind C> class CXXABIInfo;

template <CXXABIKind C>
class CXXABIManager {
  using Impl = CXXABIInfo<C>;

public:
  static std::string GetVTableNameFromTypeId(StringRef TypeId) {
    bool Consume = TypeId.consume_front(Impl::TypeIdPrefix);
    assert(Consume && "Type id name is not started with specific prefix");
    (void)Consume;
    return Impl::VTablePrefix.str() + TypeId.str();
  }

  static std::string GetVTableNameFromTypeInfo(StringRef TypeInfo) {
    bool Consume = TypeInfo.consume_front(Impl::TypeInfoPrefix);
    assert(Consume && "Type info name is not started with specific prefix");
    (void)Consume;
    return Impl::VTablePrefix.str() + TypeInfo.str();
  }

  static std::string GetTypeIdFromVTable(StringRef VTableName) {
    bool Consume = VTableName.consume_front(Impl::VTablePrefix);
    assert(Consume && "Virtual table name is not started with specific prefix");
    (void)Consume;
    return Impl::TypeIdPrefix.str() + VTableName.str();
  }

  static std::string GetTypeIdFromTypeInfo(StringRef TypeInfo) {
    bool Consume = TypeInfo.consume_front(Impl::TypeInfoPrefix);
    assert(Consume && "Virtual table name is not started with specific prefix");
    (void)Consume;
    return Impl::TypeIdPrefix.str() + TypeInfo.str();
  }

  static std::string GetTypeInfoFromVTable(StringRef VTableName) {
    bool Consume = VTableName.consume_front(Impl::VTablePrefix);
    assert(Consume && "Virtual table name is not started with specific prefix");
    (void)Consume;
    return Impl::TypeInfoPrefix.str() + VTableName.str();

  }

  static std::string GetTypeInfoFromTypeId(StringRef VTableName) {
    bool Consume = VTableName.consume_front(Impl::TypeIdPrefix);
    assert(Consume && "Virtual table name is not started with specific prefix");
    (void)Consume;
    return Impl::TypeInfoPrefix.str() + VTableName.str();

  }

  static bool IsTypeInfo(StringRef TypeInfo) {
    return TypeInfo.starts_with(Impl::TypeInfoPrefix);
  }

  static StringRef GetVTablePrefix() { return Impl::VTablePrefix; }

  static StringRef GetTypeInfoPrefix() { return Impl::TypeInfoPrefix; }

  static StringRef GetTypeIdPrefix() { return Impl::TypeIdPrefix; }

  static StringRef GetDynamicCastName() { return Impl::DynamicCastName; }

  static int GetRTTISlotOffset() { return Impl::RTTISlotOffset; }
};

template <> class CXXABIInfo<Itanium> {
public:
  static constexpr StringRef TypeIdPrefix = "_ZTS";
  static constexpr StringRef TypeInfoPrefix = "_ZTI";
  static constexpr StringRef VTablePrefix = "_ZTV";
  static constexpr StringRef DynamicCastName = "__dynamic_cast";
  static constexpr int RTTISlotOffset = -1;
};

/// This class represents the address point concept in Itanuim cxx abi.
struct AddressPoint {
  // VTableName is the name of the virtual table that this address
  // point points to
  StringRef VTableName;
  // Offset from the beginning of the virtual table
  uint64_t Offset;

  AddressPoint(StringRef Name, uint64_t Offset)
      : VTableName(Name), Offset(Offset) {}
};
}

template <> struct std::less<llvm::AddressPoint> {
  bool operator()(const llvm::AddressPoint &Lhs,
                  const llvm::AddressPoint &Rhs) const {
    return llvm::hash_combine(Lhs.VTableName, Lhs.Offset) <
           llvm::hash_combine(Rhs.VTableName, Rhs.Offset);
  }
};

namespace llvm {

/// ClassHierarchyInfo records the subclass relationship for class that
/// all of classes in its whole class hierarchy have internal linkage or
/// its vcallvisibility is not public.
class ClassHierarchyInfo {
  using ABIManager = CXXABIManager<Itanium>;
public:
  using AddressPointsSet = std::set<AddressPoint>;

private:
  Module &M;

  // Typeid metadata map. The reason why doesn't use the typeid map in summary
  // index is FullLTO does not record typeid into the typeid map.
  std::map<StringRef, AddressPointsSet> TypeIdCompatibleAddressPoints;

  std::map<std::string, uint64_t> PrimaryAddressPointOffsets;

  // This set records type ids that their number of address points may
  // change at link time.
  DenseSet<StringRef> mayChangeInLinkTime;

public:
  void init(const ModuleSummaryIndex *ImportSummary, bool Optimize = false);
  ClassHierarchyInfo(Module &M) : M(M) {}

  void insertTypeIdCompatibleAddressPoint(StringRef TypeId,
                                          StringRef VTableName,
                                          uint64_t Offset) {
    TypeIdCompatibleAddressPoints[TypeId].insert(
        AddressPoint(VTableName, Offset));
  }

  bool hasCompatibleAddressPoints(StringRef TypeID) const {
    return TypeIdCompatibleAddressPoints.count(TypeID) != 0;
  }

  const AddressPointsSet &getTypeIdCompatibleVTableInfo(StringRef TypeID) const {
    assert(hasCompatibleAddressPoints(TypeID) && "Doesn't have compatible address points!");
    return TypeIdCompatibleAddressPoints.find(TypeID)->second;
  }

  // Return true if the number of address points will not
  // increase in link time.
  bool isFixedInLinkTime(StringRef TypeId) {
    assert(hasCompatibleAddressPoints(TypeId) && "ClassHierarchyAnalyis does not have this address point");
    return !mayChangeInLinkTime.contains(TypeId);
  }

  const auto &getTypeIdMap() const {
    return TypeIdCompatibleAddressPoints;
  }
};

class ClassHierarchyAnalysis : public AnalysisInfoMixin<ClassHierarchyAnalysis> {
  friend AnalysisInfoMixin<ClassHierarchyAnalysis>;
  static AnalysisKey Key;

public:
  using Result = ClassHierarchyInfo;

  ClassHierarchyInfo run(Module &M, ModuleAnalysisManager &AM) {
    return ClassHierarchyInfo(M);
  }
};

}


#endif // CLASSHIERARCHYANALYSIS_H_
