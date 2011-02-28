#include <dl/dl.h>

#include "dl_types.h"
#include "dl_swap.h"

#include "dl_temp.h"
#include "container/dl_array.h"

#include <stdio.h> // printf
#include <stdlib.h> // malloc, free

// TODO: bug! dl_internal_default_alloc do not follow alignment.
static void* dl_internal_default_alloc( unsigned int size, unsigned int alignment, void* alloc_ctx ) { DL_UNUSED(alignment); DL_UNUSED(alloc_ctx); return malloc(size); }
static void  dl_internal_default_free ( void* ptr, void* alloc_ctx ) {  DL_UNUSED(alloc_ctx); free(ptr); }

static void* dl_internal_realloc( dl_ctx_t dl_ctx, void* ptr, unsigned int old_size, unsigned int new_size, unsigned int alignment )
{
	if( old_size == new_size )
		return ptr;

	void* new_ptr = dl_ctx->alloc_func( new_size, alignment, dl_ctx->alloc_ctx );

	if( ptr != 0x0 )
	{
		memmove( new_ptr, ptr, old_size );
		dl_ctx->free_func( ptr, dl_ctx->alloc_ctx );
	}

	return new_ptr;
}

static void* dl_internal_append_data( dl_ctx_t dl_ctx, void* old_data, unsigned int old_data_size, const void* new_data, unsigned int new_data_size )
{
	uint8* data = (uint8*)dl_internal_realloc( dl_ctx, old_data, old_data_size, old_data_size + new_data_size, sizeof(void*) );
	memcpy( data + old_data_size, new_data, new_data_size );
	return data;
}

dl_error_t dl_context_create( dl_ctx_t* dl_ctx, dl_create_params_t* create_params )
{
	dl_alloc_func the_alloc = create_params->alloc_func != 0x0 ? create_params->alloc_func : dl_internal_default_alloc;
	dl_free_func  the_free  = create_params->free_func  != 0x0 ? create_params->free_func  : dl_internal_default_free;
	dl_context*   ctx       = (dl_context*)the_alloc( sizeof(dl_context), sizeof(void*), create_params->alloc_ctx );

	if(ctx == 0x0)
		return DL_ERROR_OUT_OF_LIBRARY_MEMORY;

	memset(ctx, 0x0, sizeof(dl_context));

	ctx->alloc_func     = the_alloc;
	ctx->free_func      = the_free;
	ctx->alloc_ctx      = create_params->alloc_ctx;
	ctx->error_msg_func = create_params->error_msg_func;
	ctx->error_msg_ctx  = create_params->error_msg_ctx;

	*dl_ctx = ctx;

	return DL_ERROR_OK;
}

dl_error_t dl_context_destroy(dl_ctx_t dl_ctx)
{
	dl_ctx->free_func( dl_ctx->m_TypeInfoData, dl_ctx->alloc_ctx );
	dl_ctx->free_func( dl_ctx->m_EnumInfoData, dl_ctx->alloc_ctx );
	dl_ctx->free_func( dl_ctx->default_data, dl_ctx->alloc_ctx );
	dl_ctx->free_func( dl_ctx, dl_ctx->alloc_ctx );
	return DL_ERROR_OK;
}

// implemented in dl_convert.cpp
dl_error_t dl_internal_convert_no_header( dl_ctx_t       dl_ctx,
                                          unsigned char* packed_instance, unsigned char* packed_instance_base,
                                          unsigned char* out_instance,    unsigned int   out_instance_size,
                                          unsigned int*  needed_size,
                                          dl_endian_t    src_endian,      dl_endian_t    out_endian,
                                          dl_ptr_size_t  src_ptr_size,    dl_ptr_size_t  out_ptr_size,
                                          const SDLType* root_type,       unsigned int   base_offset );

struct SPatchedInstances
{
	CArrayStatic<const uint8*, 1024> m_lpPatched;

	bool IsPatched(const uint8* _pInstance)
	{
		for (unsigned int iPatch = 0; iPatch < m_lpPatched.Len(); ++iPatch)
			if(m_lpPatched[iPatch] == _pInstance)
				return true;
		return false;
	}
};

static void DLPatchPtr(const uint8* _pPtr, const uint8* _pBaseData)
{
	union ReadMe
	{
		pint         m_Offset;
		const uint8* m_RealData;
	};

	ReadMe* pReadMe     = (ReadMe*)_pPtr;
	if (pReadMe->m_Offset == DL_NULL_PTR_OFFSET[DL_PTR_SIZE_HOST])
		pReadMe->m_RealData = 0x0;
	else
		pReadMe->m_RealData = _pBaseData + pReadMe->m_Offset;
};

