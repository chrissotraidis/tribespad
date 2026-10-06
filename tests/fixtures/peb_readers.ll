; Original IFC22 reader bodies, extracted without instruction replacement.
%_cpu = type { i32, i32, i32, i32, i32, i32, i32, i32, i32, i32, i32, i32, [8 x double], i64 }
declare i32 @x86_read_fs_dword(i32)
declare void @tb_shift_reg(ptr,i32,i32,i32,i32,i32)
declare void @tribes_set_dispatch_target(ptr,i32)
define fastcc void @tribes_dispatch(ptr %cpu) { ret void }
define hidden fastcc void @loc_1011C79B(%_cpu* %cpu) nounwind {
;call x86_read_fs_dword 0x18
%1 = call i32 @x86_read_fs_dword(i32 24) nounwind
;mov eax, tmp0
;add tmpadr, eax, 48
%2 = add i32 %1, 48
;load eax, tmpadr, 1
%3 = getelementptr %_cpu, %_cpu* %cpu, i32 0, i32 13
%4 = load i64, i64* %3, align 8
%5 = zext i32 %2 to i64
%6 = add i64 %5, %4
%7 = inttoptr i64 %6 to i32*
%8 = load i32, i32* %7, align 1
;add tmpadr, eax, 104
%9 = add i32 %8, 104
;load eax, tmpadr, 1
%10 = zext i32 %9 to i64
%11 = add i64 %10, %4
%12 = inttoptr i64 %11 to i32*
%13 = load i32, i32* %12, align 1
%14 = getelementptr %_cpu, %_cpu* %cpu, i32 0, i32 0
store i32 %13, i32* %14, align 4
;call tb_shift_reg $, 1, 0, 32, 0, 8
call void @tb_shift_reg(%_cpu* %cpu, i32 1, i32 0, i32 32, i32 0, i32 8) nounwind
;and eax, eax, 0xffffff01
%15 = getelementptr %_cpu, %_cpu* %cpu, i32 0, i32 0
%16 = load i32, i32* %15, align 4
%17 = and i32 %16, 4294967041
%18 = getelementptr %_cpu, %_cpu* %cpu, i32 0, i32 0
store i32 %17, i32* %18, align 4
;and eflags, eflags, ~(CF|ZF|SF|OF|PF|AF)
%19 = getelementptr %_cpu, %_cpu* %cpu, i32 0, i32 8
%20 = load i32, i32* %19, align 4
%21 = and i32 %20, 4294965034
;and tmp0, eax, 0xff
%22 = and i32 %17, 255
;cmovz tmp0, tmp0, ZF, 0
%23 = icmp eq i32 %22, 0
%24 = select i1 %23, i32 64, i32 0
;or eflags, eflags, tmp0
%25 = or i32 %21, %24
;and tmp0, eax, 0x80
%26 = and i32 %17, 128
;cmovz tmp0, tmp0, 0, SF
%27 = icmp eq i32 %26, 0
%28 = select i1 %27, i32 0, i32 128
;or eflags, eflags, tmp0
%29 = or i32 %25, %28
;lshr tmp0, eax, 4
%30 = lshr i32 %17, 4
;xor tmp0, eax, tmp0
%31 = xor i32 %17, %30
;and tmp0, tmp0, 0xf
%32 = and i32 %31, 15
;mov tmp4, 0x9669 << PF_SHIFT
;lshr tmp0, tmp4, tmp0
%33 = lshr i32 154020, %32
;and tmp0, tmp0, PF
%34 = and i32 %33, 4
;or eflags, eflags, tmp0
%35 = or i32 %29, %34
%36 = getelementptr %_cpu, %_cpu* %cpu, i32 0, i32 8
store i32 %35, i32* %36, align 4
;    load tmp1, esp, 4
%37 = getelementptr %_cpu, %_cpu* %cpu, i32 0, i32 4
%38 = load i32, i32* %37, align 4
%39 = getelementptr %_cpu, %_cpu* %cpu, i32 0, i32 13
%40 = load i64, i64* %39, align 8
%41 = zext i32 %38 to i64
%42 = add i64 %41, %40
%43 = inttoptr i64 %42 to i32*
%44 = load i32, i32* %43, align 4
;    add esp, esp, 4
%45 = add i32 %38, 4
%46 = getelementptr %_cpu, %_cpu* %cpu, i32 0, i32 4
store i32 %45, i32* %46, align 4
;call tribes_set_dispatch_target $, tmp1
call void @tribes_set_dispatch_target(%_cpu* %cpu, i32 %44) nounwind
;tcall tribes_dispatch
musttail call fastcc void @tribes_dispatch(%_cpu* %cpu) nounwind
ret void
}
define hidden fastcc void @loc_1011C7AD(%_cpu* %cpu) nounwind {
;call x86_read_fs_dword 0x18
%1 = call i32 @x86_read_fs_dword(i32 24) nounwind
;mov eax, tmp0
;add tmpadr, eax, 48
%2 = add i32 %1, 48
;load eax, tmpadr, 1
%3 = getelementptr %_cpu, %_cpu* %cpu, i32 0, i32 13
%4 = load i64, i64* %3, align 8
%5 = zext i32 %2 to i64
%6 = add i64 %5, %4
%7 = inttoptr i64 %6 to i32*
%8 = load i32, i32* %7, align 1
;add tmpadr, eax, 16
%9 = add i32 %8, 16
;load eax, tmpadr, 1
%10 = zext i32 %9 to i64
%11 = add i64 %10, %4
%12 = inttoptr i64 %11 to i32*
%13 = load i32, i32* %12, align 1
;add tmpadr, eax, 8
%14 = add i32 %13, 8
;load eax, tmpadr, 1
%15 = zext i32 %14 to i64
%16 = add i64 %15, %4
%17 = inttoptr i64 %16 to i32*
%18 = load i32, i32* %17, align 1
%19 = getelementptr %_cpu, %_cpu* %cpu, i32 0, i32 0
store i32 %18, i32* %19, align 4
;call tb_shift_reg $, 1, 0, 32, 0, 31
call void @tb_shift_reg(%_cpu* %cpu, i32 1, i32 0, i32 32, i32 0, i32 31) nounwind
;    load tmp1, esp, 4
%20 = getelementptr %_cpu, %_cpu* %cpu, i32 0, i32 4
%21 = load i32, i32* %20, align 4
%22 = getelementptr %_cpu, %_cpu* %cpu, i32 0, i32 13
%23 = load i64, i64* %22, align 8
%24 = zext i32 %21 to i64
%25 = add i64 %24, %23
%26 = inttoptr i64 %25 to i32*
%27 = load i32, i32* %26, align 4
;    add esp, esp, 4
%28 = add i32 %21, 4
%29 = getelementptr %_cpu, %_cpu* %cpu, i32 0, i32 4
store i32 %28, i32* %29, align 4
;call tribes_set_dispatch_target $, tmp1
call void @tribes_set_dispatch_target(%_cpu* %cpu, i32 %27) nounwind
;tcall tribes_dispatch
musttail call fastcc void @tribes_dispatch(%_cpu* %cpu) nounwind
ret void
}
