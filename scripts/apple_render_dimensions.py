"""Use window dimensions in the original renderer's no-interop default path."""
import hashlib,json,pathlib
import pefile
from console_fade_inputs import definition

R=pathlib.Path(__file__).resolve().parents[1]
IMAGE_SHA='eaf5a42d301cc64a6e526aa49a82dbaefe655ed1ac0ba67939d0bab2fb6ab607'
BODY_SHA={
 'loc_1004324C':'1f8dc62294a9fd002da36ef736bd5a4e59af68adbb06eaf3a35c09e1a0be1b0f',
 'loc_100432DD':'71f8def96c23422dc3b2f1cf481e43f55ea020df0a24fe5e9438a45613f01f3e',
 'loc_10042B29':'08b42655ba50d70f1741554271f71348e409a2acf7ce80f4a74648c92d2e0e11',
 'loc_10042790':'eafce20f7864cba800e1fdadac1ddb157aca30c2eb3636d22d86819e60ac70cd',
 'loc_100423CB':'e1aca61fc6f796297177fd53fc6aa2e72ccf6d318394ced13c83eeac0b4f8ce6',
 'loc_10042854':'918240417ec8f541955cc2139a224a48c9bfce30da64987b3d6b70bb808bcd9f',
 'loc_10042974':'4c78676d2583ed4424f23c21777957ebe436cbb3a4374d69ebde89783cdf3188',
}
SITES=[('loc_1004324C',0x1004325a,'2b4a28','%11','%2','%21','%22',4),
       ('loc_100432DD',0x100432dd,'8b4224','%2','%5','%8','%9',0),
       ('loc_100432DD',0x100432e4,'8b4228','%2','%5','%19','%20',4),
       ('loc_10042B29',0x10042b3f,'8b4124','%6','%2','%21','%22',0),
       ('loc_10042B29',0x10042b46,'8b4128','%6','%2','%30','%31',4),
       ('loc_10042790',0x100427a8,'8b5128','%42','%7','%55','%56',4),
       ('loc_10042790',0x100427b6,'8b4124','%42','%7','%103','%104',0),
       ('loc_100423CB',0x1004240d,'2b4828','%49','%52','%55','%56',4),
       ('loc_10042854',0x10042896,'2b4828','%49','%52','%55','%56',4),
       ('loc_10042974',0x100429b6,'2b4828','%49','%52','%55','%56',4)]

def translate(text):
 image=R/'ref/qol/IFC22.dll'
 assert hashlib.sha256(image.read_bytes()).hexdigest()==IMAGE_SHA
 pe=pefile.PE(str(image))
 for name,expected in BODY_SHA.items():
  assert hashlib.sha256(definition(text,name).encode()).hexdigest()==expected,name
 for name,pc,raw,target,base,pointer,value,axis in SITES:
  assert pe.get_data(pc-0x10000000,3)==bytes.fromhex(raw),hex(pc)
  body=definition(text,name);needle=f'{value} = load i32, i32* {pointer}, align 1'
  assert body.count(needle)==1
  p=f'apple_{pc:x}'
  # A real nonzero framebuffer continues to read its original object, without
  # consulting any new globals. Only a null target with interop/current-target
  # both zero uses the existing window dimensions. Every read remains subject
  # to normal checked-memory lowering, including invalid nonzero targets and
  # null targets in other modes. No target object or graphics capability is made.
  replacement=f'''%{p}_null = icmp eq i32 {target}, 0
br i1 %{p}_null, label %{p}_default, label %{p}_object
{p}_object:
%{p}_object_value = load i32, i32* {pointer}, align 1
br label %{p}_merge
{p}_default:
%{p}_mode_address = add i64 {base}, 273075808
%{p}_mode_pointer = inttoptr i64 %{p}_mode_address to i32*
%{p}_mode = load i32, i32* %{p}_mode_pointer, align 4
%{p}_current_address = add i64 {base}, 273080920
%{p}_current_pointer = inttoptr i64 %{p}_current_address to i32*
%{p}_current = load i32, i32* %{p}_current_pointer, align 4
%{p}_no_interop = icmp eq i32 %{p}_mode, 0
%{p}_no_current = icmp eq i32 %{p}_current, 0
%{p}_window = and i1 %{p}_no_interop, %{p}_no_current
%{p}_dimensions_address = add i64 {base}, 270362664
%{p}_dimensions_pointer = inttoptr i64 %{p}_dimensions_address to i32*
%{p}_dimensions = load i32, i32* %{p}_dimensions_pointer, align 4
%{p}_axis = add i32 %{p}_dimensions, {axis}
%{p}_axis64 = zext i32 %{p}_axis to i64
%{p}_window_address = add i64 {base}, %{p}_axis64
%{p}_window_pointer = inttoptr i64 %{p}_window_address to i32*
%{p}_selected = select i1 %{p}_window, i32* %{p}_window_pointer, i32* {pointer}
%{p}_default_value = load i32, i32* %{p}_selected, align 1
br label %{p}_merge
{p}_merge:
{value} = phi i32 [ %{p}_object_value, %{p}_object ], [ %{p}_default_value, %{p}_default ]'''
  text=text.replace(body,body.replace(needle,replacement,1),1)
 return text

def prepare(text):
 test=R/'generated/integration-review/apple-render-dimensions/result.json'
 proof=json.loads(test.read_text());assert proof['passed']
 for name,expected in proof['sources_sha256'].items():
  assert hashlib.sha256((R/name).read_bytes()).hexdigest()==expected,name
 return translate(text),dict(image_sha256=IMAGE_SHA,original_blocks_sha256=BODY_SHA,
  sites=[dict(pc=pc,bytes_hex=raw,dimension='height' if axis else 'width') for _,pc,raw,_,_,_,_,axis in SITES],
  script_sha256=hashlib.sha256(pathlib.Path(__file__).read_bytes()).hexdigest(),test_sha256=hashlib.sha256(test.read_bytes()).hexdigest(),
  scope='Real window dimensions only for null target, interop mode zero and current target zero; original nonzero object reads and checked memory preserved.')
