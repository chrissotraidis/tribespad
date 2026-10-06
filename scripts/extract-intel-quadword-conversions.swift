import Foundation
import PDFKit
let doc=PDFDocument(url:URL(fileURLWithPath:CommandLine.arguments[1]))!
for i in 0..<doc.pageCount {
 let s=doc.page(at:i)!.string ?? ""
 if s.contains("VCVTTPS2UQQ") || s.contains("VCVTTPD2UQQ") || s.contains("VCVTTPS2QQ") || s.contains("VCVTTPD2QQ") || s.contains("VCVTUQQ2PD") || s.contains("VCVTQQ2PD") || s.contains("VCVTQQ2PS") {
  print("PAGE \(i+1)\n"+s)
 }
}
