declare i64 @getaddrinfo(ptr,ptr,ptr,ptr)
declare i32 @freeaddrinfo(ptr)
define void @dns_bad_result(){ %r=call i64 @getaddrinfo(ptr null,ptr null,ptr null,ptr null)
 ret void
}
define void @dns_bad_free(){ %r=call i32 @freeaddrinfo(ptr null)
 ret void
}
