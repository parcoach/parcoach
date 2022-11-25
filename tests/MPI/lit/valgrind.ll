; REQUIRES: valgrind
; RUN: %valgrind --leak-check=full %parcoach -check=mpi -o /dev/null %s 2>&1 | %filecheck %s
; CHECK: warning: MPI_Reduce line {{[0-9]+}} possibly not called by all processes because of conditional(s) line(s)  {{[0-9]+}}
; CHECK: definitely lost: 0 bytes
; CHECK: indirectly lost: 0 bytes
; CHECK: possibly lost: 0 bytes
%struct.ompi_predefined_datatype_t = type opaque
%struct.ompi_predefined_op_t = type opaque
%struct.ompi_predefined_communicator_t = type opaque

@ompi_mpi_int = external global %struct.ompi_predefined_datatype_t, align 1
@ompi_mpi_op_sum = external global %struct.ompi_predefined_op_t, align 1
@ompi_mpi_comm_world = external global %struct.ompi_predefined_communicator_t, align 1

; Function Attrs: nounwind uwtable
define dso_local void @g(i32 noundef %0) local_unnamed_addr #0 !dbg !21 {
  %2 = alloca i32, align 4
  %3 = alloca i32, align 4
  call void @llvm.dbg.value(metadata i32 %0, metadata !26, metadata !DIExpression()), !dbg !29
  call void @llvm.lifetime.start.p0(i64 4, ptr nonnull %2) #4, !dbg !30
  call void @llvm.dbg.value(metadata i32 0, metadata !27, metadata !DIExpression()), !dbg !29
  store i32 0, ptr %2, align 4, !dbg !31, !tbaa !32
  call void @llvm.lifetime.start.p0(i64 4, ptr nonnull %3) #4, !dbg !36
  call void @llvm.dbg.value(metadata i32 12, metadata !28, metadata !DIExpression()), !dbg !29
  store i32 12, ptr %3, align 4, !dbg !37, !tbaa !32
  %4 = icmp sgt i32 %0, 256, !dbg !38
  br i1 %4, label %5, label %7, !dbg !40

5:                                                ; preds = %1
  call void @llvm.dbg.value(metadata ptr %2, metadata !27, metadata !DIExpression(DW_OP_deref)), !dbg !29
  call void @llvm.dbg.value(metadata ptr %3, metadata !28, metadata !DIExpression(DW_OP_deref)), !dbg !29
  %6 = call i32 @MPI_Reduce(ptr noundef nonnull %3, ptr noundef nonnull %2, i32 noundef 1, ptr noundef nonnull @ompi_mpi_int, ptr noundef nonnull @ompi_mpi_op_sum, i32 noundef 0, ptr noundef nonnull @ompi_mpi_comm_world) #4, !dbg !41
  br label %7, !dbg !41

7:                                                ; preds = %5, %1
  call void @llvm.lifetime.end.p0(i64 4, ptr nonnull %3) #4, !dbg !42
  call void @llvm.lifetime.end.p0(i64 4, ptr nonnull %2) #4, !dbg !42
  ret void, !dbg !42
}

; Function Attrs: argmemonly mustprogress nocallback nofree nosync nounwind willreturn
declare void @llvm.lifetime.start.p0(i64 immarg, ptr nocapture) #1

declare !dbg !43 i32 @MPI_Reduce(ptr noundef, ptr noundef, i32 noundef, ptr noundef, ptr noundef, i32 noundef, ptr noundef) local_unnamed_addr #2

; Function Attrs: argmemonly mustprogress nocallback nofree nosync nounwind willreturn
declare void @llvm.lifetime.end.p0(i64 immarg, ptr nocapture) #1

; Function Attrs: nounwind uwtable
define dso_local void @f() local_unnamed_addr #0 !dbg !49 {
  %1 = alloca i32, align 4
  %2 = alloca i32, align 4
  %3 = alloca i32, align 4
  %4 = alloca i32, align 4
  call void @llvm.lifetime.start.p0(i64 4, ptr nonnull %3) #4, !dbg !56
  call void @llvm.lifetime.start.p0(i64 4, ptr nonnull %4) #4, !dbg !56
  call void @llvm.dbg.value(metadata ptr %4, metadata !54, metadata !DIExpression(DW_OP_deref)), !dbg !57
  %5 = call i32 @MPI_Comm_rank(ptr noundef nonnull @ompi_mpi_comm_world, ptr noundef nonnull %4) #4, !dbg !58
  call void @llvm.dbg.value(metadata ptr %3, metadata !53, metadata !DIExpression(DW_OP_deref)), !dbg !57
  %6 = call i32 @MPI_Comm_size(ptr noundef nonnull @ompi_mpi_comm_world, ptr noundef nonnull %3) #4, !dbg !59
  %7 = load i32, ptr %4, align 4, !dbg !60, !tbaa !32
  call void @llvm.dbg.value(metadata i32 %7, metadata !54, metadata !DIExpression()), !dbg !57
  %8 = and i32 %7, 1, !dbg !62
  %9 = icmp eq i32 %8, 0, !dbg !62
  call void @llvm.dbg.value(metadata i32 undef, metadata !55, metadata !DIExpression()), !dbg !57
  br i1 %9, label %16, label %10, !dbg !63

