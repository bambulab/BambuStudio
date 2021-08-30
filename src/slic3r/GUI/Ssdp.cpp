#include "Ssdp.hpp"


#include <algorithm>
#include <sstream>
#include <exception>
#include <wx/progdlg.h>
#include <wx/event.h>
//#include <unistd.h>
#include <string.h>
#include <boost/log/trivial.hpp>
#include "GUI.hpp"
#include "GUI_App.hpp"
#include "MainFrame.hpp"
#include "DebugToolDialog.hpp"
#include "libslic3r/Utils.hpp"


static struct SDP_CONST Global = {
	// SSDP Method
	"M-SEARCH",
	"NOTIFY",
	"RESPONSE",

	// SSDP Header
	"M-SEARCH * HTTP/1.1\r\n",
	"NOTIFY * HTTP/1.1\r\n",
	"HTTP/1.1 200 OK\r\n",

	// IP Address
	"127.0.0.1",
	"239.255.255.250"
};

int bbl_init_socket()
{
#if defined(_WIN32)
	WORD sockVersion = MAKEWORD(2, 2);
	WSADATA wsaData;
	if (WSAStartup(sockVersion, &wsaData) != 0)
	{
		return -1;
	}
#endif
	return 0;
}

int bbl_create_ssdp_multi_sock(SOCKET* ssdpSock, const char* ipv4_addr)
{
	int ret = 0;
#if defined(_WIN32)
	char errorBuffer[ERROR_BUFFER_LEN];
	int onOff;
	u_char ttl = (u_char)4;
	struct ip_mreq ssdpMcastAddr;
	struct sockaddr_storage __ss;
	struct sockaddr_in* ssdpAddr4 = (struct sockaddr_in*)&__ss;
	struct in_addr addr;

	*ssdpSock = socket(AF_INET, SOCK_DGRAM, 0);

	if (*ssdpSock == INVALID_SOCKET) {
		return -1;
	}
	onOff = 1;
	ret = setsockopt(*ssdpSock,
		SOL_SOCKET,
		SO_REUSEADDR,
		(char*)&onOff,
		sizeof(onOff));
	if (ret == -1) {
		return -2;
		goto error_handler;
	}
	memset(&__ss, 0, sizeof(__ss));
	ssdpAddr4->sin_family = (sa_family_t)AF_INET;
	ssdpAddr4->sin_addr.s_addr = htonl(INADDR_ANY);
	ssdpAddr4->sin_port = htons(BBL_SSDP_MULTI_PORT);
	ret = bind(*ssdpSock, (struct sockaddr*)ssdpAddr4, sizeof(*ssdpAddr4));
	if (ret == -1) {
		return -3;
		goto error_handler;
	}
	memset((void*)&ssdpMcastAddr, 0, sizeof(struct ip_mreq));
	ssdpMcastAddr.imr_interface.s_addr = inet_addr(ipv4_addr);
	ssdpMcastAddr.imr_multiaddr.s_addr = inet_addr(BBL_SDP_MULTI_IP);
	ret = setsockopt(*ssdpSock,
		IPPROTO_IP,
		IP_ADD_MEMBERSHIP,
		(char*)&ssdpMcastAddr,
		sizeof(struct ip_mreq));
	if (ret == -1) {
		return -4;
		goto error_handler;
	}
	/* Set multicast interface. */
	memset((void*)&addr, 0, sizeof(struct in_addr));
	addr.s_addr = inet_addr(ipv4_addr);
	ret = setsockopt(*ssdpSock,
		IPPROTO_IP,
		IP_MULTICAST_IF,
		(char*)&addr,
		sizeof addr);
	if (ret == -1) {
		/* This is probably not a critical error, so let's continue. */
	}
	/* result is not checked becuase it will fail in WinMe and Win9x. */
	setsockopt(*ssdpSock, IPPROTO_IP, IP_MULTICAST_TTL, (const char*)&ttl, sizeof(ttl));
	onOff = 1;
	ret = setsockopt(*ssdpSock,
		SOL_SOCKET,
		SO_BROADCAST,
		(char*)&onOff,
		sizeof(onOff));
	if (ret == -1) {
		return -5;
		goto error_handler;
	}
	return 0;

error_handler:
	if (ret != 0) {
		closesocket(*ssdpSock);
	}
#endif
	return ret;
}


