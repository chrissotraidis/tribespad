declare i32 @curl_multi_setopt(ptr, i32, ...)
define void @curl_timer_bad_callback(ptr %callback) {
 %r = call i32 (ptr, i32, ...) @curl_multi_setopt(ptr null, i32 20004, ptr %callback)
 ret void
}
define void @curl_timer_bad_data(ptr %data) {
 %r = call i32 (ptr, i32, ...) @curl_multi_setopt(ptr null, i32 10005, ptr %data)
 ret void
}
define void @curl_timer_bad_width() {
 %r = call i32 (ptr, i32, ...) @curl_multi_setopt(ptr null, i32 20004, i64 0)
 ret void
}
define void @curl_timer_dynamic_option(i32 %option) {
 %r = call i32 (ptr, i32, ...) @curl_multi_setopt(ptr null, i32 %option, ptr null)
 ret void
}
define void @curl_timer_unknown_option() {
 %r = call i32 (ptr, i32, ...) @curl_multi_setopt(ptr null, i32 20007, ptr null)
 ret void
}
declare i32 @curl_easy_setopt(ptr, i32, ...)
declare i32 @curl_easy_getinfo(ptr, i32, ...)
declare i32 @curl_multi_socket_action(ptr, i64, i32, ptr)
define void @curl_socket_bad_callback(ptr %callback) {
 %r = call i32 (ptr, i32, ...) @curl_multi_setopt(ptr null, i32 20001, ptr %callback)
 ret void
}
define void @curl_close_bad_callback(ptr %callback) {
 %r = call i32 (ptr, i32, ...) @curl_easy_setopt(ptr null, i32 20208, ptr %callback)
 ret void
}
define void @curl_easy_guest_string(ptr %string) {
 %r = call i32 (ptr, i32, ...) @curl_easy_setopt(ptr null, i32 10002, ptr %string)
 ret void
}
define void @curl_easy_wrong_long() {
 %r = call i32 (ptr, i32, ...) @curl_easy_setopt(ptr null, i32 155, i32 1)
 ret void
}
define void @curl_wrong_info_selector() {
 %r = call i32 (ptr, i32, ...) @curl_easy_getinfo(ptr null, i32 0, ptr null)
 ret void
}
define void @curl_wrong_action_width() {
 %r = call i32 @curl_multi_socket_action(ptr null, i64 -1, i32 0, ptr null)
 ret void
}