static dl_error_t DLPatchLoadedPtrs( dl_ctx_t         _Context,
								     SPatchedInstances* _pPatchedInstances,
								     const uint8*       _pInstance,
								     const SDLType*     _pType,
								     const uint8*       _pBaseData )
{
	// TODO: Optimize this please, linear search might not be the best if many subinstances is in file!
	if(_pPatchedInstances->IsPatched(_pInstance))
		return DL_ERROR_OK;

	_pPatchedInstances->m_lpPatched.Add(_pInstance);

	for(uint32 iMember = 0; iMember < _pType->m_nMembers; ++iMember)
	{
		const SDLMember& Member = _pType->m_lMembers[iMember];
		const uint8* pMemberData = _pInstance + Member.m_Offset[DL_PTR_SIZE_HOST];

		dl_type_t AtomType    = Member.AtomType();
		dl_type_t StorageType = Member.StorageType();

		switch(AtomType)
		{
			case DL_TYPE_ATOM_POD:
			{
				switch(StorageType)
				{
					case DL_TYPE_STORAGE_STR: DLPatchPtr(pMemberData, _pBaseData); break;
					case DL_TYPE_STORAGE_STRUCT:
					{
						const SDLType* pStructType = DLFindType(_Context, Member.m_TypeID);
						DLPatchLoadedPtrs(_Context, _pPatchedInstances, pMemberData, pStructType, _pBaseData);
					}
					break;
					case DL_TYPE_STORAGE_PTR:
					{
						uint8** ppPtr = (uint8**)pMemberData;
						DLPatchPtr(pMemberData, _pBaseData);

						if(*ppPtr != 0x0)
						{
							const SDLType* pSubType = DLFindType(_Context, Member.m_TypeID);
							DLPatchLoadedPtrs(_Context, _pPatchedInstances, *ppPtr, pSubType, _pBaseData);
						}
					}
					break;
					default:
						// ignore
						break;
				}
			}
			break;
			case DL_TYPE_ATOM_ARRAY:
			{
				if(StorageType == DL_TYPE_STORAGE_STR || StorageType == DL_TYPE_STORAGE_STRUCT)
				{
					DLPatchPtr(pMemberData, _pBaseData);
					const uint8* pArrayData = *(const uint8**)pMemberData;

					uint32 Count = *(uint32*)(pMemberData + sizeof(void*));

					if(Count > 0)
					{
						if(StorageType == DL_TYPE_STORAGE_STRUCT)
						{
							// patch sub-ptrs!
							const SDLType* pSubType = DLFindType(_Context, Member.m_TypeID);
							uint32 Size = AlignUp(pSubType->m_Size[DL_PTR_SIZE_HOST], pSubType->m_Alignment[DL_PTR_SIZE_HOST]);

							for(uint32 iElemOffset = 0; iElemOffset < Count * Size; iElemOffset += Size)
								DLPatchLoadedPtrs(_Context, _pPatchedInstances, pArrayData + iElemOffset, pSubType, _pBaseData);
						}
						else
						{
							for(uint32 iElemOffset = 0; iElemOffset < Count * sizeof(char*); iElemOffset += sizeof(char*))
								DLPatchPtr(pArrayData + iElemOffset, _pBaseData);
						}
					}
				}
				else // pod
					DLPatchPtr(pMemberData, _pBaseData);
			}
			break;

			case DL_TYPE_ATOM_INLINE_ARRAY:
			{
				if(StorageType == DL_TYPE_STORAGE_STR)
				{
					for(pint iElemOffset = 0; iElemOffset < Member.m_Size[DL_PTR_SIZE_HOST]; iElemOffset += sizeof(char*))
						DLPatchPtr(pMemberData + iElemOffset, _pBaseData);
				}
			}
			break;

			case DL_TYPE_ATOM_BITFIELD:
			// ignore
			break;

		default:
			DL_ASSERT(false && "Unknown atom type");
		}
	}
	return DL_ERROR_OK;
}

struct SOneMemberType
{
	SOneMemberType(const SDLMember* _pMember)
	{
		m_Size[0] = _pMember->m_Size[0];
		m_Size[1] = _pMember->m_Size[1];
		m_Alignment[0] = _pMember->m_Alignment[0];
		m_Alignment[1] = _pMember->m_Alignment[1];
		m_nMembers = 1;

		memcpy(&m_Member, _pMember, sizeof(SDLMember));
		m_Member.m_Offset[0] = 0;
		m_Member.m_Offset[0] = 0;
	}

	char      m_Name[DL_TYPE_NAME_MAX_LEN];
	uint32    m_Size[2];
	uint32    m_Alignment[2];
	uint32    m_nMembers;
	SDLMember m_Member;
};

DL_STATIC_ASSERT(sizeof(SOneMemberType) - sizeof(SDLMember) == sizeof(SDLType), these_need_same_size);