int bbl_create_ssdp_broadcast_sock(SOCKET* ssdpSock, const char* ipv4_addr)
{
	int ret = 0;
#if defined(_WIN32)
	char errorBuffer[ERROR_BUFFER_LEN];
	int onOff;
	u_char ttl = (u_char)4;
	struct ip_mreq ssdpMcastAddr;
	struct sockaddr_storage __ss;
	struct sockaddr_in* ssdpAddr4 = (struct sockaddr_in*)&__ss;
	struct in_addr addr;

	*ssdpSock = socket(AF_INET, SOCK_DGRAM, 0);

	if (*ssdpSock == INVALID_SOCKET) {
		return -1;
	}
	onOff = 1;
	ret = setsockopt(*ssdpSock,
		SOL_SOCKET,
		SO_REUSEADDR,
		(char*)&onOff,
		sizeof(onOff));
	if (ret == -1) {
		return -2;
		goto error_handler;
	}
	memset(&__ss, 0, sizeof(__ss));
	ssdpAddr4->sin_family = (sa_family_t)AF_INET;
	ssdpAddr4->sin_addr.s_addr = inet_addr(ipv4_addr);
	ssdpAddr4->sin_port = htons(BBL_SSDP_BROADCAST_PORT);
	ret = bind(*ssdpSock, (struct sockaddr*)ssdpAddr4, sizeof(*ssdpAddr4));
	if (ret == -1) {
		return -3;
		goto error_handler;
	}

	onOff = 1;
	ret = setsockopt(*ssdpSock,
		SOL_SOCKET,
		SO_BROADCAST,
		(char*)&onOff,
		sizeof(onOff));
	if (ret == -1) {
		return -5;
		goto error_handler;
	}
	return 0;

error_handler:
	if (ret != 0) {
		closesocket(*ssdpSock);
	}
#endif
	return ret;
}

int bbl_init_multi_socket(SOCKET* sock, int max_size)
{
	int found_number = 0;
#if defined(_WIN32)
	PIP_ADAPTER_ADDRESSES adapts = NULL;
	PIP_ADAPTER_ADDRESSES adapts_item;
	PIP_ADAPTER_UNICAST_ADDRESS uni_addr;
	SOCKADDR* ip_addr;
	struct in_addr v4_addr;
	struct in6_addr v6_addr;
	ULONG adapts_sz = 0;
	ULONG ret;
	int ifname_found = 0;
	int valid_addr_found = 0;

	/* Get Adapters addresses required size. */
	ret = GetAdaptersAddresses(AF_UNSPEC,
		GAA_FLAG_SKIP_ANYCAST | GAA_FLAG_SKIP_DNS_SERVER,
		NULL,
		adapts,
		&adapts_sz);
	if (ret != ERROR_BUFFER_OVERFLOW) {
		printf("GetAdaptersAddresses failed to find list of "
			"adapters\n");
		return -1;
	}
	/* Allocate enough memory. */
	adapts = (PIP_ADAPTER_ADDRESSES)malloc(adapts_sz);
	if (adapts == NULL) {
		return -2;
	}
	/* Do the call that will actually return the info. */
	ret = GetAdaptersAddresses(AF_UNSPEC,
		GAA_FLAG_SKIP_ANYCAST | GAA_FLAG_SKIP_DNS_SERVER,
		NULL,
		adapts,
		&adapts_sz);
	if (ret != ERROR_SUCCESS) {
		free(adapts);
		printf("GetAdaptersAddresses failed to find list of "
			"adapters\n");
		return -3;
	}
	/* Copy interface name, if it was provided. */
	for (adapts_item = adapts; adapts_item != NULL;
		adapts_item = adapts_item->Next) {
		if (adapts_item->Flags & IP_ADAPTER_NO_MULTICAST ||
			adapts_item->OperStatus != IfOperStatusUp) {
			continue;
		}
		valid_addr_found = 0;
		/* Loop thru this adapter's unicast IP addresses. */
		uni_addr = adapts_item->FirstUnicastAddress;
		while (uni_addr) {
			ip_addr = uni_addr->Address.lpSockaddr;
			switch (ip_addr->sa_family) {
			case AF_INET:
				memcpy(&v4_addr,
					&((struct sockaddr_in*)ip_addr)
					->sin_addr,
					sizeof(v4_addr));
				/* TODO: Retrieve IPv4 netmask */
				valid_addr_found = 1;
				break;
			case AF_INET6:
				if (IN6_IS_ADDR_LINKLOCAL(
					&((struct sockaddr_in6*)ip_addr)
					->sin6_addr)) {
					memcpy(&v6_addr,
						&((struct sockaddr_in6*)
							ip_addr)
						->sin6_addr,
						sizeof(v6_addr));
					valid_addr_found = 1;
				}
				break;
			default:
				if (valid_addr_found == 0) {
					ifname_found = 0;
				}
				break;
			}
			/* Next address. */
			uni_addr = uni_addr->Next;
		}
		if (valid_addr_found == 1) {
			char ipv4_addr[INET_ADDRSTRLEN] = { '\0' };
			inet_ntop(AF_INET, &v4_addr, ipv4_addr, sizeof(ipv4_addr));
			if (strstr(ipv4_addr, "192.168") != NULL) {
				int res = bbl_create_ssdp_multi_sock(&sock[found_number], ipv4_addr);
				printf("card = %d, ip addr = %s\n", (int)found_number, ipv4_addr);
				BOOST_LOG_TRIVIAL(trace) << "card = " << found_number << ", ip addr=" << ipv4_addr;
				found_number++;
			}
		}
	}
	free(adapts);
#endif
	return found_number;
}


