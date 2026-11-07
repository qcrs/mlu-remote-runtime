; ModuleID = '/home/lvtong/mlu-remote-runtime/tests/native_bang_relocations.mlu'
source_filename = "/home/lvtong/mlu-remote-runtime/tests/native_bang_relocations.mlu"
target datalayout = "e-p:64:64:64-i64:64-v16:16-v32:32-n16:32:64"
target triple = "mlisa-cambricon-bang"

%struct.__bang_bfloat16.0 = type { i16 }
%struct.__bang_bfloat16_raw = type { i16 }
%struct.Payload = type { i8, [2 x %struct.Leaf], i32*, i64 }
%struct.Leaf = type { i32*, i32*, i32 }

$_ZN15__bang_bfloat16C1ERK19__bang_bfloat16_raw = comdat any

$_ZN15__bang_bfloat16C1Ev = comdat any

$_ZNK15__bang_bfloat16cv19__bang_bfloat16_rawEv = comdat any

$_ZN15__bang_bfloat16aSERK19__bang_bfloat16_raw = comdat any

; Function Attrs: alwaysinline convergent nounwind
define internal %struct.__bang_bfloat16.0 @_ZL6__hadd15__bang_bfloat16S_(%struct.__bang_bfloat16.0* byval(%struct.__bang_bfloat16.0) align 2 %0, %struct.__bang_bfloat16.0* byval(%struct.__bang_bfloat16.0) align 2 %1) #0 {
  %3 = alloca %struct.__bang_bfloat16.0, align 2
  %4 = alloca float, align 4
  %5 = alloca %struct.__bang_bfloat16.0, align 2
  %6 = alloca float, align 4
  %7 = alloca %struct.__bang_bfloat16.0, align 2
  %8 = alloca float, align 4
  %9 = alloca %struct.__bang_bfloat16.0, align 2
  call void @_ZN15__bang_bfloat16C1Ev(%struct.__bang_bfloat16.0* %3) #3
  %10 = bitcast %struct.__bang_bfloat16.0* %5 to i8*
  %11 = bitcast %struct.__bang_bfloat16.0* %0 to i8*
  call void @llvm.memcpy.p0i8.p0i8.i64(i8* align 2 %10, i8* align 2 %11, i64 2, i1 false)
  %12 = call contract float @_ZL18__bfloat162float__15__bang_bfloat16(%struct.__bang_bfloat16.0* byval(%struct.__bang_bfloat16.0) align 2 %5) #3
  store float %12, float* %4, align 4
  %13 = bitcast %struct.__bang_bfloat16.0* %7 to i8*
  %14 = bitcast %struct.__bang_bfloat16.0* %1 to i8*
  call void @llvm.memcpy.p0i8.p0i8.i64(i8* align 2 %13, i8* align 2 %14, i64 2, i1 false)
  %15 = call contract float @_ZL18__bfloat162float__15__bang_bfloat16(%struct.__bang_bfloat16.0* byval(%struct.__bang_bfloat16.0) align 2 %7) #3
  store float %15, float* %6, align 4
  %16 = load float, float* %4, align 4
  %17 = load float, float* %6, align 4
  %18 = fadd contract float %16, %17
  store float %18, float* %8, align 4
  %19 = load float, float* %8, align 4
  %20 = call %struct.__bang_bfloat16.0 @_ZL18__float2bfloat16__f(float %19) #3
  %21 = getelementptr inbounds %struct.__bang_bfloat16.0, %struct.__bang_bfloat16.0* %9, i32 0, i32 0
  %22 = extractvalue %struct.__bang_bfloat16.0 %20, 0
  store i16 %22, i16* %21, align 2
  %23 = bitcast %struct.__bang_bfloat16.0* %3 to i8*
  %24 = bitcast %struct.__bang_bfloat16.0* %9 to i8*
  call void @llvm.memcpy.p0i8.p0i8.i64(i8* align 2 %23, i8* align 2 %24, i64 2, i1 false)
  %25 = load %struct.__bang_bfloat16.0, %struct.__bang_bfloat16.0* %3, align 2
  ret %struct.__bang_bfloat16.0 %25
}

; Function Attrs: argmemonly nounwind willreturn
declare void @llvm.memcpy.p0i8.p0i8.i64(i8* noalias nocapture writeonly, i8* noalias nocapture readonly, i64, i1 immarg) #1

; Function Attrs: alwaysinline convergent nounwind
define internal %struct.__bang_bfloat16.0 @_ZL6__hsub15__bang_bfloat16S_(%struct.__bang_bfloat16.0* byval(%struct.__bang_bfloat16.0) align 2 %0, %struct.__bang_bfloat16.0* byval(%struct.__bang_bfloat16.0) align 2 %1) #0 {
  %3 = alloca %struct.__bang_bfloat16.0, align 2
  %4 = alloca float, align 4
  %5 = alloca %struct.__bang_bfloat16.0, align 2
  %6 = alloca float, align 4
  %7 = alloca %struct.__bang_bfloat16.0, align 2
  %8 = alloca float, align 4
  %9 = alloca %struct.__bang_bfloat16.0, align 2
  call void @_ZN15__bang_bfloat16C1Ev(%struct.__bang_bfloat16.0* %3) #3
  %10 = bitcast %struct.__bang_bfloat16.0* %5 to i8*
  %11 = bitcast %struct.__bang_bfloat16.0* %0 to i8*
  call void @llvm.memcpy.p0i8.p0i8.i64(i8* align 2 %10, i8* align 2 %11, i64 2, i1 false)
  %12 = call contract float @_ZL18__bfloat162float__15__bang_bfloat16(%struct.__bang_bfloat16.0* byval(%struct.__bang_bfloat16.0) align 2 %5) #3
  store float %12, float* %4, align 4
  %13 = bitcast %struct.__bang_bfloat16.0* %7 to i8*
  %14 = bitcast %struct.__bang_bfloat16.0* %1 to i8*
  call void @llvm.memcpy.p0i8.p0i8.i64(i8* align 2 %13, i8* align 2 %14, i64 2, i1 false)
  %15 = call contract float @_ZL18__bfloat162float__15__bang_bfloat16(%struct.__bang_bfloat16.0* byval(%struct.__bang_bfloat16.0) align 2 %7) #3
  store float %15, float* %6, align 4
  %16 = load float, float* %4, align 4
  %17 = load float, float* %6, align 4
  %18 = fsub contract float %16, %17
  store float %18, float* %8, align 4
  %19 = load float, float* %8, align 4
  %20 = call %struct.__bang_bfloat16.0 @_ZL18__float2bfloat16__f(float %19) #3
  %21 = getelementptr inbounds %struct.__bang_bfloat16.0, %struct.__bang_bfloat16.0* %9, i32 0, i32 0
  %22 = extractvalue %struct.__bang_bfloat16.0 %20, 0
  store i16 %22, i16* %21, align 2
  %23 = bitcast %struct.__bang_bfloat16.0* %3 to i8*
  %24 = bitcast %struct.__bang_bfloat16.0* %9 to i8*
  call void @llvm.memcpy.p0i8.p0i8.i64(i8* align 2 %23, i8* align 2 %24, i64 2, i1 false)
  %25 = load %struct.__bang_bfloat16.0, %struct.__bang_bfloat16.0* %3, align 2
  ret %struct.__bang_bfloat16.0 %25
}