static dl_error_t dl_internal_load_type_library_defaults(dl_ctx_t dl_ctx, unsigned int first_new_type, const uint8* default_data, unsigned int default_data_size)
{
	if( default_data_size == 0 ) return DL_ERROR_OK;

	if( dl_ctx->default_data != 0x0 )
		return DL_ERROR_OUT_OF_DEFAULT_VALUE_SLOTS;

	dl_ctx->default_data = (uint8*)dl_ctx->alloc_func( default_data_size * 2, sizeof(void*), dl_ctx->alloc_ctx ); // TODO: times 2 here need to be fixed!

	uint8* pDst = dl_ctx->default_data;

	// ptr-patch and convert to native
	for(uint32 iType = first_new_type; iType < dl_ctx->m_nTypes; ++iType)
	{
		const SDLType* pType = (SDLType*)(dl_ctx->m_TypeInfoData + dl_ctx->m_TypeLookUp[iType].offset);
		for(uint32 iMember = 0; iMember < pType->m_nMembers; ++iMember)
		{
			SDLMember* pMember = (SDLMember*)pType->m_lMembers + iMember;
			if(pMember->m_DefaultValueOffset == DL_UINT32_MAX)
				continue;

			pDst                          = AlignUp( pDst, pMember->m_Alignment[DL_PTR_SIZE_HOST] );
			uint8* pSrc                   = (uint8*)default_data + pMember->m_DefaultValueOffset;
			pint BaseOffset               = pint( pDst ) - pint( dl_ctx->default_data );
			pMember->m_DefaultValueOffset = uint32( BaseOffset );

			SOneMemberType Dummy(pMember);
			unsigned int NeededSize;
			dl_internal_convert_no_header( dl_ctx,
										   pSrc, (unsigned char*)default_data,
										   pDst, 1337, // need to check this size ;) Should be the remainder of space in m_pDefaultInstances.
										   &NeededSize,
										   DL_ENDIAN_LITTLE,  DL_ENDIAN_HOST,
										   DL_PTR_SIZE_32BIT, DL_PTR_SIZE_HOST,
										   (const SDLType*)&Dummy, (unsigned int)BaseOffset ); // TODO: Ugly cast, remove plox!

			SPatchedInstances PI;
			DLPatchLoadedPtrs( dl_ctx, &PI, pDst, (const SDLType*)&Dummy, dl_ctx->default_data );

			pDst += NeededSize;
		}
	}

	return DL_ERROR_OK;
}

static void dl_internal_read_typelibrary_header( SDLTypeLibraryHeader* header, const uint8* data )
{
	memcpy(header, data, sizeof(SDLTypeLibraryHeader));

	if(DL_ENDIAN_HOST == DL_ENDIAN_BIG)
	{
		header->m_Id          = DLSwapEndian(header->m_Id);
		header->m_Version     = DLSwapEndian(header->m_Version);

		header->m_nTypes      = DLSwapEndian(header->m_nTypes);
		header->m_TypesOffset = DLSwapEndian(header->m_TypesOffset);
		header->m_TypesSize   = DLSwapEndian(header->m_TypesSize);

		header->m_nEnums      = DLSwapEndian(header->m_nEnums);
		header->m_EnumsOffset = DLSwapEndian(header->m_EnumsOffset);
		header->m_EnumsSize   = DLSwapEndian(header->m_EnumsSize);

		header->m_DefaultValuesOffset = DLSwapEndian(header->m_DefaultValuesOffset);
		header->m_DefaultValuesSize   = DLSwapEndian(header->m_DefaultValuesSize);
	}
}

