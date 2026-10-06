declare i32 @read(i32, ptr, i64)
define i32 @unsafe_read_abi(i32 %fd, ptr %output) {
 %result = call i32 @read(i32 %fd, ptr %output, i64 1)
 ret i32 %result
}
