/* copyright (c) 2010 Fredrik Kihlander, see LICENSE for more info */

#ifndef DL_DL_TYPES_H_INCLUDED
#define DL_DL_TYPES_H_INCLUDED

#include <dl/dl.h>

#include <string.h> // for memcpy
#include <stdio.h>  // for vsnprintf
#include <stdarg.h> // for va_list

// remove me!
#if defined(_MSC_VER)

        typedef signed   __int8  int8;
        typedef signed   __int16 int16;
        typedef signed   __int32 int32;
        typedef signed   __int64 int64;
        typedef unsigned __int8  uint8;
        typedef unsigned __int16 uint16;
        typedef unsigned __int32 uint32;
        typedef unsigned __int32 uint32_t;
        typedef unsigned __int64 uint64;

#elif defined(__GNUC__)

        #include <stdint.h>
        typedef int8_t   int8;
        typedef int16_t  int16;
        typedef int32_t  int32;
        typedef int64_t  int64;
        typedef uint8_t  uint8;
        typedef uint16_t uint16;
        typedef uint32_t uint32;
        typedef uint64_t uint64;

#endif

#define DL_UNUSED (void)
#define DL_ARRAY_LENGTH(Array) (sizeof(Array)/sizeof(Array[0]))
#define DL_ASSERT( expr, ... ) do { if(!(expr)) printf("ASSERT FAIL! %s %s %u\n", #expr, __FILE__, __LINE__); } while( false ) // TODO: implement me plox!

#define DL_JOIN_TOKENS(a,b) DL_JOIN_TOKENS_DO_JOIN(a,b)
#define DL_JOIN_TOKENS_DO_JOIN(a,b) DL_JOIN_TOKENS_DO_JOIN2(a,b)
#define DL_JOIN_TOKENS_DO_JOIN2(a,b) a##b

namespace dl_staticassert
{
	template <bool x> struct STATIC_ASSERTION_FAILURE;
	template <> struct STATIC_ASSERTION_FAILURE<true> { enum { value = 1 }; };
};
#define DL_STATIC_ASSERT(_Expr, _Msg) enum { DL_JOIN_TOKENS(_static_assert_enum_##_Msg, __LINE__) = sizeof(::dl_staticassert::STATIC_ASSERTION_FAILURE< (bool)( _Expr ) >) }

static const int8  DL_INT8_MAX  = 0x7F;
static const int16 DL_INT16_MAX = 0x7FFF;
static const int32 DL_INT32_MAX = 0x7FFFFFFFL;
static const int64 DL_INT64_MAX = 0x7FFFFFFFFFFFFFFFLL;
static const int8  DL_INT8_MIN  = (-DL_INT8_MAX  - 1);
static const int16 DL_INT16_MIN = (-DL_INT16_MAX - 1);
static const int32 DL_INT32_MIN = (-DL_INT32_MAX - 1);
static const int64 DL_INT64_MIN = (-DL_INT64_MAX - 1);

static const uint8  DL_UINT8_MAX  = 0xFFU;
static const uint16 DL_UINT16_MAX = 0xFFFFU;
static const uint32 DL_UINT32_MAX = 0xFFFFFFFFUL;
static const uint64 DL_UINT64_MAX = 0xFFFFFFFFFFFFFFFFULL;
static const uint8  DL_UINT8_MIN  = 0x00U;
static const uint16 DL_UINT16_MIN = 0x0000U;
static const uint32 DL_UINT32_MIN = 0x00000000UL;
static const uint64 DL_UINT64_MIN = 0x0000000000000000ULL;

#if defined( __LP64__ ) || defined( _WIN64 )
        #define DL_INT64_FMT_STR  "%ld"
        #define DL_UINT64_FMT_STR "%lu"
        typedef uint64 pint;
#else
        #define DL_INT64_FMT_STR  "%lld"
        #define DL_UINT64_FMT_STR "%llu"
        typedef uint32 pint;
#endif // DL_PTR_SIZE_32