dl_error_t dl_context_load_type_library( dl_ctx_t dl_ctx, const unsigned char* lib_data, unsigned int lib_data_size )
{
	if(lib_data_size < sizeof(SDLTypeLibraryHeader))
		return DL_ERROR_MALFORMED_DATA;

	SDLTypeLibraryHeader Header;
	dl_internal_read_typelibrary_header(&Header, lib_data);

	if( Header.m_Id      != DL_TYPELIB_ID )      return DL_ERROR_MALFORMED_DATA;
	if( Header.m_Version != DL_TYPELIB_VERSION ) return DL_ERROR_VERSION_MISMATCH;

	// store type-info data.
	dl_ctx->m_TypeInfoData = (uint8*)dl_internal_append_data( dl_ctx, dl_ctx->m_TypeInfoData, dl_ctx->m_TypeInfoDataSize, lib_data + Header.m_TypesOffset, Header.m_TypesSize );

	// read type-lookup table
	SDLTypeLookup* _pFromData = (SDLTypeLookup*)(lib_data + sizeof(SDLTypeLibraryHeader));
	for(uint32 i = dl_ctx->m_nTypes; i < dl_ctx->m_nTypes + Header.m_nTypes; ++i)
	{
		dl_context::STypeLookUp* look = dl_ctx->m_TypeLookUp + i;

		if(DL_ENDIAN_HOST == DL_ENDIAN_BIG)
		{
			look->type_id  = DLSwapEndian(_pFromData->type_id);
			look->offset   = dl_ctx->m_TypeInfoDataSize + DLSwapEndian(_pFromData->offset);
			SDLType* pType = (SDLType*)(dl_ctx->m_TypeInfoData + look->offset );

			pType->m_Size[DL_PTR_SIZE_32BIT]      = DLSwapEndian(pType->m_Size[DL_PTR_SIZE_32BIT]);
			pType->m_Size[DL_PTR_SIZE_64BIT]      = DLSwapEndian(pType->m_Size[DL_PTR_SIZE_64BIT]);
			pType->m_Alignment[DL_PTR_SIZE_32BIT] = DLSwapEndian(pType->m_Alignment[DL_PTR_SIZE_32BIT]);
			pType->m_Alignment[DL_PTR_SIZE_64BIT] = DLSwapEndian(pType->m_Alignment[DL_PTR_SIZE_64BIT]);
			pType->m_nMembers                     = DLSwapEndian(pType->m_nMembers);

			for(uint32 i = 0; i < pType->m_nMembers; ++i)
			{
				SDLMember* pMember = pType->m_lMembers + i;

				pMember->m_Type                         = DLSwapEndian(pMember->m_Type);
				pMember->m_TypeID	                    = DLSwapEndian(pMember->m_TypeID);
				pMember->m_Size[DL_PTR_SIZE_32BIT]      = DLSwapEndian(pMember->m_Size[DL_PTR_SIZE_32BIT]);
				pMember->m_Size[DL_PTR_SIZE_64BIT]      = DLSwapEndian(pMember->m_Size[DL_PTR_SIZE_64BIT]);
				pMember->m_Offset[DL_PTR_SIZE_32BIT]    = DLSwapEndian(pMember->m_Offset[DL_PTR_SIZE_32BIT]);
				pMember->m_Offset[DL_PTR_SIZE_64BIT]    = DLSwapEndian(pMember->m_Offset[DL_PTR_SIZE_64BIT]);
				pMember->m_Alignment[DL_PTR_SIZE_32BIT] = DLSwapEndian(pMember->m_Alignment[DL_PTR_SIZE_32BIT]);
				pMember->m_Alignment[DL_PTR_SIZE_64BIT] = DLSwapEndian(pMember->m_Alignment[DL_PTR_SIZE_64BIT]);
				pMember->m_DefaultValueOffset           = DLSwapEndian(pMember->m_DefaultValueOffset);
			}
		}
		else
		{
			look->type_id = _pFromData->type_id;
			look->offset  = dl_ctx->m_TypeInfoDataSize + _pFromData->offset;
		}

		++_pFromData;
	}

	dl_ctx->m_nTypes           += Header.m_nTypes;
	dl_ctx->m_TypeInfoDataSize += Header.m_TypesSize;

	// store enum-info data.
	dl_ctx->m_EnumInfoData = (uint8*)dl_internal_append_data( dl_ctx, dl_ctx->m_EnumInfoData, dl_ctx->m_EnumInfoDataSize, lib_data + Header.m_EnumsOffset, Header.m_EnumsSize );

	// read enum-lookup table
	SDLTypeLookup* _pEnumFromData = (SDLTypeLookup*)(lib_data + Header.m_TypesOffset + Header.m_TypesSize);
	for(uint32 i = dl_ctx->m_nEnums; i < dl_ctx->m_nEnums + Header.m_nEnums; ++i)
	{
		dl_context::SEnumLookUp* look = dl_ctx->m_EnumLookUp + i;

		if(DL_ENDIAN_HOST == DL_ENDIAN_BIG)
		{
			look->type_id  = DLSwapEndian(_pEnumFromData->type_id);
			look->offset   = dl_ctx->m_EnumInfoDataSize + DLSwapEndian(_pEnumFromData->offset);
			SDLEnum* pEnum = (SDLEnum*)(dl_ctx->m_EnumInfoData + look->offset);

			pEnum->m_EnumID  = DLSwapEndian(pEnum->m_EnumID);
			pEnum->m_nValues = DLSwapEndian(pEnum->m_nValues);
			for(uint32 i = 0; i < pEnum->m_nValues; ++i)
			{
				SDLEnumValue* pValue = pEnum->m_lValues + i;
				pValue->m_Value = DLSwapEndian(pValue->m_Value);
			}
		}
		else
		{
			look->type_id = _pEnumFromData->type_id;
			look->offset  = dl_ctx->m_EnumInfoDataSize + _pEnumFromData->offset;
		}

		++_pEnumFromData;
	}

	dl_ctx->m_nEnums           += Header.m_nEnums;
	dl_ctx->m_EnumInfoDataSize += Header.m_EnumsSize;

	return dl_internal_load_type_library_defaults( dl_ctx, dl_ctx->m_nTypes - Header.m_nTypes, lib_data + Header.m_DefaultValuesOffset, Header.m_DefaultValuesSize );
}

