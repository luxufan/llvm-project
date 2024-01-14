; Test summary parsing of index-based WPD related summary fields
; RUN: llvm-as %s -o - | llvm-dis -o %t.ll
; RUN: grep "^\^" %s >%t2
; RUN: grep "^\^" %t.ll >%t3
; Expect that the summary information is the same after round-trip through
; llvm-as and llvm-dis.
; RUN: diff -b %t2 %t3

source_filename = "thinlto-vtable-summary.ll"
target datalayout = "e-m:e-i64:64-f80:128-n8:16:32:64-S128"
target triple = "x86_64-grtev4-linux-gnu"

%struct.A = type { ptr }
%struct.B = type { %struct.A }
%struct.C = type { %struct.A }

@_ZTV1B = constant { [4 x ptr] } { [4 x ptr] [ptr null, ptr undef, ptr @_ZN1B1fEi, ptr @_ZN1A1nEi] }, !type !0, !type !1
@_ZTV1C = constant { [4 x ptr] } { [4 x ptr] [ptr null, ptr undef, ptr @_ZN1C1fEi, ptr @_ZN1A1nEi] }, !type !0, !type !2

@typeid1 = external global ptr
@typeid2 = external global ptr

@_ZTIxxx = external global ptr
@_ZTIxxxx = external global ptr

declare i32 @_ZN1B1fEi(ptr, i32)

declare i32 @_ZN1A1nEi(ptr, i32)

declare i32 @_ZN1C1fEi(ptr, i32)

!0 = !{i64 16, !"_ZTS1A"}
!1 = !{i64 16, !"_ZTS1B"}
!2 = !{i64 16, !"_ZTS1C"}

^0 = module: (path: "<stdin>", hash: (0, 0, 0, 0, 0))
^1 = gv: (name: "_ZTIxxxx") ; guid = 1223429745491429665
^2 = gv: (name: "_ZN1A1nEi") ; guid = 1621563287929432257
^3 = gv: (name: "_ZTIxxx") ; guid = 2928584540419986814
^4 = gv: (name: "_ZTV1B", summaries: (variable: (module: ^0, flags: (linkage: external, visibility: default, notEligibleToImport: 0, live: 0, dsoLocal: 0, canAutoHide: 0), varFlags: (readonly: 0, writeonly: 0, constant: 0, vcall_visibility: 0), vTableFuncs: ((virtFunc: ^5, offset: 16), (virtFunc: ^2, offset: 24)), refs: (^5, ^2)))) ; guid = 5283576821522790367
^5 = gv: (name: "_ZN1B1fEi") ; guid = 7162046368816414394
^6 = gv: (name: "_ZTV1C", summaries: (variable: (module: ^0, flags: (linkage: external, visibility: default, notEligibleToImport: 0, live: 0, dsoLocal: 0, canAutoHide: 0), varFlags: (readonly: 0, writeonly: 0, constant: 0, vcall_visibility: 0), vTableFuncs: ((virtFunc: ^8, offset: 16), (virtFunc: ^2, offset: 24)), refs: (^2, ^8)))) ; guid = 13624023785555846296
^7 = gv: (name: "typeid1") ; guid = 14276520915468743435
^8 = gv: (name: "_ZN1C1fEi") ; guid = 14876272565662207556
^9 = gv: (name: "typeid2") ; guid = 15427464259790519041
^10 = typeidCompatibleVTable: (name: "_ZTS1A", summary: ((offset: 16, ^4), (offset: 16, ^6))) ; guid = 7004155349499253778
^11 = typeidCompatibleVTable: (name: "_ZTS1B", summary: ((offset: 16, ^4))) ; guid = 6203814149063363976
^12 = typeidCompatibleVTable: (name: "_ZTS1C", summary: ((offset: 16, ^6))) ; guid = 1884921850105019584
^13 = vtableaccesses: ((name: "_ZTS1A", offset: -8), (name: "_ZTS1A", offset: -16), (name: "_ZTS1B", offset: -8))
^14 = rttisusedbynondyncast: (name: "_ZTIxxx", name: "_ZTIxxxx")
^15 = dyncastdst: ((name: "typeid2", count: 2), (name: "typeid1", count: 1))
^16 = dyncastsrc: ((name: "typeid2", count: 2), (name: "typeid1", count: 1))
^17 = blockcount: 0