template<class T>
struct TAlignmentOf
{
        struct CAlign { ~CAlign() {}; unsigned char m_Dummy; T m_T; };
        enum { ALIGNOF = sizeof(CAlign) - sizeof(T) };
};
#define DL_ALIGNMENTOF(Type) TAlignmentOf<Type>::ALIGNOF

enum
{
	DL_MEMBER_NAME_MAX_LEN     = 32,
	DL_TYPE_NAME_MAX_LEN       = 32,
	DL_ENUM_NAME_MAX_LEN       = 32,
	DL_ENUM_VALUE_NAME_MAX_LEN = 32,
};

#include "dl_swap.h"

static const uint32 DL_TYPELIB_VERSION    = 1; // format version for type-libraries.
static const uint32 DL_INSTANCE_VERSION   = 1; // format version for instances.
static const uint32 DL_INSTANCE_VERSION_SWAPED = DLSwapEndian(DL_INSTANCE_VERSION);
static const uint32 DL_TYPELIB_ID              = ('D'<< 24) | ('L' << 16) | ('T' << 8) | 'L';
static const uint32 DL_TYPELIB_ID_SWAPED       = DLSwapEndian(DL_TYPELIB_ID);
static const uint32 DL_INSTANCE_ID             = ('D'<< 24) | ('L' << 16) | ('D' << 8) | 'L';
static const uint32 DL_INSTANCE_ID_SWAPED      = DLSwapEndian(DL_INSTANCE_ID);

static const pint DL_NULL_PTR_OFFSET[2] =
{
	pint(DL_UINT32_MAX), // DL_PTR_SIZE_32BIT
	pint(-1)            // DL_PTR_SIZE_64BIT
};

struct SDLTypeLibraryHeader
{
	uint32 m_Id;
	uint32 m_Version;

	uint32 m_nTypes;		// number of types in typelibrary
	uint32 m_TypesOffset;	// offset from start of data where types are stored
	uint32 m_TypesSize;		// number of bytes that are types

	uint32 m_nEnums;		// number of enums in typelibrary
	uint32 m_EnumsOffset;   // offset from start of data where enums are stored
	uint32 m_EnumsSize;		// number of bytes that are enums

	uint32 m_DefaultValuesOffset;
	uint32 m_DefaultValuesSize;
};

struct SDLDataHeader
{
	uint32  m_Id;
	uint32  m_Version;
	dl_typeid_t m_RootInstanceType;
	uint32  m_InstanceSize;

	uint8   m_64BitPtr; // currently uses uint8 instead of bitfield to be compiler-compliant.
	uint8   m_Pad[3];
};

struct SDLTypeLookup
{
	dl_typeid_t type_id;
	uint32      offset;
};

enum dl_ptr_size_t
{
	DL_PTR_SIZE_32BIT = 0,
	DL_PTR_SIZE_64BIT = 1,

	DL_PTR_SIZE_HOST = sizeof(void*) == 4 ? DL_PTR_SIZE_32BIT : DL_PTR_SIZE_64BIT
};

struct SDLMember
{
	char m_Name[DL_MEMBER_NAME_MAX_LEN];
	dl_type_t m_Type;
	dl_typeid_t m_TypeID;

	uint32 m_Size[2];
	uint32 m_Alignment[2];
	uint32 m_Offset[2];

	// if M_UINT32_MAX, default value is not present, otherwise offset into default-value-data.
	uint32 m_DefaultValueOffset;

	dl_type_t AtomType()       const { return dl_type_t( m_Type & DL_TYPE_ATOM_MASK); }
	dl_type_t StorageType()    const { return dl_type_t( m_Type & DL_TYPE_STORAGE_MASK); }
	uint32  BitFieldBits()   const { return DL_EXTRACT_BITS(m_Type, DL_TYPE_BITFIELD_SIZE_MIN_BIT,   DL_TYPE_BITFIELD_SIZE_BITS_USED); }
	uint32  BitFieldOffset() const { return DL_EXTRACT_BITS(m_Type, DL_TYPE_BITFIELD_OFFSET_MIN_BIT, DL_TYPE_BITFIELD_OFFSET_BITS_USED); }
	bool    IsSimplePod()    const { return StorageType() >= DL_TYPE_STORAGE_INT8 && StorageType() <= DL_TYPE_STORAGE_FP64; }
};

