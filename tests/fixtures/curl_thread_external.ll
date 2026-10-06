declare i32 @curl_easy_setopt(ptr,i32,...)
declare i32 @curl_easy_getinfo(ptr,i32,...)
define void @unsafe_curl_write_callback(ptr %p) {
 %r=call i32 (ptr,i32,...) @curl_easy_setopt(ptr null,i32 20011,ptr %p)
 ret void
}
define void @unsafe_curl_write_data(ptr %p) {
 %r=call i32 (ptr,i32,...) @curl_easy_setopt(ptr null,i32 10001,ptr %p)
 ret void
}
define void @unsafe_curl_time_width() {
 %r=call i32 (ptr,i32,...) @curl_easy_setopt(ptr null,i32 30270,i32 1)
 ret void
}
define void @unsafe_curl_ca_output(ptr %p) {
 %r=call i32 (ptr,i32,...) @curl_easy_getinfo(ptr null,i32 1048637,ptr %p)
 ret void
}
