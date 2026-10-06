import Foundation
import PDFKit
let doc=PDFDocument(url:URL(fileURLWithPath:CommandLine.arguments[1]))!
for i in 0..<doc.pageCount {
 let s=doc.page(at:i)!.string ?? ""
 if s.contains("PEXTRD") || s.contains("PINSRD") {
  print("PAGE \(i+1)\n"+s)
 }
}
