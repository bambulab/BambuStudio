#if defined(_MSC_VER)
#pragma warning(push)
// Third-party single-header implementation warnings:
// C4101: unused local enum variable in mp4e_write_fragment_header()
// C4996: POSIX strdup name warning
// C4005: ERROR macro can be pre-defined by Windows headers
#pragma warning(disable : 4101 4996 4005)
#define strdup _strdup
#ifdef ERROR
#pragma push_macro("ERROR")
#undef ERROR
#define MINIMP4_RESTORE_ERROR_MACRO
#endif
#endif

#define MINIMP4_IMPLEMENTATION
#include "minimp4/minimp4.h"

#if defined(_MSC_VER)
#ifdef MINIMP4_RESTORE_ERROR_MACRO
#pragma pop_macro("ERROR")
#undef MINIMP4_RESTORE_ERROR_MACRO
#endif
#undef strdup
#pragma warning(pop)
#endif