dl_error_t dl_instance_load( dl_ctx_t             dl_ctx,          dl_typeid_t type,
                             void*                instance,        unsigned int instance_size,
                             const unsigned char* packed_instance, unsigned int packed_instance_size,
                             unsigned int*        consumed )
{
	SDLDataHeader* header = (SDLDataHeader*)packed_instance;

	if( packed_instance_size < sizeof(SDLDataHeader) ) return DL_ERROR_MALFORMED_DATA;
	if( header->m_Id == DL_INSTANCE_ID_SWAPED )        return DL_ERROR_ENDIAN_MISMATCH;
	if( header->m_Id != DL_INSTANCE_ID )               return DL_ERROR_MALFORMED_DATA;
	if( header->m_Version != DL_INSTANCE_VERSION )     return DL_ERROR_VERSION_MISMATCH;
	if( header->m_RootInstanceType != type )           return DL_ERROR_TYPE_MISMATCH;
	if( header->m_InstanceSize > instance_size )       return DL_ERROR_BUFFER_TO_SMALL;

	const SDLType* pType = DLFindType(dl_ctx, header->m_RootInstanceType);
	if(pType == 0x0)
		return DL_ERROR_TYPE_NOT_FOUND;

	// TODO: Temporary disabled due to CL doing some magic stuff!!! 
	// Structs allocated on qstack seems to be unaligned!!!
	// if( !IsAlign( instance, pType->m_Alignment[DL_PTR_SIZE_HOST] ) )
	//	return DL_ERROR_BAD_ALIGNMENT;

	memcpy(instance, packed_instance + sizeof(SDLDataHeader), header->m_InstanceSize);

	SPatchedInstances PI;
	DLPatchLoadedPtrs(dl_ctx, &PI, (uint8*)instance, pType, (uint8*)instance);

	if( consumed )
		*consumed = header->m_InstanceSize + sizeof(SDLDataHeader);

	return DL_ERROR_OK;
}

#if 1 // BinaryWriterVerbose
	#define DL_LOG_BIN_WRITER_VERBOSE(_Fmt, ...)
#else
	#define DL_LOG_BIN_WRITER_VERBOSE(_Fmt, ...) printf("DL:" _Fmt, ##__VA_ARGS__)
#endif

// TODO: This should use the CDLBinaryWriter!
class CDLBinStoreContext
{
public:
	CDLBinStoreContext(uint8* _OutData, pint _OutDataSize, bool _Dummy)
		: m_OutData(_OutData)
		, m_OutDataSize(_OutDataSize)
		, m_Dummy(_Dummy)
		, m_WritePtr(0)
		, m_WriteEnd(0) {}

	void Write(void* _pData, pint _DataSize)
	{
		if( !m_Dummy && ( m_WritePtr + _DataSize <= m_OutDataSize ) )
		{
			DL_ASSERT(m_WritePtr < m_OutDataSize);
			DL_LOG_BIN_WRITER_VERBOSE("Write: %lu + %lu (%lu)", m_WritePtr, _DataSize, *(pint*)_pData);
			memcpy(m_OutData + m_WritePtr, _pData, _DataSize);
		}
		m_WritePtr += _DataSize;
		m_WriteEnd = Max(m_WriteEnd, m_WritePtr);
	};

	void Align(pint _Alignment)
	{
		if(IsAlign((void*)m_WritePtr, _Alignment))
			return;

		pint MoveMe = AlignUp(m_WritePtr, _Alignment) - m_WritePtr;

		if( !m_Dummy && ( m_WritePtr + MoveMe <= m_OutDataSize ) )
		{
			memset(m_OutData + m_WritePtr, 0x0, MoveMe);
			DL_LOG_BIN_WRITER_VERBOSE("Align: %lu + %lu", m_WritePtr, MoveMe);
		}
		m_WritePtr += MoveMe;
		m_WriteEnd = Max(m_WriteEnd, m_WritePtr);
	}

	pint Tell()              { return m_WritePtr; }
	void SeekSet(pint Pos)   { DL_LOG_BIN_WRITER_VERBOSE("Seek set: %lu", Pos);                       m_WritePtr = Pos;  }
	void SeekEnd()           { DL_LOG_BIN_WRITER_VERBOSE("Seek end: %lu", m_WriteEnd);                m_WritePtr = m_WriteEnd; }
	void Reserve(pint Bytes) { DL_LOG_BIN_WRITER_VERBOSE("Reserve: end %lu + %lu", m_WriteEnd, Bytes); m_WriteEnd += Bytes; }

	pint FindWrittenPtr(void* _Ptr)
	{
		for (unsigned int iPtr = 0; iPtr < m_WrittenPtrs.Len(); ++iPtr)
			if(m_WrittenPtrs[iPtr].m_pPtr == _Ptr)
				return m_WrittenPtrs[iPtr].m_Pos;

		return pint(-1);
	}

	void AddWrittenPtr(void* _Ptr, pint _Pos) { m_WrittenPtrs.Add(SWrittenPtr(_Pos, _Ptr)); }

private:
	struct SWrittenPtr
	{
 		SWrittenPtr() {}
 		SWrittenPtr(pint _Pos, void* _pPtr) : m_Pos(_Pos), m_pPtr(_pPtr) {}
		pint  m_Pos;
		void* m_pPtr;
	};

