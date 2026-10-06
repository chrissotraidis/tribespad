declare i64 @fstatfs(i32, ptr)
define i64 @unsafe_volume_abi(i32 %fd, ptr %output) {
 %result = call i64 @fstatfs(i32 %fd, ptr %output)
 ret i64 %result
}
