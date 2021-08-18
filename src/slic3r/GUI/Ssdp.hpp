#ifndef slic3r_Ssdp_hpp_
#define slic3r_Ssdp_hpp_


#include <string>
#include <vector>
#include <ctime>
#include <memory>
#include <wx/string.h>
#include <wx/event.h>
#include <stdarg.h>
#if defined(_WIN32)
#include <Windows.h>
#include <WinSock2.h>
#include <iphlpapi.h>
#include <ws2tcpip.h>
#endif

#if(_WIN32_WINNT < 0x0600)
typedef short sa_family_t;
#else
typedef ADDRESS_FAMILY sa_family_t;
#endif

#define BUFSIZE   (size_t)2500
#define BBL_SDP_IP "239.255.255.250"
#define BBL_SSDP_PORT 1990
#define ERROR_BUFFER_LEN 256

#define MAX_SOCKET_NUM 100

#define LSSDP_FIELD_LEN         128
#define LSSDP_LOCATION_LEN      256

typedef struct lssdp_packet {
    char            method[LSSDP_FIELD_LEN];      // M-SEARCH, NOTIFY, RESPONSE
    char            st[LSSDP_FIELD_LEN];      // Search Target
    char            usn[LSSDP_FIELD_LEN];      // Unique Service Name
    char            location[LSSDP_LOCATION_LEN];   // Location

    /* Additional SSDP Header Fields */
    char            sm_id[LSSDP_FIELD_LEN];
    char            device_type[LSSDP_FIELD_LEN];
    long long       update_time;
} lssdp_packet;

struct SDP_CONST {
    const char* MSEARCH;
    const char* NOTIFY;
    const char* RESPONSE;

    const char* HEADER_MSEARCH;
    const char* HEADER_NOTIFY;
    const char* HEADER_RESPONSE;

    const char* ADDR_LOCALHOST;
    const char* ADDR_MULTICAST;

    void (*log_callback)(const char* file, const char* tag, int level, int line, const char* func, const char* message);
};

int bbl_init_socket();
int bbl_create_ssdp_sock(SOCKET* ssdpSock, const char* ipv4_addr);
int bbl_get_network_card_list(SOCKET* sock, int max_size);
int bbl_read_from_ssdp(SOCKET socket, char* buf, int* buf_size, int max_buf_size);

int lssdp_packet_parser(const char* data, size_t data_len, lssdp_packet* packet);
int parse_field_line(const char* data, size_t start, size_t end, lssdp_packet* packet);
int get_colon_index(const char* string, size_t start, size_t end);
int trim_spaces(const char* string, size_t* start, size_t* end);

#endif