int bbl_init_broadcast_socket(SOCKET* sock, int max_size)
{
	int found_number = 0;
#if defined(_WIN32)
	PIP_ADAPTER_ADDRESSES adapts = NULL;
	PIP_ADAPTER_ADDRESSES adapts_item;
	PIP_ADAPTER_UNICAST_ADDRESS uni_addr;
	SOCKADDR* ip_addr;
	struct in_addr v4_addr;
	struct in6_addr v6_addr;
	ULONG adapts_sz = 0;
	ULONG ret;
	int ifname_found = 0;
	int valid_addr_found = 0;

	/* Get Adapters addresses required size. */
	ret = GetAdaptersAddresses(AF_UNSPEC,
		GAA_FLAG_SKIP_ANYCAST | GAA_FLAG_SKIP_DNS_SERVER,
		NULL,
		adapts,
		&adapts_sz);
	if (ret != ERROR_BUFFER_OVERFLOW) {
		printf("GetAdaptersAddresses failed to find list of "
			"adapters\n");
		return -1;
	}
	/* Allocate enough memory. */
	adapts = (PIP_ADAPTER_ADDRESSES)malloc(adapts_sz);
	if (adapts == NULL) {
		return -2;
	}
	/* Do the call that will actually return the info. */
	ret = GetAdaptersAddresses(AF_UNSPEC,
		GAA_FLAG_SKIP_ANYCAST | GAA_FLAG_SKIP_DNS_SERVER,
		NULL,
		adapts,
		&adapts_sz);
	if (ret != ERROR_SUCCESS) {
		free(adapts);
		printf("GetAdaptersAddresses failed to find list of "
			"adapters\n");
		return -3;
	}
	/* Copy interface name, if it was provided. */
	for (adapts_item = adapts; adapts_item != NULL;
		adapts_item = adapts_item->Next) {
		if (adapts_item->Flags & IP_ADAPTER_NO_MULTICAST ||
			adapts_item->OperStatus != IfOperStatusUp) {
			continue;
		}
		valid_addr_found = 0;
		/* Loop thru this adapter's unicast IP addresses. */
		uni_addr = adapts_item->FirstUnicastAddress;
		while (uni_addr) {
			ip_addr = uni_addr->Address.lpSockaddr;
			switch (ip_addr->sa_family) {
			case AF_INET:
				memcpy(&v4_addr,
					&((struct sockaddr_in*)ip_addr)
					->sin_addr,
					sizeof(v4_addr));
				/* TODO: Retrieve IPv4 netmask */
				valid_addr_found = 1;
				break;
			case AF_INET6:
				if (IN6_IS_ADDR_LINKLOCAL(
					&((struct sockaddr_in6*)ip_addr)
					->sin6_addr)) {
					memcpy(&v6_addr,
						&((struct sockaddr_in6*)
							ip_addr)
						->sin6_addr,
						sizeof(v6_addr));
					valid_addr_found = 1;
				}
				break;
			default:
				if (valid_addr_found == 0) {
					ifname_found = 0;
				}
				break;
			}
			/* Next address. */
			uni_addr = uni_addr->Next;
		}
		if (valid_addr_found == 1) {
			char ipv4_addr[INET_ADDRSTRLEN] = { '\0' };
			inet_ntop(AF_INET, &v4_addr, ipv4_addr, sizeof(ipv4_addr));
			if (strstr(ipv4_addr, "192.168") != NULL) {
				int res = bbl_create_ssdp_broadcast_sock(&sock[found_number], ipv4_addr);
				printf("card = %d, ip addr = %s\n", (int)found_number, ipv4_addr);
				BOOST_LOG_TRIVIAL(trace) << "card = " << found_number << ", ip addr=" << ipv4_addr;
				found_number++;
			}
		}
	}
	free(adapts);
#endif
	return found_number;
}

