target triple = "arm64-apple-macos12"
declare i32 @fixture_dinput_enum(ptr, i32, i32)
declare ptr @tribes_lookup(i32)
define fastcc void @fixture_dinput_callback(ptr %cpu) {
 %sp = getelementptr i8, ptr %cpu, i64 16
 %esp = load i32, ptr %sp
 %bp = getelementptr i8, ptr %cpu, i64 112
 %base = load i64, ptr %bp
 %offset = zext i32 %esp to i64
 %address = add i64 %base, %offset
 %p = inttoptr i64 %address to ptr
 %target = load i32, ptr %p, align 1
 %p0 = getelementptr i8, ptr %p, i64 4
 %p1 = getelementptr i8, ptr %p, i64 8
 %a0 = load i32, ptr %p0, align 1
 %a1 = load i32, ptr %p1, align 1
 %result = call i32 @fixture_dinput_enum(ptr %cpu, i32 %a0, i32 %a1)
 store i32 %result, ptr %cpu
 %next = add i32 %esp, 12
 store i32 %next, ptr %sp
 %fn = call ptr @tribes_lookup(i32 %target)
 musttail call fastcc void %fn(ptr %cpu)
 ret void
}
