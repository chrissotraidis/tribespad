#!/usr/bin/env python3
"""Compile the upstream declarations with the actual Windows i686 ABI."""
import hashlib,json,pathlib,subprocess
R=pathlib.Path(__file__).resolve().parents[1];out=R/'generated/integration-review/adapters-layout';out.mkdir(exist_ok=True)
header=R/'ref/adapter-api/iptypes.h';s=header.read_text();fragments=[]
for name in ['IP_ADAPTER_UNICAST_ADDRESS_LH','IP_ADAPTER_ADDRESSES_LH']:
 start=s.index('  typedef struct _'+name+' {');end=s.index('*P'+name+';',start)+len('*P'+name+';');fragments.append(s[start:end])
preamble='''#define __C89_NAMELESS
#define NTDDI_VERSION 0x06000100
#define MAX_ADAPTER_ADDRESS_LENGTH 8
#define MAX_DHCPV6_DUID_LENGTH 130
typedef unsigned long ULONG, DWORD, IF_INDEX, IFTYPE, NET_IF_COMPARTMENT_ID;
typedef unsigned long long ULONGLONG, ULONG64, IF_LUID;
typedef unsigned char BYTE, UINT8;
typedef char *PCHAR;
typedef unsigned short *PWCHAR;
typedef int IP_PREFIX_ORIGIN, IP_SUFFIX_ORIGIN, IP_DAD_STATE, IF_OPER_STATUS, NET_IF_CONNECTION_TYPE, TUNNEL_TYPE;
typedef struct { void *lpSockaddr; int iSockaddrLength; } SOCKET_ADDRESS;
typedef struct { ULONG d1; unsigned short d2,d3; BYTE d4[8]; } NET_IF_NETWORK_GUID;
typedef void *PIP_ADAPTER_ANYCAST_ADDRESS_XP, *PIP_ADAPTER_MULTICAST_ADDRESS_XP, *PIP_ADAPTER_DNS_SERVER_ADDRESS_XP, *PIP_ADAPTER_PREFIX_XP, *PIP_ADAPTER_WINS_SERVER_ADDRESS_LH, *PIP_ADAPTER_GATEWAY_ADDRESS_LH, *PIP_ADAPTER_DNS_SUFFIX;
'''
checks=[]
for name,size,offsets in [('IP_ADAPTER_ADDRESSES_LH',376,{'Next':8,'AdapterName':12,'FirstUnicastAddress':16,'DnsSuffix':32,'Description':36,'FriendlyName':40,'PhysicalAddress':44,'PhysicalAddressLength':52,'Flags':56,'Mtu':60,'IfType':64,'OperStatus':68,'Ipv6IfIndex':72,'ZoneIndices':76,'FirstPrefix':140,'TransmitLinkSpeed':144,'FirstDnsSuffix':368}),('IP_ADAPTER_UNICAST_ADDRESS_LH',48,{'Next':8,'Address':12,'PrefixOrigin':20,'DadState':28,'OnLinkPrefixLength':44})]:
 checks.append('_Static_assert(sizeof(%s)==%d,"size");'%(name,size))
 for field,offset in offsets.items():checks.append('_Static_assert(__builtin_offsetof(%s,%s)==%d,"%s");'%(name,field,offset,field))
source=out/'upstream-layout.c';source.write_text(preamble+'\n'.join(fragments+checks));command=['clang','-target','i686-pc-windows-msvc','-std=c11','-fsyntax-only','-Wall','-Wextra','-Werror',str(source)];subprocess.run(command,check=True)
sha=lambda p:hashlib.sha256(p.read_bytes()).hexdigest();(out/'result.json').write_text(json.dumps(dict(passed=True,upstream='https://raw.githubusercontent.com/mingw-w64/mingw-w64/v12.0.0/mingw-w64-headers/include/iptypes.h',header_sha256=sha(header),source_sha256=sha(source),script_sha256=sha(pathlib.Path(__file__).resolve()),assertions=len(checks),command=command),indent=2)+'\n');print('PASS: upstream i686 Windows structure sizes and %d offset/size assertions'%len(checks))
