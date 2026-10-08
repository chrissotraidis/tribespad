declare i32 @curl_global_init_mem(i32, ptr, ptr, ptr, ptr, ptr)
declare i32 @curl_multi_init()
declare i64 @curl_multi_cleanup(ptr)
declare i32 @curl_global_cleanup()
declare i32 @curl_version()
define void @curl_wrong_version() { %r = call i32 @curl_version()
 ret void }
define void @curl_wrong_init() {
 %r = call i32 @curl_global_init_mem(i32 1, ptr null, ptr null, ptr null, ptr null, ptr null)
 ret void
}
define void @curl_wrong_multi() { %r = call i32 @curl_multi_init()
 ret void }
define void @curl_wrong_cleanup() { %r = call i64 @curl_multi_cleanup(ptr null)
 ret void }
define void @curl_wrong_global_cleanup() { %r = call i32 @curl_global_cleanup()
 ret void }

declare i32 @curl_easy_reset(ptr)
define void @curl_wrong_reset() { %r = call i32 @curl_easy_reset(ptr null)
 ret void }
