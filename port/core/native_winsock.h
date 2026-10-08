#ifndef TRIBESPAD_NATIVE_WINSOCK_H
#define TRIBESPAD_NATIVE_WINSOCK_H
#include <stdint.h>
/* The guest host order is x86 little-endian, independently of native headers. */
uint32_t tribes_network_u16(uint32_t value);
uint32_t tribes_network_u32(uint32_t value);
uint32_t tribes_wsa_startup(uint64_t base,uint32_t version,uint32_t output);
uint32_t tribes_wsa_cleanup(uint64_t base);
uint32_t tribes_get_addr_info_w(uint64_t base,uint32_t node,uint32_t service,uint32_t hints,uint32_t output);
void tribes_free_addr_info_w(uint64_t base,uint32_t address);
uint32_t tribes_set_socket_handle_information(uint64_t base,uint32_t handle,uint32_t mask,uint32_t flags);
uint32_t tribes_socket(uint64_t base,uint32_t family,uint32_t type,uint32_t protocol);
uint32_t tribes_closesocket(uint64_t base,uint32_t socket);
uint32_t tribes_bind_socket(uint64_t base,uint32_t socket,uint32_t address,uint32_t length);
uint32_t tribes_getsockname(uint64_t base,uint32_t socket,uint32_t address,uint32_t length);
uint32_t tribes_getsockopt(uint64_t base,uint32_t socket,uint32_t level,uint32_t option,uint32_t value,uint32_t length);
uint32_t tribes_wsa_ioctl(uint64_t base,uint32_t socket,uint32_t command,uint32_t input,uint32_t input_size,uint32_t output,uint32_t capacity,uint32_t returned,uint32_t overlapped,uint32_t completion);
uint32_t tribes_socket_select(uint64_t base,uint32_t nfds,uint32_t read_set,uint32_t write_set,uint32_t except_set,uint32_t timeout);
uint32_t tribes_ioctlsocket(uint64_t base,uint32_t socket,uint32_t command,uint32_t argument);
uint32_t tribes_sendto(uint64_t base,uint32_t socket,uint32_t buffer,uint32_t length,uint32_t flags,uint32_t address,uint32_t address_length);
uint32_t tribes_recvfrom(uint64_t base,uint32_t socket,uint32_t buffer,uint32_t length,uint32_t flags,uint32_t address,uint32_t address_length);
/* Host adapters only. The owner retains the original descriptor, and must
   detach before closing it. Poll removal is not detachment. No persistent
   duplicate keeps a connection alive after its owner closes it. */
uint32_t tribes_socket_attach(uint64_t base,int descriptor,const void *owner);
int tribes_socket_detach(uint64_t base,uint32_t socket,const void *owner);
/* Caller owns the returned temporary duplicate; never expose it to guest. */
int tribes_socket_duplicate(uint64_t base,uint32_t socket);
uint32_t tribes_socket_associate_completion(uint64_t base,uint32_t socket,uint32_t port,uint32_t key);
uint32_t tribes_socket_set_completion_modes(uint64_t base,uint32_t socket,uint32_t flags);
#define TRIBES_CONNECT_EX_ENTRY UINT32_C(0xfffb1240)
uint32_t tribes_connect_ex(uint64_t base,uint32_t socket,uint32_t address,uint32_t length,uint32_t send_buffer,uint32_t send_length,uint32_t sent,uint32_t overlapped);
uint32_t tribes_setsockopt(uint64_t base,uint32_t socket,uint32_t level,uint32_t option,uint32_t value,uint32_t length);
/* Host lifecycle observation; no guest pointer or native thread ID escapes. */
uint32_t tribes_socket_pending_connect_count(void);
uint32_t tribes_socket_pending_io_count(void);
uint32_t tribes_cancel_socket_io_ex(uint64_t base,uint32_t socket,uint32_t overlapped);
uint32_t tribes_cancel_socket_io(uint64_t base,uint32_t socket);
uint32_t tribes_wsa_recv(uint64_t base,uint32_t socket,uint32_t buffers,uint32_t count,uint32_t bytes,uint32_t flags,uint32_t overlapped,uint32_t callback);
uint32_t tribes_wsa_send(uint64_t base,uint32_t socket,uint32_t buffers,uint32_t count,uint32_t bytes,uint32_t flags,uint32_t overlapped,uint32_t callback);
/* Host operation adapter, called only after successful real native I/O. */
uint32_t tribes_socket_post_completion(uint64_t base,uint32_t socket,uint32_t bytes,uint32_t overlapped,uint32_t immediate);
uint32_t tribes_nt_device_io_control_file(uint64_t,uint32_t,uint32_t,uint32_t,uint32_t,uint32_t,uint32_t,uint32_t,uint32_t,uint32_t,uint32_t);
uint32_t tribes_wsa_recv_from(uint64_t,uint32_t,uint32_t,uint32_t,uint32_t,uint32_t,uint32_t,uint32_t,uint32_t,uint32_t);
uint32_t tribes_wsa_send_to(uint64_t,uint32_t,uint32_t,uint32_t,uint32_t,uint32_t,uint32_t,uint32_t,uint32_t,uint32_t);
#endif