10:                                               ; preds = %0
  %11 = load i32, ptr %3, align 4, !dbg !64, !tbaa !32
  call void @llvm.dbg.value(metadata i32 %11, metadata !53, metadata !DIExpression()), !dbg !57
  call void @llvm.dbg.value(metadata i32 %11, metadata !26, metadata !DIExpression()), !dbg !66
  call void @llvm.lifetime.start.p0(i64 4, ptr nonnull %1) #4, !dbg !68
  call void @llvm.dbg.value(metadata i32 0, metadata !27, metadata !DIExpression()), !dbg !66
  store i32 0, ptr %1, align 4, !dbg !69, !tbaa !32
  call void @llvm.lifetime.start.p0(i64 4, ptr nonnull %2) #4, !dbg !70
  call void @llvm.dbg.value(metadata i32 12, metadata !28, metadata !DIExpression()), !dbg !66
  store i32 12, ptr %2, align 4, !dbg !71, !tbaa !32
  %12 = icmp sgt i32 %11, 256, !dbg !72
  br i1 %12, label %13, label %15, !dbg !73

13:                                               ; preds = %10
  call void @llvm.dbg.value(metadata ptr %1, metadata !27, metadata !DIExpression(DW_OP_deref)), !dbg !66
  call void @llvm.dbg.value(metadata ptr %2, metadata !28, metadata !DIExpression(DW_OP_deref)), !dbg !66
  %14 = call i32 @MPI_Reduce(ptr noundef nonnull %2, ptr noundef nonnull %1, i32 noundef 1, ptr noundef nonnull @ompi_mpi_int, ptr noundef nonnull @ompi_mpi_op_sum, i32 noundef 0, ptr noundef nonnull @ompi_mpi_comm_world) #4, !dbg !74
  br label %15, !dbg !74

15:                                               ; preds = %10, %13
  call void @llvm.lifetime.end.p0(i64 4, ptr nonnull %2) #4, !dbg !75
  call void @llvm.lifetime.end.p0(i64 4, ptr nonnull %1) #4, !dbg !75
  br label %16, !dbg !76

16:                                               ; preds = %15, %0
  %17 = call i32 @MPI_Barrier(ptr noundef nonnull @ompi_mpi_comm_world) #4, !dbg !77
  call void @llvm.lifetime.end.p0(i64 4, ptr nonnull %4) #4, !dbg !78
  call void @llvm.lifetime.end.p0(i64 4, ptr nonnull %3) #4, !dbg !78
  ret void, !dbg !78
}

declare !dbg !79 i32 @MPI_Comm_rank(ptr noundef, ptr noundef) local_unnamed_addr #2

declare !dbg !83 i32 @MPI_Comm_size(ptr noundef, ptr noundef) local_unnamed_addr #2

declare !dbg !84 i32 @MPI_Barrier(ptr noundef) local_unnamed_addr #2