	CArrayStatic<SWrittenPtr, 128> m_WrittenPtrs;

	uint8* m_OutData;
	pint   m_OutDataSize;
	bool   m_Dummy;
	pint   m_WritePtr;
	pint   m_WriteEnd;
};

static void DLInternalStoreString(const uint8* _pInstance, CDLBinStoreContext* _pStoreContext)
{
	char* pTheString = *(char**)_pInstance;
	pint Pos = _pStoreContext->Tell();
	_pStoreContext->SeekEnd();
	pint Offset = _pStoreContext->Tell();
	_pStoreContext->Write(pTheString, strlen(pTheString) + 1);
	_pStoreContext->SeekSet(Pos);
	_pStoreContext->Write(&Offset, sizeof(pint));	
}

static dl_error_t dl_internal_instance_store(dl_ctx_t dl_ctx, const SDLType* dl_type, uint8* instance, CDLBinStoreContext* store_ctx);

static dl_error_t DLInternalStoreMember(dl_ctx_t _Context, const SDLMember* _pMember, uint8* _pInstance, CDLBinStoreContext* _pStoreContext)
{
	_pStoreContext->Align(_pMember->m_Alignment[DL_PTR_SIZE_HOST]);

	dl_type_t AtomType    = dl_type_t(_pMember->m_Type & DL_TYPE_ATOM_MASK);
	dl_type_t StorageType = dl_type_t(_pMember->m_Type & DL_TYPE_STORAGE_MASK);

	switch (AtomType)
	{
		case DL_TYPE_ATOM_POD:
		{
			switch(StorageType)
			{
				case DL_TYPE_STORAGE_STRUCT:
				{
					const SDLType* pSubType = DLFindType(_Context, _pMember->m_TypeID);
					if(pSubType == 0x0)
					{
						dl_log_error( _Context, "Could not find subtype for member %s", _pMember->m_Name );
						return DL_ERROR_TYPE_NOT_FOUND;
					}
					dl_internal_instance_store(_Context, pSubType, _pInstance, _pStoreContext );
				}
				break;
				case DL_TYPE_STORAGE_STR:
					DLInternalStoreString(_pInstance, _pStoreContext);
					break;
				case DL_TYPE_STORAGE_PTR:
				{
					uint8* pData = *(uint8**)_pInstance;
					pint Offset = _pStoreContext->FindWrittenPtr(pData);

					if(pData == 0x0) // Null-pointer, store pint(-1) to signal to patching!
					{
						DL_ASSERT(Offset == pint(-1) && "This pointer should not have been found among the written ptrs!");
						// keep the -1 in Offset and store it to ptr.
					}
					else if(Offset == pint(-1)) // has not been written yet!
					{
						pint Pos = _pStoreContext->Tell();
						_pStoreContext->SeekEnd();

						const SDLType* pSubType = DLFindType(_Context, _pMember->m_TypeID);
						pint Size = AlignUp(pSubType->m_Size[DL_PTR_SIZE_HOST], pSubType->m_Alignment[DL_PTR_SIZE_HOST]);
						_pStoreContext->Align(pSubType->m_Alignment[DL_PTR_SIZE_HOST]);

						Offset = _pStoreContext->Tell();

						// write data!
						_pStoreContext->Reserve(Size); // reserve space for ptr so subdata is placed correctly

						_pStoreContext->AddWrittenPtr(pData, Offset);

						dl_internal_instance_store(_Context, pSubType, pData, _pStoreContext);

						_pStoreContext->SeekSet(Pos);
					}

					_pStoreContext->Write(&Offset, sizeof(pint));
				}
				break;
				default: // default is a standard pod-type
					DL_ASSERT(_pMember->IsSimplePod() || StorageType == DL_TYPE_STORAGE_ENUM);
					_pStoreContext->Write(_pInstance, _pMember->m_Size[DL_PTR_SIZE_HOST]);
					break;
			}
		}
		return DL_ERROR_OK;

		case DL_TYPE_ATOM_INLINE_ARRAY:
		{
			switch(StorageType)
			{
				case DL_TYPE_STORAGE_STRUCT:
					_pStoreContext->Write(_pInstance, _pMember->m_Size[DL_PTR_SIZE_HOST]); // TODO: I Guess that this is a bug! Will it fix ptrs well?
					break;
				case DL_TYPE_STORAGE_STR:
				{
					uint32 Count = _pMember->m_Size[DL_PTR_SIZE_HOST] / sizeof(char*);

					for(uint32 iElem = 0; iElem < Count; ++iElem)
						DLInternalStoreString(_pInstance + (iElem * sizeof(char*)), _pStoreContext);
				}
				break;
				default: // default is a standard pod-type
					DL_ASSERT(_pMember->IsSimplePod() || StorageType == DL_TYPE_STORAGE_ENUM);
					_pStoreContext->Write(_pInstance, _pMember->m_Size[DL_PTR_SIZE_HOST]);
					break;
			}
		}
		return DL_ERROR_OK;

		case DL_TYPE_ATOM_ARRAY:
		{
			pint Size = 0;
			const SDLType* pSubType = 0x0;

			uint8* pDataPtr = _pInstance;
			uint32 Count = *(uint32*)(pDataPtr + sizeof(void*));

			pint Offset = 0;

			if( Count == 0 )
				Offset = DL_NULL_PTR_OFFSET[ DL_PTR_SIZE_HOST ];
			else
			{
				pint Pos = _pStoreContext->Tell();
				_pStoreContext->SeekEnd();

				switch(StorageType)
				{
					case DL_TYPE_STORAGE_STRUCT:
						pSubType = DLFindType(_Context, _pMember->m_TypeID);
						Size = AlignUp(pSubType->m_Size[DL_PTR_SIZE_HOST], pSubType->m_Alignment[DL_PTR_SIZE_HOST]);
						_pStoreContext->Align(pSubType->m_Alignment[DL_PTR_SIZE_HOST]);
						break;
					case DL_TYPE_STORAGE_STR:
						Size = sizeof(void*);
						_pStoreContext->Align(Size);
						break;
					default:
						Size = DLPodSize(_pMember->m_Type);
						_pStoreContext->Align(Size);
				}

				Offset = _pStoreContext->Tell();

				// write data!
				_pStoreContext->Reserve(Count * Size); // reserve space for array so subdata is placed correctly

				uint8* pData = *(uint8**)pDataPtr;

				switch(StorageType)
				{
					case DL_TYPE_STORAGE_STRUCT:
						for (unsigned int iElem = 0; iElem < Count; ++iElem)
							dl_internal_instance_store(_Context, pSubType, pData + (iElem * Size), _pStoreContext);
						break;
					case DL_TYPE_STORAGE_STR:
						for (unsigned int iElem = 0; iElem < Count; ++iElem)
							DLInternalStoreString(pData + (iElem * Size), _pStoreContext);
						break;
					default:
						for (unsigned int iElem = 0; iElem < Count; ++iElem)
							_pStoreContext->Write(pData + (iElem * Size), Size); break;
				}

				_pStoreContext->SeekSet(Pos);
			}

			// make room for ptr
			_pStoreContext->Write(&Offset, sizeof(pint));

			// write count
			_pStoreContext->Write(&Count, sizeof(uint32));
		}
		return DL_ERROR_OK;

		case DL_TYPE_ATOM_BITFIELD:
			_pStoreContext->Write(_pInstance, _pMember->m_Size[DL_PTR_SIZE_HOST]);
		break;

		default:
			DL_ASSERT(false && "Invalid ATOM-type!");
	}

	return DL_ERROR_OK;
}

