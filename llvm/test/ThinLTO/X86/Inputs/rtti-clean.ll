target datalayout = "e-m:e-p270:32:32-p271:32:32-p272:64:64-i64:64-f80:128-n8:16:32:64-S128"
target triple = "x86_64-unknown-linux-gnu"

@_ZTIvt = external constant ptr
@_ZTIvtbase = weak_odr constant { ptr } { ptr @_ZTIvt }
@_ZTIvt1 = weak_odr constant { ptr } { ptr @_ZTIvt }
@_ZTVvt = external constant ptr
@_ZTVvt1 = external constant ptr