; Function Attrs: alwaysinline convergent nounwind
define internal %struct.__bang_bfloat16.0 @_ZL6__hmul15__bang_bfloat16S_(%struct.__bang_bfloat16.0* byval(%struct.__bang_bfloat16.0) align 2 %0, %struct.__bang_bfloat16.0* byval(%struct.__bang_bfloat16.0) align 2 %1) #0 {
  %3 = alloca %struct.__bang_bfloat16.0, align 2
  %4 = alloca float, align 4
  %5 = alloca %struct.__bang_bfloat16.0, align 2
  %6 = alloca float, align 4
  %7 = alloca %struct.__bang_bfloat16.0, align 2
  %8 = alloca float, align 4
  %9 = alloca %struct.__bang_bfloat16.0, align 2
  call void @_ZN15__bang_bfloat16C1Ev(%struct.__bang_bfloat16.0* %3) #3
  %10 = bitcast %struct.__bang_bfloat16.0* %5 to i8*
  %11 = bitcast %struct.__bang_bfloat16.0* %0 to i8*
  call void @llvm.memcpy.p0i8.p0i8.i64(i8* align 2 %10, i8* align 2 %11, i64 2, i1 false)
  %12 = call contract float @_ZL18__bfloat162float__15__bang_bfloat16(%struct.__bang_bfloat16.0* byval(%struct.__bang_bfloat16.0) align 2 %5) #3
  store float %12, float* %4, align 4
  %13 = bitcast %struct.__bang_bfloat16.0* %7 to i8*
  %14 = bitcast %struct.__bang_bfloat16.0* %1 to i8*
  call void @llvm.memcpy.p0i8.p0i8.i64(i8* align 2 %13, i8* align 2 %14, i64 2, i1 false)
  %15 = call contract float @_ZL18__bfloat162float__15__bang_bfloat16(%struct.__bang_bfloat16.0* byval(%struct.__bang_bfloat16.0) align 2 %7) #3
  store float %15, float* %6, align 4
  %16 = load float, float* %4, align 4
  %17 = load float, float* %6, align 4
  %18 = fmul contract float %16, %17
  store float %18, float* %8, align 4
  %19 = load float, float* %8, align 4
  %20 = call %struct.__bang_bfloat16.0 @_ZL18__float2bfloat16__f(float %19) #3
  %21 = getelementptr inbounds %struct.__bang_bfloat16.0, %struct.__bang_bfloat16.0* %9, i32 0, i32 0
  %22 = extractvalue %struct.__bang_bfloat16.0 %20, 0
  store i16 %22, i16* %21, align 2
  %23 = bitcast %struct.__bang_bfloat16.0* %3 to i8*
  %24 = bitcast %struct.__bang_bfloat16.0* %9 to i8*
  call void @llvm.memcpy.p0i8.p0i8.i64(i8* align 2 %23, i8* align 2 %24, i64 2, i1 false)
  %25 = load %struct.__bang_bfloat16.0, %struct.__bang_bfloat16.0* %3, align 2
  ret %struct.__bang_bfloat16.0 %25
}

; Function Attrs: alwaysinline convergent nounwind
define internal %struct.__bang_bfloat16.0 @_ZL6__hdiv15__bang_bfloat16S_(%struct.__bang_bfloat16.0* byval(%struct.__bang_bfloat16.0) align 2 %0, %struct.__bang_bfloat16.0* byval(%struct.__bang_bfloat16.0) align 2 %1) #0 {
  %3 = alloca %struct.__bang_bfloat16.0, align 2
  %4 = alloca float, align 4
  %5 = alloca %struct.__bang_bfloat16.0, align 2
  %6 = alloca float, align 4
  %7 = alloca %struct.__bang_bfloat16.0, align 2
  %8 = alloca float, align 4
  %9 = alloca %struct.__bang_bfloat16.0, align 2
  call void @_ZN15__bang_bfloat16C1Ev(%struct.__bang_bfloat16.0* %3) #3
  %10 = bitcast %struct.__bang_bfloat16.0* %5 to i8*
  %11 = bitcast %struct.__bang_bfloat16.0* %0 to i8*
  call void @llvm.memcpy.p0i8.p0i8.i64(i8* align 2 %10, i8* align 2 %11, i64 2, i1 false)
  %12 = call contract float @_ZL18__bfloat162float__15__bang_bfloat16(%struct.__bang_bfloat16.0* byval(%struct.__bang_bfloat16.0) align 2 %5) #3
  store float %12, float* %4, align 4
  %13 = bitcast %struct.__bang_bfloat16.0* %7 to i8*
  %14 = bitcast %struct.__bang_bfloat16.0* %1 to i8*
  call void @llvm.memcpy.p0i8.p0i8.i64(i8* align 2 %13, i8* align 2 %14, i64 2, i1 false)
  %15 = call contract float @_ZL18__bfloat162float__15__bang_bfloat16(%struct.__bang_bfloat16.0* byval(%struct.__bang_bfloat16.0) align 2 %7) #3
  store float %15, float* %6, align 4
  %16 = load float, float* %4, align 4
  %17 = load float, float* %6, align 4
  %18 = fdiv contract float %16, %17
  store float %18, float* %8, align 4
  %19 = load float, float* %8, align 4
  %20 = call %struct.__bang_bfloat16.0 @_ZL18__float2bfloat16__f(float %19) #3
  %21 = getelementptr inbounds %struct.__bang_bfloat16.0, %struct.__bang_bfloat16.0* %9, i32 0, i32 0
  %22 = extractvalue %struct.__bang_bfloat16.0 %20, 0
  store i16 %22, i16* %21, align 2
  %23 = bitcast %struct.__bang_bfloat16.0* %3 to i8*
  %24 = bitcast %struct.__bang_bfloat16.0* %9 to i8*
  call void @llvm.memcpy.p0i8.p0i8.i64(i8* align 2 %23, i8* align 2 %24, i64 2, i1 false)
  %25 = load %struct.__bang_bfloat16.0, %struct.__bang_bfloat16.0* %3, align 2
  ret %struct.__bang_bfloat16.0 %25
}

; Function Attrs: alwaysinline convergent nounwind
define internal nonnull align 2 dereferenceable(2) %struct.__bang_bfloat16.0* @_ZpLR15__bang_bfloat16RKS_(%struct.__bang_bfloat16.0* nonnull align 2 dereferenceable(2) %0, %struct.__bang_bfloat16.0* nonnull align 2 dereferenceable(2) %1) #0 {
  %3 = alloca %struct.__bang_bfloat16.0*, align 8
  %4 = alloca %struct.__bang_bfloat16.0*, align 8
  %5 = alloca %struct.__bang_bfloat16.0, align 2
  %6 = alloca %struct.__bang_bfloat16.0, align 2
  %7 = alloca %struct.__bang_bfloat16.0, align 2
  store %struct.__bang_bfloat16.0* %0, %struct.__bang_bfloat16.0** %3, align 8
  store %struct.__bang_bfloat16.0* %1, %struct.__bang_bfloat16.0** %4, align 8
  %8 = load %struct.__bang_bfloat16.0*, %struct.__bang_bfloat16.0** %3, align 8
  %9 = bitcast %struct.__bang_bfloat16.0* %6 to i8*
  %10 = bitcast %struct.__bang_bfloat16.0* %8 to i8*
  call void @llvm.memcpy.p0i8.p0i8.i64(i8* align 2 %9, i8* align 2 %10, i64 2, i1 false)
  %11 = load %struct.__bang_bfloat16.0*, %struct.__bang_bfloat16.0** %4, align 8
  %12 = bitcast %struct.__bang_bfloat16.0* %7 to i8*
  %13 = bitcast %struct.__bang_bfloat16.0* %11 to i8*
  call void @llvm.memcpy.p0i8.p0i8.i64(i8* align 2 %12, i8* align 2 %13, i64 2, i1 false)
  %14 = call %struct.__bang_bfloat16.0 @_ZL6__hadd15__bang_bfloat16S_(%struct.__bang_bfloat16.0* byval(%struct.__bang_bfloat16.0) align 2 %6, %struct.__bang_bfloat16.0* byval(%struct.__bang_bfloat16.0) align 2 %7) #3
  %15 = getelementptr inbounds %struct.__bang_bfloat16.0, %struct.__bang_bfloat16.0* %5, i32 0, i32 0
  %16 = extractvalue %struct.__bang_bfloat16.0 %14, 0
  store i16 %16, i16* %15, align 2
  %17 = load %struct.__bang_bfloat16.0*, %struct.__bang_bfloat16.0** %3, align 8
  %18 = bitcast %struct.__bang_bfloat16.0* %17 to i8*
  %19 = bitcast %struct.__bang_bfloat16.0* %5 to i8*
  call void @llvm.memcpy.p0i8.p0i8.i64(i8* align 2 %18, i8* align 2 %19, i64 2, i1 false)
  %20 = load %struct.__bang_bfloat16.0*, %struct.__bang_bfloat16.0** %3, align 8
  ret %struct.__bang_bfloat16.0* %20
}