static dl_error_t dl_internal_instance_store(dl_ctx_t dl_ctx, const SDLType* dl_type, uint8* instance, CDLBinStoreContext* store_ctx)
{
	bool bLastWasBF = false;

	for(uint32 member = 0; member < dl_type->m_nMembers; ++member)
	{
		const SDLMember& Member = dl_type->m_lMembers[member];

		if(!bLastWasBF || Member.AtomType() != DL_TYPE_ATOM_BITFIELD)
		{
			dl_error_t Err = DLInternalStoreMember(dl_ctx, &Member, instance + Member.m_Offset[DL_PTR_SIZE_HOST], store_ctx);
			if(Err != DL_ERROR_OK)
				return Err;
		}

		bLastWasBF = Member.AtomType() == DL_TYPE_ATOM_BITFIELD;
	}

	return DL_ERROR_OK;
}

dl_error_t dl_instance_store( dl_ctx_t       dl_ctx,     dl_typeid_t  type,            void*         instance,
							  unsigned char* out_buffer, unsigned int out_buffer_size, unsigned int* produced_bytes )
{
	if( out_buffer_size > 0 && out_buffer_size <= sizeof(SDLDataHeader) )
		return DL_ERROR_BUFFER_TO_SMALL;

	const SDLType* pType = DLFindType(dl_ctx, type);
	if(pType == 0x0)
		return DL_ERROR_TYPE_NOT_FOUND;

	// write header
	SDLDataHeader Header;
	Header.m_Id = DL_INSTANCE_ID;
	Header.m_Version = DL_INSTANCE_VERSION;
	Header.m_RootInstanceType = type;
	Header.m_InstanceSize = 0;
	Header.m_64BitPtr = sizeof(void*) == 8 ? 1 : 0;

	unsigned char* store_ctx_buffer      = 0x0;
	unsigned int   store_ctx_buffer_size = 0;
	bool           store_ctx_is_dummy    = out_buffer_size == 0;

	if( out_buffer_size > 0 )
	{
		memcpy(out_buffer, &Header, sizeof(SDLDataHeader));
		store_ctx_buffer      = out_buffer + sizeof(SDLDataHeader);
		store_ctx_buffer_size = out_buffer_size - sizeof(SDLDataHeader);
	}

	CDLBinStoreContext StoreContext( store_ctx_buffer, store_ctx_buffer_size, store_ctx_is_dummy );

	StoreContext.Reserve(pType->m_Size[DL_PTR_SIZE_HOST]);
	StoreContext.AddWrittenPtr(instance, 0); // if pointer refere to root-node, it can be found at offset 0

	dl_error_t err = dl_internal_instance_store(dl_ctx, pType, (uint8*)instance, &StoreContext);

	// write instance size!
	SDLDataHeader* pHeader = (SDLDataHeader*)out_buffer;
	StoreContext.SeekEnd();
	if( out_buffer )
		pHeader->m_InstanceSize = (uint32)StoreContext.Tell();

	if( produced_bytes )
		*produced_bytes = (uint32)StoreContext.Tell() + sizeof(SDLDataHeader);

	if( out_buffer_size > 0 && pHeader->m_InstanceSize > out_buffer_size )
		return DL_ERROR_BUFFER_TO_SMALL;

	return err;
}