; Function Attrs: nounwind uwtable
define dso_local i32 @main(i32 noundef %0, ptr noundef %1) local_unnamed_addr #0 !dbg !87 {
  %3 = alloca i32, align 4
  %4 = alloca i32, align 4
  %5 = alloca i32, align 4
  %6 = alloca i32, align 4
  %7 = alloca i32, align 4
  %8 = alloca ptr, align 8
  call void @llvm.dbg.value(metadata i32 %0, metadata !94, metadata !DIExpression()), !dbg !96
  store i32 %0, ptr %7, align 4, !tbaa !32
  call void @llvm.dbg.value(metadata ptr %1, metadata !95, metadata !DIExpression()), !dbg !96
  store ptr %1, ptr %8, align 8, !tbaa !97
  call void @llvm.dbg.value(metadata ptr %7, metadata !94, metadata !DIExpression(DW_OP_deref)), !dbg !96
  call void @llvm.dbg.value(metadata ptr %8, metadata !95, metadata !DIExpression(DW_OP_deref)), !dbg !96
  %9 = call i32 @MPI_Init(ptr noundef nonnull %7, ptr noundef nonnull %8) #4, !dbg !99
  call void @llvm.lifetime.start.p0(i64 4, ptr nonnull %5) #4, !dbg !100
  call void @llvm.lifetime.start.p0(i64 4, ptr nonnull %6) #4, !dbg !100
  call void @llvm.dbg.value(metadata ptr %6, metadata !54, metadata !DIExpression(DW_OP_deref)), !dbg !102
  %10 = call i32 @MPI_Comm_rank(ptr noundef nonnull @ompi_mpi_comm_world, ptr noundef nonnull %6) #4, !dbg !103
  call void @llvm.dbg.value(metadata ptr %5, metadata !53, metadata !DIExpression(DW_OP_deref)), !dbg !102
  %11 = call i32 @MPI_Comm_size(ptr noundef nonnull @ompi_mpi_comm_world, ptr noundef nonnull %5) #4, !dbg !104
  %12 = load i32, ptr %6, align 4, !dbg !105, !tbaa !32
  call void @llvm.dbg.value(metadata i32 %12, metadata !54, metadata !DIExpression()), !dbg !102
  %13 = and i32 %12, 1, !dbg !106
  %14 = icmp eq i32 %13, 0, !dbg !106
  call void @llvm.dbg.value(metadata i32 undef, metadata !55, metadata !DIExpression()), !dbg !102
  br i1 %14, label %21, label %15, !dbg !107

15:                                               ; preds = %2
  %16 = load i32, ptr %5, align 4, !dbg !108, !tbaa !32
  call void @llvm.dbg.value(metadata i32 %16, metadata !53, metadata !DIExpression()), !dbg !102
  call void @llvm.dbg.value(metadata i32 %16, metadata !26, metadata !DIExpression()), !dbg !109
  call void @llvm.lifetime.start.p0(i64 4, ptr nonnull %3) #4, !dbg !111
  call void @llvm.dbg.value(metadata i32 0, metadata !27, metadata !DIExpression()), !dbg !109
  store i32 0, ptr %3, align 4, !dbg !112, !tbaa !32
  call void @llvm.lifetime.start.p0(i64 4, ptr nonnull %4) #4, !dbg !113
  call void @llvm.dbg.value(metadata i32 12, metadata !28, metadata !DIExpression()), !dbg !109
  store i32 12, ptr %4, align 4, !dbg !114, !tbaa !32
  %17 = icmp sgt i32 %16, 256, !dbg !115
  br i1 %17, label %18, label %20, !dbg !116

18:                                               ; preds = %15
  call void @llvm.dbg.value(metadata ptr %3, metadata !27, metadata !DIExpression(DW_OP_deref)), !dbg !109
  call void @llvm.dbg.value(metadata ptr %4, metadata !28, metadata !DIExpression(DW_OP_deref)), !dbg !109
  %19 = call i32 @MPI_Reduce(ptr noundef nonnull %4, ptr noundef nonnull %3, i32 noundef 1, ptr noundef nonnull @ompi_mpi_int, ptr noundef nonnull @ompi_mpi_op_sum, i32 noundef 0, ptr noundef nonnull @ompi_mpi_comm_world) #4, !dbg !117
  br label %20, !dbg !117

20:                                               ; preds = %18, %15
  call void @llvm.lifetime.end.p0(i64 4, ptr nonnull %4) #4, !dbg !118
  call void @llvm.lifetime.end.p0(i64 4, ptr nonnull %3) #4, !dbg !118
  br label %21, !dbg !119

21:                                               ; preds = %2, %20
  %22 = call i32 @MPI_Barrier(ptr noundef nonnull @ompi_mpi_comm_world) #4, !dbg !120
  call void @llvm.lifetime.end.p0(i64 4, ptr nonnull %6) #4, !dbg !121
  call void @llvm.lifetime.end.p0(i64 4, ptr nonnull %5) #4, !dbg !121
  %23 = call i32 @MPI_Finalize() #4, !dbg !122
  ret i32 0, !dbg !123
}

declare !dbg !124 i32 @MPI_Init(ptr noundef, ptr noundef) local_unnamed_addr #2

declare !dbg !128 i32 @MPI_Finalize() local_unnamed_addr #2