; Function Attrs: alwaysinline convergent nounwind
define internal nonnull align 2 dereferenceable(2) %struct.__bang_bfloat16.0* @_ZmIR15__bang_bfloat16RKS_(%struct.__bang_bfloat16.0* nonnull align 2 dereferenceable(2) %0, %struct.__bang_bfloat16.0* nonnull align 2 dereferenceable(2) %1) #0 {
  %3 = alloca %struct.__bang_bfloat16.0*, align 8
  %4 = alloca %struct.__bang_bfloat16.0*, align 8
  %5 = alloca %struct.__bang_bfloat16.0, align 2
  %6 = alloca %struct.__bang_bfloat16.0, align 2
  %7 = alloca %struct.__bang_bfloat16.0, align 2
  store %struct.__bang_bfloat16.0* %0, %struct.__bang_bfloat16.0** %3, align 8
  store %struct.__bang_bfloat16.0* %1, %struct.__bang_bfloat16.0** %4, align 8
  %8 = load %struct.__bang_bfloat16.0*, %struct.__bang_bfloat16.0** %3, align 8
  %9 = bitcast %struct.__bang_bfloat16.0* %6 to i8*
  %10 = bitcast %struct.__bang_bfloat16.0* %8 to i8*
  call void @llvm.memcpy.p0i8.p0i8.i64(i8* align 2 %9, i8* align 2 %10, i64 2, i1 false)
  %11 = load %struct.__bang_bfloat16.0*, %struct.__bang_bfloat16.0** %4, align 8
  %12 = bitcast %struct.__bang_bfloat16.0* %7 to i8*
  %13 = bitcast %struct.__bang_bfloat16.0* %11 to i8*
  call void @llvm.memcpy.p0i8.p0i8.i64(i8* align 2 %12, i8* align 2 %13, i64 2, i1 false)
  %14 = call %struct.__bang_bfloat16.0 @_ZL6__hsub15__bang_bfloat16S_(%struct.__bang_bfloat16.0* byval(%struct.__bang_bfloat16.0) align 2 %6, %struct.__bang_bfloat16.0* byval(%struct.__bang_bfloat16.0) align 2 %7) #3
  %15 = getelementptr inbounds %struct.__bang_bfloat16.0, %struct.__bang_bfloat16.0* %5, i32 0, i32 0
  %16 = extractvalue %struct.__bang_bfloat16.0 %14, 0
  store i16 %16, i16* %15, align 2
  %17 = load %struct.__bang_bfloat16.0*, %struct.__bang_bfloat16.0** %3, align 8
  %18 = bitcast %struct.__bang_bfloat16.0* %17 to i8*
  %19 = bitcast %struct.__bang_bfloat16.0* %5 to i8*
  call void @llvm.memcpy.p0i8.p0i8.i64(i8* align 2 %18, i8* align 2 %19, i64 2, i1 false)
  %20 = load %struct.__bang_bfloat16.0*, %struct.__bang_bfloat16.0** %3, align 8
  ret %struct.__bang_bfloat16.0* %20
}

; Function Attrs: alwaysinline convergent nounwind
define linkonce_odr dso_local void @_ZN15__bang_bfloat16C1ERK19__bang_bfloat16_raw(%struct.__bang_bfloat16.0* %0, %struct.__bang_bfloat16_raw* nonnull align 2 dereferenceable(2) %1) unnamed_addr #0 comdat align 2 {
  %3 = alloca %struct.__bang_bfloat16.0*, align 8
  %4 = alloca %struct.__bang_bfloat16_raw*, align 8
  %5 = alloca %struct.__bang_bfloat16.0*, align 8
  %6 = alloca %struct.__bang_bfloat16_raw*, align 8
  store %struct.__bang_bfloat16.0* %0, %struct.__bang_bfloat16.0** %5, align 8
  store %struct.__bang_bfloat16_raw* %1, %struct.__bang_bfloat16_raw** %6, align 8
  %7 = load %struct.__bang_bfloat16.0*, %struct.__bang_bfloat16.0** %5, align 8
  %8 = load %struct.__bang_bfloat16_raw*, %struct.__bang_bfloat16_raw** %6, align 8
  store %struct.__bang_bfloat16.0* %7, %struct.__bang_bfloat16.0** %3, align 8
  store %struct.__bang_bfloat16_raw* %8, %struct.__bang_bfloat16_raw** %4, align 8
  %9 = load %struct.__bang_bfloat16.0*, %struct.__bang_bfloat16.0** %3, align 8
  %10 = getelementptr inbounds %struct.__bang_bfloat16.0, %struct.__bang_bfloat16.0* %9, i32 0, i32 0
  %11 = load %struct.__bang_bfloat16_raw*, %struct.__bang_bfloat16_raw** %4, align 8
  %12 = getelementptr inbounds %struct.__bang_bfloat16_raw, %struct.__bang_bfloat16_raw* %11, i32 0, i32 0
  %13 = load i16, i16* %12, align 2
  store i16 %13, i16* %10, align 2
  ret void
}

; Function Attrs: alwaysinline convergent nounwind
define internal %struct.__bang_bfloat16.0 @_ZL6__hneg15__bang_bfloat16(%struct.__bang_bfloat16.0* byval(%struct.__bang_bfloat16.0) align 2 %0) #0 {
  %2 = alloca %struct.__bang_bfloat16.0, align 2
  %3 = alloca %struct.__bang_bfloat16.0, align 2
  %4 = alloca float, align 4
  %5 = alloca %struct.__bang_bfloat16.0, align 2
  %6 = alloca float, align 4
  call void @_ZN15__bang_bfloat16C1Ev(%struct.__bang_bfloat16.0* %3) #3
  %7 = bitcast %struct.__bang_bfloat16.0* %5 to i8*
  %8 = bitcast %struct.__bang_bfloat16.0* %0 to i8*
  call void @llvm.memcpy.p0i8.p0i8.i64(i8* align 2 %7, i8* align 2 %8, i64 2, i1 false)
  %9 = call contract float @_ZL18__bfloat162float__15__bang_bfloat16(%struct.__bang_bfloat16.0* byval(%struct.__bang_bfloat16.0) align 2 %5) #3
  store float %9, float* %4, align 4
  %10 = load float, float* %6, align 4
  %11 = load float, float* %4, align 4
  %12 = call contract float asm "neg.gpr.f32 \09 $0, $1; \0A\09", "=r,r,0"(float %11, float %10) #4, !srcloc !4
  store float %12, float* %6, align 4
  %13 = load float, float* %6, align 4
  %14 = call %struct.__bang_bfloat16.0 @_ZL18__float2bfloat16__f(float %13) #3
  %15 = getelementptr inbounds %struct.__bang_bfloat16.0, %struct.__bang_bfloat16.0* %2, i32 0, i32 0
  %16 = extractvalue %struct.__bang_bfloat16.0 %14, 0
  store i16 %16, i16* %15, align 2
  %17 = load %struct.__bang_bfloat16.0, %struct.__bang_bfloat16.0* %2, align 2
  ret %struct.__bang_bfloat16.0 %17
}

