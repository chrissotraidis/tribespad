declare i32 @curl_global_init_mem(i64, ptr, ptr, ptr, ptr, ptr)
declare i32 @curl_multi_cleanup(ptr)
define void @curl_bad_callback(ptr %callback) {
 %r = call i32 @curl_global_init_mem(i64 1, ptr %callback, ptr %callback, ptr %callback, ptr %callback, ptr %callback)
 ret void
}
define void @curl_bad_pointer(ptr %handle) {
 %r = call i32 @curl_multi_cleanup(ptr %handle)
 ret void
}
