declare ptr @"\01_realpath$DARWIN_EXTSN"(i64, i64)
define ptr @file_security_wrong_realpath() {
  %result = call ptr @"\01_realpath$DARWIN_EXTSN"(i64 0, i64 0)
  ret ptr %result
}
