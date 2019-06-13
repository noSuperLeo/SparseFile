#ifndef TST_SPARSEFILE_H
#define TST_SPARSEFILE_H
#ifndef __cplusplus
#   error ERROR: This file requires C++ compilation (use a .cpp suffix)
#endif

#include <vector>

#ifndef CXX_NEWTYPESDEFINE_H
#   include "NewTypesDefine.h"
#endif

#ifndef CXX_PFILESERVICEPROXY_H
#   include "PFileServiceProxy.h"
#endif

struct TST_SparseChunkInfo {
    WORD chunk_type;        // data type
    DWORD chunk_data_ss;    // src data addr
    LONGLONG chunk_data_ds; // des data addr
    DWORD chunk_data_sz;    // data size
    DWORD fill_val;         // fill value
};

/*=============================================================================
     Class declaration                                                          */

class TST_SparseFile;

/*===========================================================================*/
/**
    sparse file
*/
/*===========================================================================*/
class TST_SparseFile
{
public:
    TST_SparseFile();
    NP_BOOL load(PFileServiceProxy* file, DWORD dwBeginPos = 0);
    LONGLONG getTotalSize();
    VOID clear();
    BOOL Read(LPVOID pBuffer, DWORD dwSize, PDWORD pdwLength);
    NP_BOOL read(VOID* pBuff, const DWORD& inLen, DWORD& outLen, LONGLONG& llDesPos, NP_BOOL& bEnd);
    virtual ~TST_SparseFile();

protected:
    NP_BOOL m_bLoad;
    PFileServiceProxy* m_file;

    DWORD m_dwChunkIndex;
    DWORD m_dwChunkDataPos;
    LONGLONG m_llTotalSize;
    std::vector<TST_SparseChunkInfo> m_chunks;
};

#endif // TST_SPARSEFILE_H

//================================================================= End of File