; Function Attrs: nocallback nofree nosync nounwind readnone speculatable willreturn
declare void @llvm.dbg.value(metadata, metadata, metadata) #3

attributes #0 = { nounwind uwtable "frame-pointer"="none" "min-legal-vector-width"="0" "no-trapping-math"="true" "stack-protector-buffer-size"="8" "target-cpu"="x86-64" "target-features"="+cx8,+fxsr,+mmx,+sse,+sse2,+x87" "tune-cpu"="generic" }
attributes #1 = { argmemonly mustprogress nocallback nofree nosync nounwind willreturn }
attributes #2 = { "frame-pointer"="none" "no-trapping-math"="true" "stack-protector-buffer-size"="8" "target-cpu"="x86-64" "target-features"="+cx8,+fxsr,+mmx,+sse,+sse2,+x87" "tune-cpu"="generic" }
attributes #3 = { nocallback nofree nosync nounwind readnone speculatable willreturn }
attributes #4 = { nounwind }

!llvm.dbg.cu = !{!0}
!llvm.module.flags = !{!14, !15, !16, !17, !18, !19}
!llvm.ident = !{!20}

!0 = distinct !DICompileUnit(language: DW_LANG_C99, file: !1, producer: "Debian clang version 15.0.4", isOptimized: true, runtimeVersion: 0, emissionKind: FullDebug, retainedTypes: !2, splitDebugInlining: false, nameTableKind: None)
!1 = !DIFile(filename: "../tests/MPI/basic/src/MPIexample.c", directory: "/home/fifi/dev/parcoach/build", checksumkind: CSK_MD5, checksum: "7f75d535485bdc75f1f8ab50e86404a0")
!2 = !{!3, !7, !8, !11}
!3 = !DIDerivedType(tag: DW_TAG_typedef, name: "MPI_Datatype", file: !4, line: 420, baseType: !5)
!4 = !DIFile(filename: "/usr/lib/x86_64-linux-gnu/openmpi/include/mpi.h", directory: "", checksumkind: CSK_MD5, checksum: "18ea94113a9892f1cf9b8c57c42f0ffc")
!5 = !DIDerivedType(tag: DW_TAG_pointer_type, baseType: !6, size: 64)
!6 = !DICompositeType(tag: DW_TAG_structure_type, name: "ompi_datatype_t", file: !4, line: 420, flags: DIFlagFwdDecl)
!7 = !DIDerivedType(tag: DW_TAG_pointer_type, baseType: null, size: 64)
!8 = !DIDerivedType(tag: DW_TAG_typedef, name: "MPI_Op", file: !4, line: 425, baseType: !9)
!9 = !DIDerivedType(tag: DW_TAG_pointer_type, baseType: !10, size: 64)
!10 = !DICompositeType(tag: DW_TAG_structure_type, name: "ompi_op_t", file: !4, line: 425, flags: DIFlagFwdDecl)
!11 = !DIDerivedType(tag: DW_TAG_typedef, name: "MPI_Comm", file: !4, line: 419, baseType: !12)
!12 = !DIDerivedType(tag: DW_TAG_pointer_type, baseType: !13, size: 64)
!13 = !DICompositeType(tag: DW_TAG_structure_type, name: "ompi_communicator_t", file: !4, line: 419, flags: DIFlagFwdDecl)
!14 = !{i32 7, !"Dwarf Version", i32 5}
!15 = !{i32 2, !"Debug Info Version", i32 3}
!16 = !{i32 1, !"wchar_size", i32 4}
!17 = !{i32 7, !"PIC Level", i32 2}
!18 = !{i32 7, !"PIE Level", i32 2}
!19 = !{i32 7, !"uwtable", i32 2}
!20 = !{!"Debian clang version 15.0.4"}
!21 = distinct !DISubprogram(name: "g", scope: !1, file: !1, line: 5, type: !22, scopeLine: 5, flags: DIFlagPrototyped | DIFlagAllCallsDescribed, spFlags: DISPFlagDefinition | DISPFlagOptimized, unit: !0, retainedNodes: !25)
!22 = !DISubroutineType(types: !23)
!23 = !{null, !24}
!24 = !DIBasicType(name: "int", size: 32, encoding: DW_ATE_signed)
!25 = !{!26, !27, !28}
!26 = !DILocalVariable(name: "s", arg: 1, scope: !21, file: !1, line: 5, type: !24)
!27 = !DILocalVariable(name: "res", scope: !21, file: !1, line: 6, type: !24)
!28 = !DILocalVariable(name: "i", scope: !21, file: !1, line: 7, type: !24)
!29 = !DILocation(line: 0, scope: !21)
!30 = !DILocation(line: 6, column: 3, scope: !21)
!31 = !DILocation(line: 6, column: 7, scope: !21)
!32 = !{!33, !33, i64 0}
!33 = !{!"int", !34, i64 0}
!34 = !{!"omnipotent char", !35, i64 0}
!35 = !{!"Simple C/C++ TBAA"}
!36 = !DILocation(line: 7, column: 3, scope: !21)
!37 = !DILocation(line: 7, column: 7, scope: !21)
!38 = !DILocation(line: 9, column: 9, scope: !39)
!39 = distinct !DILexicalBlock(scope: !21, file: !1, line: 9, column: 7)
!40 = !DILocation(line: 9, column: 7, scope: !21)
!41 = !DILocation(line: 10, column: 5, scope: !39)
!42 = !DILocation(line: 11, column: 1, scope: !21)
!43 = !DISubprogram(name: "MPI_Reduce", scope: !4, file: !4, line: 1725, type: !44, flags: DIFlagPrototyped, spFlags: DISPFlagOptimized, retainedNodes: !48)
!44 = !DISubroutineType(types: !45)
!45 = !{!24, !46, !7, !24, !3, !8, !24, !11}
!46 = !DIDerivedType(tag: DW_TAG_pointer_type, baseType: !47, size: 64)
!47 = !DIDerivedType(tag: DW_TAG_const_type, baseType: null)
!48 = !{}
!49 = distinct !DISubprogram(name: "f", scope: !1, file: !1, line: 13, type: !50, scopeLine: 13, flags: DIFlagAllCallsDescribed, spFlags: DISPFlagDefinition | DISPFlagOptimized, unit: !0, retainedNodes: !52)
!50 = !DISubroutineType(types: !51)
!51 = !{null}
!52 = !{!53, !54, !55}
!53 = !DILocalVariable(name: "s", scope: !49, file: !1, line: 14, type: !24)
!54 = !DILocalVariable(name: "r", scope: !49, file: !1, line: 14, type: !24)
!55 = !DILocalVariable(name: "n", scope: !49, file: !1, line: 14, type: !24)
!56 = !DILocation(line: 14, column: 3, scope: !49)
!57 = !DILocation(line: 0, scope: !49)
!58 = !DILocation(line: 16, column: 3, scope: !49)
!59 = !DILocation(line: 17, column: 3, scope: !49)
!60 = !DILocation(line: 19, column: 7, scope: !61)
!61 = distinct !DILexicalBlock(scope: !49, file: !1, line: 19, column: 7)
!62 = !DILocation(line: 19, column: 9, scope: !61)
!63 = !DILocation(line: 24, column: 7, scope: !49)
!64 = !DILocation(line: 25, column: 7, scope: !65)
!65 = distinct !DILexicalBlock(scope: !49, file: !1, line: 24, column: 7)
!66 = !DILocation(line: 0, scope: !21, inlinedAt: !67)
!67 = distinct !DILocation(line: 25, column: 5, scope: !65)
!68 = !DILocation(line: 6, column: 3, scope: !21, inlinedAt: !67)
!69 = !DILocation(line: 6, column: 7, scope: !21, inlinedAt: !67)
!70 = !DILocation(line: 7, column: 3, scope: !21, inlinedAt: !67)
!71 = !DILocation(line: 7, column: 7, scope: !21, inlinedAt: !67)
!72 = !DILocation(line: 9, column: 9, scope: !39, inlinedAt: !67)
!73 = !DILocation(line: 9, column: 7, scope: !21, inlinedAt: !67)
!74 = !DILocation(line: 10, column: 5, scope: !39, inlinedAt: !67)
!75 = !DILocation(line: 11, column: 1, scope: !21, inlinedAt: !67)
!76 = !DILocation(line: 25, column: 5, scope: !65)
!77 = !DILocation(line: 27, column: 3, scope: !49)
!78 = !DILocation(line: 28, column: 1, scope: !49)
!79 = !DISubprogram(name: "MPI_Comm_rank", scope: !4, file: !4, line: 1421, type: !80, flags: DIFlagPrototyped, spFlags: DISPFlagOptimized, retainedNodes: !48)
!80 = !DISubroutineType(types: !81)
!81 = !{!24, !11, !82}
!82 = !DIDerivedType(tag: DW_TAG_pointer_type, baseType: !24, size: 64)
!83 = !DISubprogram(name: "MPI_Comm_size", scope: !4, file: !4, line: 1428, type: !80, flags: DIFlagPrototyped, spFlags: DISPFlagOptimized, retainedNodes: !48)
!84 = !DISubprogram(name: "MPI_Barrier", scope: !4, file: !4, line: 1343, type: !85, flags: DIFlagPrototyped, spFlags: DISPFlagOptimized, retainedNodes: !48)
!85 = !DISubroutineType(types: !86)
!86 = !{!24, !11}
!87 = distinct !DISubprogram(name: "main", scope: !1, file: !1, line: 30, type: !88, scopeLine: 30, flags: DIFlagPrototyped | DIFlagAllCallsDescribed, spFlags: DISPFlagDefinition | DISPFlagOptimized, unit: !0, retainedNodes: !93)
!88 = !DISubroutineType(types: !89)
!89 = !{!24, !24, !90}
!90 = !DIDerivedType(tag: DW_TAG_pointer_type, baseType: !91, size: 64)
!91 = !DIDerivedType(tag: DW_TAG_pointer_type, baseType: !92, size: 64)
!92 = !DIBasicType(name: "char", size: 8, encoding: DW_ATE_signed_char)
!93 = !{!94, !95}
!94 = !DILocalVariable(name: "argc", arg: 1, scope: !87, file: !1, line: 30, type: !24)
!95 = !DILocalVariable(name: "argv", arg: 2, scope: !87, file: !1, line: 30, type: !90)
!96 = !DILocation(line: 0, scope: !87)
!97 = !{!98, !98, i64 0}
!98 = !{!"any pointer", !34, i64 0}
!99 = !DILocation(line: 32, column: 3, scope: !87)
!100 = !DILocation(line: 14, column: 3, scope: !49, inlinedAt: !101)
!101 = distinct !DILocation(line: 33, column: 3, scope: !87)
!102 = !DILocation(line: 0, scope: !49, inlinedAt: !101)
!103 = !DILocation(line: 16, column: 3, scope: !49, inlinedAt: !101)
!104 = !DILocation(line: 17, column: 3, scope: !49, inlinedAt: !101)
!105 = !DILocation(line: 19, column: 7, scope: !61, inlinedAt: !101)
!106 = !DILocation(line: 19, column: 9, scope: !61, inlinedAt: !101)
!107 = !DILocation(line: 24, column: 7, scope: !49, inlinedAt: !101)
!108 = !DILocation(line: 25, column: 7, scope: !65, inlinedAt: !101)
!109 = !DILocation(line: 0, scope: !21, inlinedAt: !110)
!110 = distinct !DILocation(line: 25, column: 5, scope: !65, inlinedAt: !101)
!111 = !DILocation(line: 6, column: 3, scope: !21, inlinedAt: !110)
!112 = !DILocation(line: 6, column: 7, scope: !21, inlinedAt: !110)
!113 = !DILocation(line: 7, column: 3, scope: !21, inlinedAt: !110)
!114 = !DILocation(line: 7, column: 7, scope: !21, inlinedAt: !110)
!115 = !DILocation(line: 9, column: 9, scope: !39, inlinedAt: !110)
!116 = !DILocation(line: 9, column: 7, scope: !21, inlinedAt: !110)
!117 = !DILocation(line: 10, column: 5, scope: !39, inlinedAt: !110)
!118 = !DILocation(line: 11, column: 1, scope: !21, inlinedAt: !110)
!119 = !DILocation(line: 25, column: 5, scope: !65, inlinedAt: !101)
!120 = !DILocation(line: 27, column: 3, scope: !49, inlinedAt: !101)
!121 = !DILocation(line: 28, column: 1, scope: !49, inlinedAt: !101)
!122 = !DILocation(line: 34, column: 3, scope: !87)
!123 = !DILocation(line: 36, column: 3, scope: !87)
!124 = !DISubprogram(name: "MPI_Init", scope: !4, file: !4, line: 1637, type: !125, flags: DIFlagPrototyped, spFlags: DISPFlagOptimized, retainedNodes: !48)
!125 = !DISubroutineType(types: !126)
!126 = !{!24, !82, !127}
!127 = !DIDerivedType(tag: DW_TAG_pointer_type, baseType: !90, size: 64)
!128 = !DISubprogram(name: "MPI_Finalize", scope: !4, file: !4, line: 1550, type: !129, flags: DIFlagPrototyped, spFlags: DISPFlagOptimized, retainedNodes: !48)
!129 = !DISubroutineType(types: !130)
!130 = !{!24}