struct SDLEnumValue
{
	char   m_Name[DL_ENUM_VALUE_NAME_MAX_LEN];
	uint32 m_Value;
};

#if defined( _MSC_VER )
#pragma warning(push)
#pragma warning(disable:4200) // disable warning for 0-size array
#endif // defined( _MSC_VER )

struct SDLType
{
	char      m_Name[DL_TYPE_NAME_MAX_LEN];
	uint32    m_Size[2];
	uint32    m_Alignment[2];
	// add some bytes for m_HasSubPtrs; for optimizations.
	uint32    m_nMembers;
	SDLMember m_lMembers[0];
};

struct SDLEnum
{
	char    m_Name[DL_ENUM_NAME_MAX_LEN];
	dl_typeid_t m_EnumID;

	uint32 m_nValues;
	SDLEnumValue m_lValues[0];
};

#if defined( _MSC_VER )
#pragma warning(pop)
#endif // defined( _MSC_VER )

struct dl_context
{
	void* (*alloc_func)( unsigned int size, unsigned int alignment, void* alloc_ctx );
	void  (*free_func) ( void* ptr, void* alloc_ctx );
	void* alloc_ctx;

	dl_error_msg_handler error_msg_func;
	void*                error_msg_ctx;

	struct STypeLookUp { dl_typeid_t type_id; unsigned int offset; } m_TypeLookUp[128]; // dynamic alloc?
	struct SEnumLookUp { dl_typeid_t type_id; unsigned int offset; } m_EnumLookUp[128]; // dynamic alloc?

	unsigned int m_nTypes;
	uint8*       m_TypeInfoData;
	unsigned int m_TypeInfoDataSize;

	unsigned int m_nEnums;
	uint8*       m_EnumInfoData;
	unsigned int m_EnumInfoDataSize;

	uint8*       m_pDefaultInstances;
	unsigned int m_DefaultInstancesSize;
};

inline void dl_log_error( dl_ctx_t dl_ctx, const char* fmt, ... )
{
	if( dl_ctx->error_msg_func == 0x0 )
		return;

	char buffer[256];
	va_list args;
	va_start( args, fmt );
	vsnprintf( buffer, DL_ARRAY_LENGTH(buffer), fmt, args );
	va_end(args);

	buffer[DL_ARRAY_LENGTH(buffer) - 1] = '\0';

	dl_ctx->error_msg_func( buffer, dl_ctx->error_msg_ctx );
}

/*
	return a bitfield offset on a particular platform (currently endian-ness is used to set them apart, that might break ;) )
	the bitfield offset are counted from least significant bit or most significant bit on different platforms.
*/
inline unsigned int DLBitFieldOffset(dl_endian_t _Endian, unsigned int _BFSize, unsigned int _Offset, unsigned int _nBits) { return _Endian == DL_ENDIAN_LITTLE ? _Offset : (_BFSize * 8) - _Offset - _nBits; }
inline unsigned int DLBitFieldOffset(unsigned int _BFSize, unsigned int _Offset, unsigned int _nBits)                      { return DLBitFieldOffset(DL_ENDIAN_HOST, _BFSize, _Offset, _nBits); }

DL_FORCEINLINE dl_endian_t DLOtherEndian(dl_endian_t _Endian) { return _Endian == DL_ENDIAN_LITTLE ? DL_ENDIAN_BIG : DL_ENDIAN_LITTLE; }