; Function Attrs: alwaysinline convergent nounwind
define internal zeroext i1 @_ZL5__heq15__bang_bfloat16S_(%struct.__bang_bfloat16.0* byval(%struct.__bang_bfloat16.0) align 2 %0, %struct.__bang_bfloat16.0* byval(%struct.__bang_bfloat16.0) align 2 %1) #0 {
  %3 = alloca float, align 4
  %4 = alloca float, align 4
  %5 = alloca float, align 4
  %6 = alloca %struct.__bang_bfloat16.0, align 2
  %7 = alloca %struct.__bang_bfloat16.0, align 2
  %8 = alloca i8, align 1
  %9 = bitcast %struct.__bang_bfloat16.0* %6 to i8*
  %10 = bitcast %struct.__bang_bfloat16.0* %0 to i8*
  call void @llvm.memcpy.p0i8.p0i8.i64(i8* align 2 %9, i8* align 2 %10, i64 2, i1 false)
  %11 = call contract float @_ZL18__bfloat162float__15__bang_bfloat16(%struct.__bang_bfloat16.0* byval(%struct.__bang_bfloat16.0) align 2 %6) #3
  store float %11, float* %3, align 4
  %12 = bitcast %struct.__bang_bfloat16.0* %7 to i8*
  %13 = bitcast %struct.__bang_bfloat16.0* %1 to i8*
  call void @llvm.memcpy.p0i8.p0i8.i64(i8* align 2 %12, i8* align 2 %13, i64 2, i1 false)
  %14 = call contract float @_ZL18__bfloat162float__15__bang_bfloat16(%struct.__bang_bfloat16.0* byval(%struct.__bang_bfloat16.0) align 2 %7) #3
  store float %14, float* %4, align 4
  %15 = load float, float* %5, align 4
  %16 = load float, float* %3, align 4
  %17 = load float, float* %4, align 4
  %18 = call { float, float, float } asm "eq.gpr.f32 \09 $0, $1, $2; \0A\09", "=&r,=&r,=&r,0,1,2"(float %15, float %16, float %17) #4, !srcloc !5
  %19 = extractvalue { float, float, float } %18, 0
  %20 = extractvalue { float, float, float } %18, 1
  %21 = extractvalue { float, float, float } %18, 2
  store float %19, float* %5, align 4
  store float %20, float* %3, align 4
  store float %21, float* %4, align 4
  %22 = load i8, i8* %8, align 1
  %23 = trunc i8 %22 to i1
  %24 = load float, float* %5, align 4
  %25 = call i8 asm "cvt.gpr.tz.s8.f32 \09 $0, $1; \0A\09", "=r,r,0"(float %24, i1 %23) #4, !srcloc !6
  store i8 %25, i8* %8, align 1
  %26 = load i8, i8* %8, align 1
  %27 = trunc i8 %26 to i1
  ret i1 %27
}

; Function Attrs: alwaysinline convergent nounwind
define internal zeroext i1 @_ZL6__hneu15__bang_bfloat16S_(%struct.__bang_bfloat16.0* byval(%struct.__bang_bfloat16.0) align 2 %0, %struct.__bang_bfloat16.0* byval(%struct.__bang_bfloat16.0) align 2 %1) #0 {
  %3 = alloca float, align 4
  %4 = alloca float, align 4
  %5 = alloca float, align 4
  %6 = alloca %struct.__bang_bfloat16.0, align 2
  %7 = alloca %struct.__bang_bfloat16.0, align 2
  %8 = alloca i8, align 1
  %9 = bitcast %struct.__bang_bfloat16.0* %6 to i8*
  %10 = bitcast %struct.__bang_bfloat16.0* %0 to i8*
  call void @llvm.memcpy.p0i8.p0i8.i64(i8* align 2 %9, i8* align 2 %10, i64 2, i1 false)
  %11 = call contract float @_ZL18__bfloat162float__15__bang_bfloat16(%struct.__bang_bfloat16.0* byval(%struct.__bang_bfloat16.0) align 2 %6) #3
  store float %11, float* %3, align 4
  %12 = bitcast %struct.__bang_bfloat16.0* %7 to i8*
  %13 = bitcast %struct.__bang_bfloat16.0* %1 to i8*
  call void @llvm.memcpy.p0i8.p0i8.i64(i8* align 2 %12, i8* align 2 %13, i64 2, i1 false)
  %14 = call contract float @_ZL18__bfloat162float__15__bang_bfloat16(%struct.__bang_bfloat16.0* byval(%struct.__bang_bfloat16.0) align 2 %7) #3
  store float %14, float* %4, align 4
  %15 = load float, float* %5, align 4
  %16 = load float, float* %3, align 4
  %17 = load float, float* %4, align 4
  %18 = call { float, float, float } asm "neu.gpr.f32 \09 $0, $1, $2; \0A\09", "=&r,=&r,=&r,0,1,2"(float %15, float %16, float %17) #4, !srcloc !7
  %19 = extractvalue { float, float, float } %18, 0
  %20 = extractvalue { float, float, float } %18, 1
  %21 = extractvalue { float, float, float } %18, 2
  store float %19, float* %5, align 4
  store float %20, float* %3, align 4
  store float %21, float* %4, align 4
  %22 = load i8, i8* %8, align 1
  %23 = trunc i8 %22 to i1
  %24 = load float, float* %5, align 4
  %25 = call i8 asm "cvt.gpr.tz.s8.f32 \09 $0, $1; \0A\09", "=r,r,0"(float %24, i1 %23) #4, !srcloc !8
  store i8 %25, i8* %8, align 1
  %26 = load i8, i8* %8, align 1
  %27 = trunc i8 %26 to i1
  ret i1 %27
}

; Function Attrs: alwaysinline convergent nounwind
define internal zeroext i1 @_ZL5__hgt15__bang_bfloat16S_(%struct.__bang_bfloat16.0* byval(%struct.__bang_bfloat16.0) align 2 %0, %struct.__bang_bfloat16.0* byval(%struct.__bang_bfloat16.0) align 2 %1) #0 {
  %3 = alloca float, align 4
  %4 = alloca float, align 4
  %5 = alloca float, align 4
  %6 = alloca %struct.__bang_bfloat16.0, align 2
  %7 = alloca %struct.__bang_bfloat16.0, align 2
  %8 = alloca i8, align 1
  %9 = bitcast %struct.__bang_bfloat16.0* %6 to i8*
  %10 = bitcast %struct.__bang_bfloat16.0* %1 to i8*
  call void @llvm.memcpy.p0i8.p0i8.i64(i8* align 2 %9, i8* align 2 %10, i64 2, i1 false)
  %11 = call contract float @_ZL18__bfloat162float__15__bang_bfloat16(%struct.__bang_bfloat16.0* byval(%struct.__bang_bfloat16.0) align 2 %6) #3
  store float %11, float* %3, align 4
  %12 = bitcast %struct.__bang_bfloat16.0* %7 to i8*
  %13 = bitcast %struct.__bang_bfloat16.0* %0 to i8*
  call void @llvm.memcpy.p0i8.p0i8.i64(i8* align 2 %12, i8* align 2 %13, i64 2, i1 false)
  %14 = call contract float @_ZL18__bfloat162float__15__bang_bfloat16(%struct.__bang_bfloat16.0* byval(%struct.__bang_bfloat16.0) align 2 %7) #3
  store float %14, float* %4, align 4
  %15 = load float, float* %5, align 4
  %16 = load float, float* %3, align 4
  %17 = load float, float* %4, align 4
  %18 = call { float, float, float } asm "lt.gpr.f32 \09 $0, $1, $2; \0A\09", "=&r,=&r,=&r,0,1,2"(float %15, float %16, float %17) #4, !srcloc !9
  %19 = extractvalue { float, float, float } %18, 0
  %20 = extractvalue { float, float, float } %18, 1
  %21 = extractvalue { float, float, float } %18, 2
  store float %19, float* %5, align 4
  store float %20, float* %3, align 4
  store float %21, float* %4, align 4
  %22 = load i8, i8* %8, align 1
  %23 = trunc i8 %22 to i1
  %24 = load float, float* %5, align 4
  %25 = call i8 asm "cvt.gpr.tz.s8.f32 \09 $0, $1; \0A\09", "=r,r,0"(float %24, i1 %23) #4, !srcloc !10
  store i8 %25, i8* %8, align 1
  %26 = load i8, i8* %8, align 1
  %27 = trunc i8 %26 to i1
  ret i1 %27
}