int bbl_send_ssdp_msg(SOCKET socket)
{
	char msearch[4096] = {};
	snprintf(msearch, sizeof(msearch),
		"%s"
		"HOST:%s:%d\r\n"
		"MAN:\"ssdp:discover\"\r\n"
		"MX:1\r\n"
		"ST:%s\r\n"
		"USER-AGENT:OS/version product/version\r\n"
		"\r\n",
		Global.HEADER_MSEARCH,
		BBL_SDP_BROADCAST_IP, BBL_SSDP_BROADCAST_PORT,
		"urn:bambulab-com:device:3dprinter:1"
	);
	int buf_size = strlen(msearch) + 1;
	struct sockaddr_in si_other;
	si_other.sin_family = AF_INET;
	si_other.sin_addr.s_addr = htonl(INADDR_BROADCAST);
	si_other.sin_port = htons(BBL_SSDP_BROADCAST_PORT);
	int slen = sizeof(si_other);
	return sendto(socket, msearch, buf_size, MSG_DONTROUTE, (struct sockaddr*)&si_other, slen);
}

int bbl_read_from_ssdp(SOCKET socket, char* buf, int *buf_size, int max_buf_size)
{
	char* requestBuf = NULL;
	char staticBuf[BUFSIZE];
	struct sockaddr_storage __ss;
	socklen_t socklen = sizeof(__ss);
	size_t byteReceived = 0;
	char ntop_buf[INET6_ADDRSTRLEN];

	memset(staticBuf, 0, sizeof(staticBuf));

	requestBuf = staticBuf;
	/* in case memory can't be allocated, still drain the socket using a
	 * static buffer. */

	byteReceived = recvfrom(socket,
		requestBuf,
		BUFSIZE - (size_t)1,
		0,
		(struct sockaddr*)&__ss,
		&socklen);
	if (byteReceived > 0) {
		requestBuf[byteReceived] = '\0';
		/* clang-format off */
		printf("byte %d, received str: %s\n", byteReceived, requestBuf);
		memset(buf, 0, max_buf_size);
		strncpy(buf, requestBuf, byteReceived);
		buf[byteReceived] = '\0';
		*buf_size = byteReceived;
	}
	return 0;
}

int bbl_read_from_broadcast(SOCKET socket, char* buf, int* buf_size, int max_buf_size)
{
	char* requestBuf = NULL;
	char staticBuf[BUFSIZE];
	struct sockaddr_storage __ss;
	socklen_t socklen = sizeof(__ss);
	size_t byteReceived = 0;
	char ntop_buf[INET6_ADDRSTRLEN];

	memset(staticBuf, 0, sizeof(staticBuf));

	requestBuf = staticBuf;
	/* in case memory can't be allocated, still drain the socket using a
	 * static buffer. */

	byteReceived = recvfrom(socket,
		requestBuf,
		BUFSIZE - (size_t)1,
		0,
		(struct sockaddr*)&__ss,
		&socklen);
	if (byteReceived > 0) {
		requestBuf[byteReceived] = '\0';
		/* clang-format off */
		printf("byte %d, received str: %s\n", byteReceived, requestBuf);
		memset(buf, 0, max_buf_size);
		strncpy(buf, requestBuf, byteReceived);
		buf[byteReceived] = '\0';
		*buf_size = byteReceived;
	}
	return 0;
}


