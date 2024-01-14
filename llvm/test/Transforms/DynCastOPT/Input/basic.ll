@_ZTVvt1 = weak_odr hidden constant ptr null, !type !0, !vcall_visibility !2
@_ZTVvt2 = weak_odr hidden constant ptr null, !type !0, !type !1, !vcall_visibility !2

!0 = !{i32 0, !"_ZTSvt1"}
!1 = !{i32 0, !"_ZTSvt2"}
!2 = !{i32 1}

^0 = module: (path: "[Regular LTO]", hash: (0, 0, 0, 0, 0))
^1 = gv: (name: "_ZTVvt1", summaries: (variable: (module: ^0, flags: (linkage: weak_odr, visibility: hidden, notEligibleToImport: 0, live: 0, dsoLocal: 1, canAutoHide: 0, importType: definition), varFlags: (readonly: 1, writeonly: 0, constant: 1, vcall_visibility: 1)))) ; guid = 12545491950472416514
^2 = gv: (name: "_ZTVvt2", summaries: (variable: (module: ^0, flags: (linkage: weak_odr, visibility: hidden, notEligibleToImport: 0, live: 0, dsoLocal: 1, canAutoHide: 0, importType: definition), varFlags: (readonly: 1, writeonly: 0, constant: 1, vcall_visibility: 1)))) ; guid = 13479009217555890650
^3 = typeidCompatibleVTable: (name: "_ZTSvt1", summary: ((offset: 0, ^1), (offset: 0, ^2))) ; guid = 15578902725261158874
^4 = typeidCompatibleVTable: (name: "_ZTSvt2", summary: ((offset: 0, ^2))) ; guid = 6732071633713080711
^5 = blockcount: 0