dl_error_t dl_instance_calc_size(dl_ctx_t dl_ctx, dl_typeid_t type, void* instance, unsigned int* out_size )
{
	return dl_instance_store( dl_ctx, type, instance, 0x0, 0, out_size );
}

const char* dl_error_to_string(dl_error_t _Err)
{
#define DL_ERR_TO_STR(ERR) case ERR: return #ERR
	switch(_Err)
	{
		DL_ERR_TO_STR(DL_ERROR_OK);
		DL_ERR_TO_STR(DL_ERROR_MALFORMED_DATA);
		DL_ERR_TO_STR(DL_ERROR_VERSION_MISMATCH);
		DL_ERR_TO_STR(DL_ERROR_OUT_OF_LIBRARY_MEMORY);
		DL_ERR_TO_STR(DL_ERROR_OUT_OF_INSTANCE_MEMORY);
		DL_ERR_TO_STR(DL_ERROR_DYNAMIC_SIZE_TYPES_AND_NO_INSTANCE_ALLOCATOR);
		DL_ERR_TO_STR(DL_ERROR_OUT_OF_DEFAULT_VALUE_SLOTS);
		DL_ERR_TO_STR(DL_ERROR_TYPE_MISMATCH);
		DL_ERR_TO_STR(DL_ERROR_TYPE_NOT_FOUND);
		DL_ERR_TO_STR(DL_ERROR_MEMBER_NOT_FOUND);
		DL_ERR_TO_STR(DL_ERROR_BUFFER_TO_SMALL);
		DL_ERR_TO_STR(DL_ERROR_ENDIAN_MISMATCH);
		DL_ERR_TO_STR(DL_ERROR_BAD_ALIGNMENT);
		DL_ERR_TO_STR(DL_ERROR_INVALID_PARAMETER);
		DL_ERR_TO_STR(DL_ERROR_UNSUPPORTED_OPERATION);

		DL_ERR_TO_STR(DL_ERROR_TXT_PARSE_ERROR);
		DL_ERR_TO_STR(DL_ERROR_TXT_MEMBER_MISSING);
		DL_ERR_TO_STR(DL_ERROR_TXT_MEMBER_SET_TWICE);

		DL_ERR_TO_STR(DL_ERROR_UTIL_FILE_NOT_FOUND);
		DL_ERR_TO_STR(DL_ERROR_UTIL_FILE_TYPE_MISMATCH);

		DL_ERR_TO_STR(DL_ERROR_INTERNAL_ERROR);
		default: return "Unknown error!";
	}
#undef DL_ERR_TO_STR
}

dl_error_t dl_instance_get_info( const unsigned char* packed_instance, unsigned int packed_instance_size, dl_instance_info_t* out_info )
{
	SDLDataHeader* header = (SDLDataHeader*)packed_instance;

	if( packed_instance_size < sizeof(SDLDataHeader) && header->m_Id != DL_INSTANCE_ID_SWAPED && header->m_Id != DL_INSTANCE_ID )
		return DL_ERROR_MALFORMED_DATA;
	if(header->m_Version != DL_INSTANCE_VERSION && header->m_Version != DL_INSTANCE_VERSION_SWAPED)
		return DL_ERROR_VERSION_MISMATCH;

	out_info->ptrsize   = header->m_64BitPtr ? 8 : 4;
	if( header->m_Id == DL_INSTANCE_ID )
	{
		out_info->load_size = header->m_InstanceSize;
		out_info->endian    = DL_ENDIAN_HOST;
		out_info->root_type = header->m_RootInstanceType ;
	}
	else
	{
		out_info->load_size = DLSwapEndian( header->m_InstanceSize );
		out_info->endian    = DLOtherEndian( DL_ENDIAN_HOST );
		out_info->root_type = DLSwapEndian( header->m_RootInstanceType );
	}

	return DL_ERROR_OK;
}