static inline pint DLPodSize(dl_type_t _Type)
{
	switch(_Type & DL_TYPE_STORAGE_MASK)
	{
		case DL_TYPE_STORAGE_INT8:  
		case DL_TYPE_STORAGE_UINT8:  return 1;

		case DL_TYPE_STORAGE_INT16: 
		case DL_TYPE_STORAGE_UINT16: return 2;

		case DL_TYPE_STORAGE_INT32: 
		case DL_TYPE_STORAGE_UINT32: 
		case DL_TYPE_STORAGE_FP32: 
		case DL_TYPE_STORAGE_ENUM:   return 4;

		case DL_TYPE_STORAGE_INT64: 
		case DL_TYPE_STORAGE_UINT64: 
		case DL_TYPE_STORAGE_FP64:   return 8;

		default:
			DL_ASSERT(false && "This should not happen!");
			return 0;
	}
}

static inline const SDLType* DLFindType(dl_ctx_t dl_ctx, dl_typeid_t type_id)
{
	// linear search right now!
	for(unsigned int i = 0; i < dl_ctx->m_nTypes; ++i)
		if(dl_ctx->m_TypeLookUp[i].type_id == type_id)
			return (SDLType*)(dl_ctx->m_TypeInfoData + dl_ctx->m_TypeLookUp[i].offset);

	return 0x0;
}

static inline const SDLEnum* DLFindEnum(dl_ctx_t dl_ctx, dl_typeid_t type_id)
{
	for (unsigned int i = 0; i < dl_ctx->m_nEnums; ++i)
		if( dl_ctx->m_EnumLookUp[i].type_id == type_id )
			return (SDLEnum*)( dl_ctx->m_EnumInfoData + dl_ctx->m_EnumLookUp[i].offset );

	return 0x0;
}

static inline uint32 DLFindEnumValue(const SDLEnum* _pEnum, const char* _Name, unsigned int _NameLen)
{
	for (unsigned int j = 0; j < _pEnum->m_nValues; ++j)
		if(strncmp(_pEnum->m_lValues[j].m_Name, _Name, _NameLen) == 0)
			return _pEnum->m_lValues[j].m_Value;
	return 0;
}

static inline const char* DLFindEnumName(dl_ctx_t _Context, dl_typeid_t _EnumHash, uint32 _Value)
{
	const SDLEnum* pEnum = DLFindEnum(_Context, _EnumHash);

	if(pEnum != 0x0)
	{
		for (unsigned int j = 0; j < pEnum->m_nValues; ++j)
			if(pEnum->m_lValues[j].m_Value == _Value)
				return pEnum->m_lValues[j].m_Name;
	}

	return "UnknownEnum!";
}

DL_FORCEINLINE static uint32_t DLHashBuffer(const uint8* _pBuffer, unsigned int _Bytes, uint32_t _BaseHash)
{
	DL_ASSERT(_pBuffer != 0x0 && "You made wrong!");
	uint32 Hash = _BaseHash + 5381;
	for (unsigned int i = 0; i < _Bytes; i++)
		Hash = (Hash * uint32(33)) + *((uint8*)_pBuffer + i);
	return Hash - 5381;
}

DL_FORCEINLINE static uint32_t DLHashString(const char* _pStr, uint32_t _BaseHash = 0)
{
	DL_ASSERT(_pStr != 0x0 && "You made wrong!");
	uint32 Hash = _BaseHash + 5381;
	for (unsigned int i = 0; _pStr[i] != 0; i++)
		Hash = (Hash * uint32(33)) + _pStr[i];
	return Hash - 5381; // So empty string == 0
}

static inline unsigned int DLFindMember(const SDLType* _pType, dl_typeid_t _NameHash)
{
	// TODO: currently members only hold name, but they should hold a hash!

	for(unsigned int i = 0; i < _pType->m_nMembers; ++i)
	{
		const SDLMember* pMember = _pType->m_lMembers + i;

		if(DLHashString(pMember->m_Name) == _NameHash)
			return i;
	}

	return _pType->m_nMembers + 1;
}

#endif // DL_DL_TYPES_H_INCLUDED