; Function Attrs: alwaysinline convergent nounwind
define internal zeroext i1 @_ZL5__hlt15__bang_bfloat16S_(%struct.__bang_bfloat16.0* byval(%struct.__bang_bfloat16.0) align 2 %0, %struct.__bang_bfloat16.0* byval(%struct.__bang_bfloat16.0) align 2 %1) #0 {
  %3 = alloca float, align 4
  %4 = alloca float, align 4
  %5 = alloca float, align 4
  %6 = alloca %struct.__bang_bfloat16.0, align 2
  %7 = alloca %struct.__bang_bfloat16.0, align 2
  %8 = alloca i8, align 1
  %9 = bitcast %struct.__bang_bfloat16.0* %6 to i8*
  %10 = bitcast %struct.__bang_bfloat16.0* %0 to i8*
  call void @llvm.memcpy.p0i8.p0i8.i64(i8* align 2 %9, i8* align 2 %10, i64 2, i1 false)
  %11 = call contract float @_ZL18__bfloat162float__15__bang_bfloat16(%struct.__bang_bfloat16.0* byval(%struct.__bang_bfloat16.0) align 2 %6) #3
  store float %11, float* %3, align 4
  %12 = bitcast %struct.__bang_bfloat16.0* %7 to i8*
  %13 = bitcast %struct.__bang_bfloat16.0* %1 to i8*
  call void @llvm.memcpy.p0i8.p0i8.i64(i8* align 2 %12, i8* align 2 %13, i64 2, i1 false)
  %14 = call contract float @_ZL18__bfloat162float__15__bang_bfloat16(%struct.__bang_bfloat16.0* byval(%struct.__bang_bfloat16.0) align 2 %7) #3
  store float %14, float* %4, align 4
  %15 = load float, float* %5, align 4
  %16 = load float, float* %3, align 4
  %17 = load float, float* %4, align 4
  %18 = call { float, float, float } asm "lt.gpr.f32 \09 $0, $1, $2; \0A\09", "=&r,=&r,=&r,0,1,2"(float %15, float %16, float %17) #4, !srcloc !11
  %19 = extractvalue { float, float, float } %18, 0
  %20 = extractvalue { float, float, float } %18, 1
  %21 = extractvalue { float, float, float } %18, 2
  store float %19, float* %5, align 4
  store float %20, float* %3, align 4
  store float %21, float* %4, align 4
  %22 = load i8, i8* %8, align 1
  %23 = trunc i8 %22 to i1
  %24 = load float, float* %5, align 4
  %25 = call i8 asm "cvt.gpr.tz.s8.f32 \09 $0, $1; \0A\09", "=r,r,0"(float %24, i1 %23) #4, !srcloc !12
  store i8 %25, i8* %8, align 1
  %26 = load i8, i8* %8, align 1
  %27 = trunc i8 %26 to i1
  ret i1 %27
}

; Function Attrs: alwaysinline convergent nounwind
define internal zeroext i1 @_ZL5__hge15__bang_bfloat16S_(%struct.__bang_bfloat16.0* byval(%struct.__bang_bfloat16.0) align 2 %0, %struct.__bang_bfloat16.0* byval(%struct.__bang_bfloat16.0) align 2 %1) #0 {
  %3 = alloca float, align 4
  %4 = alloca float, align 4
  %5 = alloca float, align 4
  %6 = alloca %struct.__bang_bfloat16.0, align 2
  %7 = alloca %struct.__bang_bfloat16.0, align 2
  %8 = alloca i8, align 1
  %9 = bitcast %struct.__bang_bfloat16.0* %6 to i8*
  %10 = bitcast %struct.__bang_bfloat16.0* %0 to i8*
  call void @llvm.memcpy.p0i8.p0i8.i64(i8* align 2 %9, i8* align 2 %10, i64 2, i1 false)
  %11 = call contract float @_ZL18__bfloat162float__15__bang_bfloat16(%struct.__bang_bfloat16.0* byval(%struct.__bang_bfloat16.0) align 2 %6) #3
  store float %11, float* %3, align 4
  %12 = bitcast %struct.__bang_bfloat16.0* %7 to i8*
  %13 = bitcast %struct.__bang_bfloat16.0* %1 to i8*
  call void @llvm.memcpy.p0i8.p0i8.i64(i8* align 2 %12, i8* align 2 %13, i64 2, i1 false)
  %14 = call contract float @_ZL18__bfloat162float__15__bang_bfloat16(%struct.__bang_bfloat16.0* byval(%struct.__bang_bfloat16.0) align 2 %7) #3
  store float %14, float* %4, align 4
  %15 = load float, float* %5, align 4
  %16 = load float, float* %3, align 4
  %17 = load float, float* %4, align 4
  %18 = call { float, float, float } asm "ge.gpr.f32 \09 $0, $1, $2; \0A\09", "=&r,=&r,=&r,0,1,2"(float %15, float %16, float %17) #4, !srcloc !13
  %19 = extractvalue { float, float, float } %18, 0
  %20 = extractvalue { float, float, float } %18, 1
  %21 = extractvalue { float, float, float } %18, 2
  store float %19, float* %5, align 4
  store float %20, float* %3, align 4
  store float %21, float* %4, align 4
  %22 = load i8, i8* %8, align 1
  %23 = trunc i8 %22 to i1
  %24 = load float, float* %5, align 4
  %25 = call i8 asm "cvt.gpr.tz.s8.f32 \09 $0, $1; \0A\09", "=r,r,0"(float %24, i1 %23) #4, !srcloc !14
  store i8 %25, i8* %8, align 1
  %26 = load i8, i8* %8, align 1
  %27 = trunc i8 %26 to i1
  ret i1 %27
}