int lssdp_packet_parser(const char* data, size_t data_len, lssdp_packet* packet) {
	if (data == NULL) {
		printf("data should not be NULL\n");
		return -1;
	}

	if (data_len != strlen(data)) {
		printf("data_len (%zu) is not match to the data length (%zu)\n", data_len, strlen(data));
		return -1;
	}

	if (packet == NULL) {
		printf("packet should not be NULL\n");
		return -1;
	}

	// 1. compare SSDP Method Header: M-SEARCH, NOTIFY, RESPONSE
	size_t i;
	if ((i = strlen(Global.HEADER_MSEARCH)) < data_len && memcmp(data, Global.HEADER_MSEARCH, i) == 0) {
		strcpy(packet->method, Global.MSEARCH);
		return -1;
	}
	else if ((i = strlen(Global.HEADER_NOTIFY)) < data_len && memcmp(data, Global.HEADER_NOTIFY, i) == 0) {
		strcpy(packet->method, Global.NOTIFY);
	}
	else if ((i = strlen(Global.HEADER_RESPONSE)) < data_len && memcmp(data, Global.HEADER_RESPONSE, i) == 0) {
		strcpy(packet->method, Global.RESPONSE);
		return -1;
	}
	else {
		printf("received unknown SSDP packet\n");
		printf("%s\n", data);
		return -1;
	}

	// 2. parse each field line
	size_t start = i;
	for (i = start; i < data_len; i++) {
		if (data[i] == '\n' && i - 1 > start && data[i - 1] == '\r') {
			parse_field_line(data, start, i - 2, packet);
			start = i + 1;
		}
	}
	return 0;
}

int parse_field_line(const char* data, size_t start, size_t end, lssdp_packet* packet) {
	// 1. find the colon
	if (data[start] == ':') {
		printf("the first character of line should not be colon\n");
		printf("%s\n", data);
		return -1;
	}

	int colon = get_colon_index(data, start + 1, end);
	if (colon == -1) {
		printf("there is no colon in line\n");
		printf("%s\n", data);
		return -1;
	}

	if (colon == end) {
		// value is empty
		return -1;
	}


	// 2. get field, field_len
	size_t i = start;
	size_t j = colon - 1;
	if (trim_spaces(data, &i, &j) == -1) {
		return -1;
	}
	const char* field = &data[i];
	size_t field_len = j - i + 1;


	// 3. get value, value_len
	i = colon + 1;
	j = end;
	if (trim_spaces(data, &i, &j) == -1) {
		return -1;
	};
	const char* value = &data[i];
	size_t value_len = j - i + 1;
	// 4. set each field's value to packet
	if (field_len == strlen("st") && strncmp(field, "ST", field_len) == 0) {
		memcpy(packet->st, value, value_len < LSSDP_FIELD_LEN ? value_len : LSSDP_FIELD_LEN - 1);
		return 0;
	}

	if (field_len == strlen("nt") && strncmp(field, "NT", field_len) == 0) {
		memcpy(packet->st, value, value_len < LSSDP_FIELD_LEN ? value_len : LSSDP_FIELD_LEN - 1);
		return 0;
	}

	if (field_len == strlen("usn") && strncmp(field, "USN", field_len) == 0) {
		memcpy(packet->usn, value, value_len < LSSDP_FIELD_LEN ? value_len : LSSDP_FIELD_LEN - 1);
		return 0;
	}

	if (field_len == strlen("location") && strncmp(field, "Location", field_len) == 0) {
		memcpy(packet->location, value, value_len < LSSDP_LOCATION_LEN ? value_len : LSSDP_LOCATION_LEN - 1);
		return 0;
	}

	if (field_len == strlen("sm_id") && strncmp(field, "sm_id", field_len) == 0) {
		memcpy(packet->sm_id, value, value_len < LSSDP_FIELD_LEN ? value_len : LSSDP_FIELD_LEN - 1);
		return 0;
	}

	if (field_len == strlen("dev_type") && strncmp(field, "dev_type", field_len) == 0) {
		memcpy(packet->device_type, value, value_len < LSSDP_FIELD_LEN ? value_len : LSSDP_FIELD_LEN - 1);
		return 0;
	}

	// the field is not in the struct packet
	return 0;
}

int get_colon_index(const char* string, size_t start, size_t end) {
	size_t i;
	for (i = start; i <= end; i++) {
		if (string[i] == ':') {
			return i;
		}
	}
	return -1;
}

int trim_spaces(const char* string, size_t* start, size_t* end) {
	int i = *start;
	int j = *end;

	while (i <= *end && (!isprint(string[i]) || isspace(string[i]))) i++;
	while (j >= *start && (!isprint(string[j]) || isspace(string[j]))) j--;

	if (i > j) {
		return -1;
	}

	*start = i;
	*end = j;
	return 0;
}