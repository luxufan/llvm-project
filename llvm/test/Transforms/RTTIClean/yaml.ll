; RUN: opt -passes=rtti-clean -rtti-clean-write-summary=%t %s
; RUN: FileCheck --check-prefix=SUMMARY %s < %t
;
; Check the summary yaml is correct.
;
; SUMMARY: VTableOffsetAdjust:
; SUMMARY:   7199889692870024414:
; SUMMARY:     16:              0

declare i32 @vf()
%vtTy = type { [3 x ptr] }

@_ZTVvt = internal constant %vtTy { [3 x ptr] [ptr null, ptr null, ptr @vf] }, !type !0

!0 = !{i32 16, !"_ZTSvt"}