; Function Attrs: alwaysinline convergent nounwind
define internal zeroext i1 @_ZL5__hle15__bang_bfloat16S_(%struct.__bang_bfloat16.0* byval(%struct.__bang_bfloat16.0) align 2 %0, %struct.__bang_bfloat16.0* byval(%struct.__bang_bfloat16.0) align 2 %1) #0 {
  %3 = alloca float, align 4
  %4 = alloca float, align 4
  %5 = alloca float, align 4
  %6 = alloca %struct.__bang_bfloat16.0, align 2
  %7 = alloca %struct.__bang_bfloat16.0, align 2
  %8 = alloca i8, align 1
  %9 = alloca i8, align 1
  %10 = bitcast %struct.__bang_bfloat16.0* %6 to i8*
  %11 = bitcast %struct.__bang_bfloat16.0* %0 to i8*
  call void @llvm.memcpy.p0i8.p0i8.i64(i8* align 2 %10, i8* align 2 %11, i64 2, i1 false)
  %12 = call contract float @_ZL18__bfloat162float__15__bang_bfloat16(%struct.__bang_bfloat16.0* byval(%struct.__bang_bfloat16.0) align 2 %6) #3
  store float %12, float* %3, align 4
  %13 = bitcast %struct.__bang_bfloat16.0* %7 to i8*
  %14 = bitcast %struct.__bang_bfloat16.0* %1 to i8*
  call void @llvm.memcpy.p0i8.p0i8.i64(i8* align 2 %13, i8* align 2 %14, i64 2, i1 false)
  %15 = call contract float @_ZL18__bfloat162float__15__bang_bfloat16(%struct.__bang_bfloat16.0* byval(%struct.__bang_bfloat16.0) align 2 %7) #3
  store float %15, float* %4, align 4
  %16 = load float, float* %5, align 4
  %17 = load float, float* %3, align 4
  %18 = load float, float* %4, align 4
  %19 = call { float, float, float } asm "lt.gpr.f32 \09 $0, $1, $2; \0A\09", "=&r,=&r,=&r,0,1,2"(float %16, float %17, float %18) #4, !srcloc !15
  %20 = extractvalue { float, float, float } %19, 0
  %21 = extractvalue { float, float, float } %19, 1
  %22 = extractvalue { float, float, float } %19, 2
  store float %20, float* %5, align 4
  store float %21, float* %3, align 4
  store float %22, float* %4, align 4
  %23 = load i8, i8* %8, align 1
  %24 = trunc i8 %23 to i1
  %25 = load float, float* %5, align 4
  %26 = call i8 asm "cvt.gpr.tz.s8.f32 \09 $0, $1; \0A\09", "=r,r,0"(float %25, i1 %24) #4, !srcloc !16
  store i8 %26, i8* %8, align 1
  %27 = load float, float* %5, align 4
  %28 = load float, float* %3, align 4
  %29 = load float, float* %4, align 4
  %30 = call { float, float, float } asm "eq.gpr.f32 \09 $0, $1, $2; \0A\09", "=&r,=&r,=&r,0,1,2"(float %27, float %28, float %29) #4, !srcloc !17
  %31 = extractvalue { float, float, float } %30, 0
  %32 = extractvalue { float, float, float } %30, 1
  %33 = extractvalue { float, float, float } %30, 2
  store float %31, float* %5, align 4
  store float %32, float* %3, align 4
  store float %33, float* %4, align 4
  %34 = load i8, i8* %9, align 1
  %35 = trunc i8 %34 to i1
  %36 = load float, float* %5, align 4
  %37 = call i8 asm "cvt.gpr.tz.s8.f32 \09 $0, $1; \0A\09", "=r,r,0"(float %36, i1 %35) #4, !srcloc !18
  store i8 %37, i8* %9, align 1
  %38 = load i8, i8* %9, align 1
  %39 = trunc i8 %38 to i1
  br i1 %39, label %43, label %40

40:                                               ; preds = %2
  %41 = load i8, i8* %8, align 1
  %42 = trunc i8 %41 to i1
  br label %43

43:                                               ; preds = %40, %2
  %44 = phi i1 [ true, %2 ], [ %42, %40 ]
  ret i1 %44
}

; Function Attrs: convergent noinline nounwind optnone
define dso_local mlisa_kernel void @relocated(%struct.Payload* byval(%struct.Payload) align 8 %0) #2 {
  %2 = getelementptr inbounds %struct.Payload, %struct.Payload* %0, i32 0, i32 1
  %3 = getelementptr inbounds [2 x %struct.Leaf], [2 x %struct.Leaf]* %2, i64 0, i64 0
  %4 = getelementptr inbounds %struct.Leaf, %struct.Leaf* %3, i32 0, i32 0
  %5 = load i32*, i32** %4, align 8
  %6 = getelementptr inbounds i32, i32* %5, i64 0
  %7 = load i32, i32* %6, align 4
  %8 = getelementptr inbounds %struct.Payload, %struct.Payload* %0, i32 0, i32 1
  %9 = getelementptr inbounds [2 x %struct.Leaf], [2 x %struct.Leaf]* %8, i64 0, i64 0
  %10 = getelementptr inbounds %struct.Leaf, %struct.Leaf* %9, i32 0, i32 2
  %11 = load i32, i32* %10, align 8
  %12 = add nsw i32 %7, %11
  %13 = getelementptr inbounds %struct.Payload, %struct.Payload* %0, i32 0, i32 0
  %14 = load i8, i8* %13, align 8
  %15 = zext i8 %14 to i32
  %16 = add nsw i32 %12, %15
  %17 = getelementptr inbounds %struct.Payload, %struct.Payload* %0, i32 0, i32 2
  %18 = load i32*, i32** %17, align 8
  %19 = getelementptr inbounds i32, i32* %18, i64 0
  store i32 %16, i32* %19, align 4
  %20 = getelementptr inbounds %struct.Payload, %struct.Payload* %0, i32 0, i32 1
  %21 = getelementptr inbounds [2 x %struct.Leaf], [2 x %struct.Leaf]* %20, i64 0, i64 1
  %22 = getelementptr inbounds %struct.Leaf, %struct.Leaf* %21, i32 0, i32 0
  %23 = load i32*, i32** %22, align 8
  %24 = getelementptr inbounds i32, i32* %23, i64 0
  %25 = load i32, i32* %24, align 4
  %26 = getelementptr inbounds %struct.Payload, %struct.Payload* %0, i32 0, i32 1
  %27 = getelementptr inbounds [2 x %struct.Leaf], [2 x %struct.Leaf]* %26, i64 0, i64 1
  %28 = getelementptr inbounds %struct.Leaf, %struct.Leaf* %27, i32 0, i32 2
  %29 = load i32, i32* %28, align 8
  %30 = add nsw i32 %25, %29
  %31 = getelementptr inbounds %struct.Payload, %struct.Payload* %0, i32 0, i32 1
  %32 = getelementptr inbounds [2 x %struct.Leaf], [2 x %struct.Leaf]* %31, i64 0, i64 0
  %33 = getelementptr inbounds %struct.Leaf, %struct.Leaf* %32, i32 0, i32 1
  %34 = load i32*, i32** %33, align 8
  %35 = icmp ne i32* %34, null
  br i1 %35, label %36, label %43

36:                                               ; preds = %1
  %37 = getelementptr inbounds %struct.Payload, %struct.Payload* %0, i32 0, i32 1
  %38 = getelementptr inbounds [2 x %struct.Leaf], [2 x %struct.Leaf]* %37, i64 0, i64 0
  %39 = getelementptr inbounds %struct.Leaf, %struct.Leaf* %38, i32 0, i32 1
  %40 = load i32*, i32** %39, align 8
  %41 = getelementptr inbounds i32, i32* %40, i64 0
  %42 = load i32, i32* %41, align 4
  br label %44

43:                                               ; preds = %1
  br label %44

44:                                               ; preds = %43, %36
  %45 = phi i32 [ %42, %36 ], [ 0, %43 ]
  %46 = add nsw i32 %30, %45
  %47 = getelementptr inbounds %struct.Payload, %struct.Payload* %0, i32 0, i32 1
  %48 = getelementptr inbounds [2 x %struct.Leaf], [2 x %struct.Leaf]* %47, i64 0, i64 1
  %49 = getelementptr inbounds %struct.Leaf, %struct.Leaf* %48, i32 0, i32 1
  %50 = load i32*, i32** %49, align 8
  %51 = icmp ne i32* %50, null
  br i1 %51, label %52, label %59

52:                                               ; preds = %44
  %53 = getelementptr inbounds %struct.Payload, %struct.Payload* %0, i32 0, i32 1
  %54 = getelementptr inbounds [2 x %struct.Leaf], [2 x %struct.Leaf]* %53, i64 0, i64 1
  %55 = getelementptr inbounds %struct.Leaf, %struct.Leaf* %54, i32 0, i32 1
  %56 = load i32*, i32** %55, align 8
  %57 = getelementptr inbounds i32, i32* %56, i64 0
  %58 = load i32, i32* %57, align 4
  br label %60

59:                                               ; preds = %44
  br label %60

60:                                               ; preds = %59, %52
  %61 = phi i32 [ %58, %52 ], [ 0, %59 ]
  %62 = add nsw i32 %46, %61
  %63 = getelementptr inbounds %struct.Payload, %struct.Payload* %0, i32 0, i32 2
  %64 = load i32*, i32** %63, align 8
  %65 = getelementptr inbounds i32, i32* %64, i64 1
  store i32 %62, i32* %65, align 4
  %66 = getelementptr inbounds %struct.Payload, %struct.Payload* %0, i32 0, i32 3
  %67 = load i64, i64* %66, align 8
  %68 = and i64 %67, 65535
  %69 = trunc i64 %68 to i32
  %70 = getelementptr inbounds %struct.Payload, %struct.Payload* %0, i32 0, i32 2
  %71 = load i32*, i32** %70, align 8
  %72 = getelementptr inbounds i32, i32* %71, i64 2
  store i32 %69, i32* %72, align 4
  ret void
}

; Function Attrs: convergent noinline nounwind optnone
define dso_local mlisa_kernel void @relocation_no_args() #2 {
  ret void
}

; Function Attrs: alwaysinline convergent nounwind
define linkonce_odr dso_local void @_ZN15__bang_bfloat16C1Ev(%struct.__bang_bfloat16.0* %0) unnamed_addr #0 comdat align 2 {
  %2 = alloca %struct.__bang_bfloat16.0*, align 8
  %3 = alloca %struct.__bang_bfloat16.0*, align 8
  store %struct.__bang_bfloat16.0* %0, %struct.__bang_bfloat16.0** %3, align 8
  %4 = load %struct.__bang_bfloat16.0*, %struct.__bang_bfloat16.0** %3, align 8
  store %struct.__bang_bfloat16.0* %4, %struct.__bang_bfloat16.0** %2, align 8
  %5 = load %struct.__bang_bfloat16.0*, %struct.__bang_bfloat16.0** %2, align 8
  ret void
}

; Function Attrs: alwaysinline convergent nounwind
define internal float @_ZL18__bfloat162float__15__bang_bfloat16(%struct.__bang_bfloat16.0* byval(%struct.__bang_bfloat16.0) align 2 %0) #0 {
  %2 = alloca %struct.__bang_bfloat16_raw, align 2
  %3 = call %struct.__bang_bfloat16_raw @_ZNK15__bang_bfloat16cv19__bang_bfloat16_rawEv(%struct.__bang_bfloat16.0* %0) #3
  %4 = getelementptr inbounds %struct.__bang_bfloat16_raw, %struct.__bang_bfloat16_raw* %2, i32 0, i32 0
  %5 = extractvalue %struct.__bang_bfloat16_raw %3, 0
  store i16 %5, i16* %4, align 2
  %6 = getelementptr inbounds %struct.__bang_bfloat16_raw, %struct.__bang_bfloat16_raw* %2, i32 0, i32 0
  %7 = load i16, i16* %6, align 2
  %8 = call contract float @_ZL25__internal_bfloat162floatt(i16 zeroext %7) #3
  ret float %8
}

; Function Attrs: alwaysinline convergent nounwind
define internal %struct.__bang_bfloat16.0 @_ZL18__float2bfloat16__f(float %0) #0 {
  %2 = alloca %struct.__bang_bfloat16.0, align 2
  %3 = alloca float, align 4
  %4 = alloca %struct.__bang_bfloat16_raw, align 2
  %5 = alloca i32, align 4
  %6 = alloca i32, align 4
  store float %0, float* %3, align 4
  call void @_ZN15__bang_bfloat16C1Ev(%struct.__bang_bfloat16.0* %2) #3
  store i32 0, i32* %5, align 4
  store i32 0, i32* %6, align 4
  %7 = load float, float* %3, align 4
  %8 = call zeroext i16 @_ZL25__internal_float2bfloat16fRjS_(float %7, i32* nonnull align 4 dereferenceable(4) %5, i32* nonnull align 4 dereferenceable(4) %6) #3
  %9 = getelementptr inbounds %struct.__bang_bfloat16_raw, %struct.__bang_bfloat16_raw* %4, i32 0, i32 0
  store i16 %8, i16* %9, align 2
  %10 = load i32, i32* %6, align 4
  %11 = icmp ugt i32 %10, -2147483648
  br i1 %11, label %21, label %12

12:                                               ; preds = %1
  %13 = load i32, i32* %6, align 4
  %14 = icmp eq i32 %13, -2147483648
  br i1 %14, label %15, label %25

15:                                               ; preds = %12
  %16 = getelementptr inbounds %struct.__bang_bfloat16_raw, %struct.__bang_bfloat16_raw* %4, i32 0, i32 0
  %17 = load i16, i16* %16, align 2
  %18 = zext i16 %17 to i32
  %19 = and i32 %18, 1
  %20 = icmp ne i32 %19, 0
  br i1 %20, label %21, label %25

21:                                               ; preds = %15, %1
  %22 = getelementptr inbounds %struct.__bang_bfloat16_raw, %struct.__bang_bfloat16_raw* %4, i32 0, i32 0
  %23 = load i16, i16* %22, align 2
  %24 = add i16 %23, 1
  store i16 %24, i16* %22, align 2
  br label %25

25:                                               ; preds = %21, %15, %12
  %26 = call nonnull align 2 dereferenceable(2) %struct.__bang_bfloat16.0* @_ZN15__bang_bfloat16aSERK19__bang_bfloat16_raw(%struct.__bang_bfloat16.0* %2, %struct.__bang_bfloat16_raw* nonnull align 2 dereferenceable(2) %4) #3
  %27 = load %struct.__bang_bfloat16.0, %struct.__bang_bfloat16.0* %2, align 2
  ret %struct.__bang_bfloat16.0 %27
}

; Function Attrs: alwaysinline convergent nounwind
define internal float @_ZL25__internal_bfloat162floatt(i16 zeroext %0) #0 {
  %2 = alloca i16, align 2
  %3 = alloca float, align 4
  store i16 %0, i16* %2, align 2
  %4 = load i16, i16* %2, align 2
  %5 = call contract float @_ZL32__internal_device_bfloat162floatt(i16 zeroext %4) #3
  store float %5, float* %3, align 4
  %6 = load float, float* %3, align 4
  ret float %6
}

; Function Attrs: alwaysinline convergent nounwind
define linkonce_odr dso_local %struct.__bang_bfloat16_raw @_ZNK15__bang_bfloat16cv19__bang_bfloat16_rawEv(%struct.__bang_bfloat16.0* %0) #0 comdat align 2 {
  %2 = alloca %struct.__bang_bfloat16_raw, align 2
  %3 = alloca %struct.__bang_bfloat16.0*, align 8
  store %struct.__bang_bfloat16.0* %0, %struct.__bang_bfloat16.0** %3, align 8
  %4 = load %struct.__bang_bfloat16.0*, %struct.__bang_bfloat16.0** %3, align 8
  %5 = getelementptr inbounds %struct.__bang_bfloat16.0, %struct.__bang_bfloat16.0* %4, i32 0, i32 0
  %6 = load i16, i16* %5, align 2
  %7 = getelementptr inbounds %struct.__bang_bfloat16_raw, %struct.__bang_bfloat16_raw* %2, i32 0, i32 0
  store i16 %6, i16* %7, align 2
  %8 = load %struct.__bang_bfloat16_raw, %struct.__bang_bfloat16_raw* %2, align 2
  ret %struct.__bang_bfloat16_raw %8
}

; Function Attrs: alwaysinline convergent nounwind
define internal float @_ZL32__internal_device_bfloat162floatt(i16 zeroext %0) #0 {
  %2 = alloca i16, align 2
  %3 = alloca float, align 4
  store i16 %0, i16* %2, align 2
  %4 = load float, float* %3, align 4
  %5 = load i16, i16* %2, align 2
  %6 = call contract float asm "sll.gpr.u32 \09 $0, $1, 16;\0A\09", "=r,r,0"(i16 %5, float %4) #4, !srcloc !19
  store float %6, float* %3, align 4
  %7 = load float, float* %3, align 4
  ret float %7
}

; Function Attrs: alwaysinline convergent nounwind
define internal zeroext i16 @_ZL25__internal_float2bfloat16fRjS_(float %0, i32* nonnull align 4 dereferenceable(4) %1, i32* nonnull align 4 dereferenceable(4) %2) #0 {
  %4 = alloca i16, align 2
  %5 = alloca float, align 4
  %6 = alloca i32*, align 8
  %7 = alloca i32*, align 8
  %8 = alloca i32, align 4
  store float %0, float* %5, align 4
  store i32* %1, i32** %6, align 8
  store i32* %2, i32** %7, align 8
  %9 = load float, float* %5, align 4
  %10 = call i32 @_ZL24__internal_float_as_uintf(float %9) #3
  store i32 %10, i32* %8, align 4
  %11 = load i32, i32* %8, align 4
  %12 = and i32 %11, 2147483647
  %13 = icmp ugt i32 %12, 2139095040
  br i1 %13, label %14, label %17

14:                                               ; preds = %3
  %15 = load i32*, i32** %6, align 8
  store i32 0, i32* %15, align 4
  %16 = load i32*, i32** %7, align 8
  store i32 0, i32* %16, align 4
  store i16 32767, i16* %4, align 2
  br label %27

17:                                               ; preds = %3
  %18 = load i32, i32* %8, align 4
  %19 = lshr i32 %18, 31
  %20 = load i32*, i32** %6, align 8
  store i32 %19, i32* %20, align 4
  %21 = load i32, i32* %8, align 4
  %22 = shl i32 %21, 16
  %23 = load i32*, i32** %7, align 8
  store i32 %22, i32* %23, align 4
  %24 = load i32, i32* %8, align 4
  %25 = lshr i32 %24, 16
  %26 = trunc i32 %25 to i16
  store i16 %26, i16* %4, align 2
  br label %27

27:                                               ; preds = %17, %14
  %28 = load i16, i16* %4, align 2
  ret i16 %28
}

; Function Attrs: alwaysinline convergent nounwind
define linkonce_odr dso_local nonnull align 2 dereferenceable(2) %struct.__bang_bfloat16.0* @_ZN15__bang_bfloat16aSERK19__bang_bfloat16_raw(%struct.__bang_bfloat16.0* %0, %struct.__bang_bfloat16_raw* nonnull align 2 dereferenceable(2) %1) #0 comdat align 2 {
  %3 = alloca %struct.__bang_bfloat16.0*, align 8
  %4 = alloca %struct.__bang_bfloat16_raw*, align 8
  store %struct.__bang_bfloat16.0* %0, %struct.__bang_bfloat16.0** %3, align 8
  store %struct.__bang_bfloat16_raw* %1, %struct.__bang_bfloat16_raw** %4, align 8
  %5 = load %struct.__bang_bfloat16.0*, %struct.__bang_bfloat16.0** %3, align 8
  %6 = load %struct.__bang_bfloat16_raw*, %struct.__bang_bfloat16_raw** %4, align 8
  %7 = getelementptr inbounds %struct.__bang_bfloat16_raw, %struct.__bang_bfloat16_raw* %6, i32 0, i32 0
  %8 = load i16, i16* %7, align 2
  %9 = getelementptr inbounds %struct.__bang_bfloat16.0, %struct.__bang_bfloat16.0* %5, i32 0, i32 0
  store i16 %8, i16* %9, align 2
  ret %struct.__bang_bfloat16.0* %5
}

; Function Attrs: alwaysinline convergent nounwind
define internal i32 @_ZL24__internal_float_as_uintf(float %0) #0 {
  %2 = alloca float, align 4
  %3 = alloca i32, align 4
  store float %0, float* %2, align 4
  %4 = bitcast float* %2 to i32*
  %5 = load i32, i32* %4, align 4
  store i32 %5, i32* %3, align 4
  %6 = load i32, i32* %3, align 4
  ret i32 %6
}

attributes #0 = { alwaysinline convergent nounwind "correctly-rounded-divide-sqrt-fp-math"="false" "disable-tail-calls"="false" "frame-pointer"="all" "less-precise-fpmad"="false" "min-legal-vector-width"="0" "no-infs-fp-math"="false" "no-jump-tables"="false" "no-nans-fp-math"="false" "no-signed-zeros-fp-math"="false" "no-trapping-math"="true" "stack-protector-buffer-size"="8" "target-cpu"="mtp_372" "unsafe-fp-math"="false" }
attributes #1 = { argmemonly nounwind willreturn }
attributes #2 = { convergent noinline nounwind optnone "correctly-rounded-divide-sqrt-fp-math"="false" "disable-tail-calls"="false" "frame-pointer"="all" "less-precise-fpmad"="false" "min-legal-vector-width"="0" "no-infs-fp-math"="false" "no-jump-tables"="false" "no-nans-fp-math"="false" "no-signed-zeros-fp-math"="false" "no-trapping-math"="true" "stack-protector-buffer-size"="8" "target-cpu"="mtp_372" "unsafe-fp-math"="false" }
attributes #3 = { convergent nounwind }
attributes #4 = { convergent nounwind readnone }

!mlvm.annotations = !{!0, !1}
!llvm.module.flags = !{!2}
!llvm.ident = !{!3, !3}

!0 = !{void (%struct.Payload*)* @relocated, !"kernel", i32 1}
!1 = !{void ()* @relocation_no_args, !"kernel", i32 1}
!2 = !{i32 1, !"wchar_size", i32 4}
!3 = !{!"cncc 4.5.1 clang version 11.1.0"}
!4 = !{i32 202798, i32 202832}
!5 = !{i32 -2147270333, i32 -2147270768}
!6 = !{i32 -2147270653, i32 -2147270613}
!7 = !{i32 -2147264479, i32 -2147264914}
!8 = !{i32 -2147264799, i32 -2147264759}
!9 = !{i32 -2147266410, i32 -2147266845}
!10 = !{i32 -2147266730, i32 -2147266690}
!11 = !{i32 -2147267375, i32 -2147267810}
!12 = !{i32 -2147267695, i32 -2147267655}
!13 = !{i32 -2147268340, i32 -2147268775}
!14 = !{i32 -2147268660, i32 -2147268620}
!15 = !{i32 198991, i32 199034}
!16 = !{i32 199114, i32 199154}
!17 = !{i32 199235, i32 199278}
!18 = !{i32 199358, i32 199398}
!19 = !{i32 188450, i32 188487}
