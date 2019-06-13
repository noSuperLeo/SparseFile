#   include "stdafx.h"
#   include <string.h>

#ifndef TST_SPARSEFILE_H
#   include "TST_SparseFile.h"
#endif

struct sparse_header {
    uint32_t  magic;          /* 0xed26ff3a */
    uint16_t  major_version;  /* (0x1) - reject images with higher major versions */
    uint16_t  minor_version;  /* (0x0) - allow images with higer minor versions */
    uint16_t  file_hdr_sz;    /* 28 bytes for first revision of the file format */
    uint16_t  chunk_hdr_sz;   /* 12 bytes for first revision of the file format */
    uint32_t  blk_sz;         /* block size in bytes, must be a multiple of 4 (4096) */
    uint32_t  total_blks;     /* total blocks in the non-sparse output image */
    uint32_t  total_chunks;   /* total chunks in the sparse input image */
    uint32_t  image_checksum; /* CRC32 checksum of the original data, counting "don't care" */
                              /* as 0. Standard 802.3 polynomial, use a Public Domain */
                              /* table implementation */
};

#define SPARSE_HEADER_MAGIC     0xed26ff3a
#define CHUNK_TYPE_RAW          0xCAC1
#define CHUNK_TYPE_FILL         0xCAC2
#define CHUNK_TYPE_DONT_CARE    0xCAC3
#define CHUNK_TYPE_CRC          0xCAC4

struct chunk_header {
    uint16_t  chunk_type; /* 0xCAC1 -> raw; 0xCAC2 -> fill; 0xCAC3 -> don't care */
    uint16_t  reserved1;
    uint32_t  chunk_sz;   /* in blocks in output image */
    uint32_t  total_sz;   /* in bytes of chunk input file including chunk header and data */
};

TST_SparseFile::TST_SparseFile()
    :m_bLoad(NP_FALSE),
     m_file(NULL),
     m_dwChunkIndex(0),
     m_dwChunkDataPos(0),
     m_llTotalSize(0),
     m_chunks()
{
}

TST_SparseFile::~TST_SparseFile()
{
    clear();
}

NP_BOOL
TST_SparseFile::load(PFileServiceProxy* file, DWORD dwBeginPos)
{
    clear();

    if (NULL == file) {
        return NP_FALSE;
    }

    LONGLONG chunk_data_ss = dwBeginPos;
    if (TRUE != file->Seek(chunk_data_ss, SEEK_SET, TRUE)) {
        LOGD("TST_SparseFile::load: seek(%llx) error", chunk_data_ss);
        return NP_FALSE;
    }

    // read hearder
    sparse_header s_header;
    DWORD dwReadOutLen = 0;
    memset(&s_header, 0, sizeof(sparse_header));
    if (TRUE != file->Read(&s_header, sizeof(sparse_header), &dwReadOutLen)) {
        LOGD("TST_SparseFile::load: read sparse_header error");
        return NP_FALSE;
    }

    // check magic
    if (SPARSE_HEADER_MAGIC != s_header.magic) {
        LOGD("TST_SparseFile::load: s_header.magic error");
        return NP_FALSE;
    }

    if (s_header.file_hdr_sz > sizeof(sparse_header)) {
        /* Skip the remaining bytes in a header that is longer than
         * we expected.
         */
        chunk_data_ss += s_header.file_hdr_sz;
        if (TRUE != file->Seek(chunk_data_ss, SEEK_SET, TRUE)) {
            LOGD("TST_SparseFile::load: seek(%llx) error", chunk_data_ss);
            return NP_FALSE;
        }
    }
    else {
        chunk_data_ss += sizeof(sparse_header);
    }

    // get chunk info
    chunk_header c_header;
    LONGLONG llchunk_data_sz = 0;
    LONGLONG chunk_data_ds = 0;
    DWORD fill_val = 0;
    for (DWORD chunk = 0; chunk < s_header.total_chunks; chunk++) {
        memset(&c_header, 0, sizeof(chunk_header));
        if (TRUE != file->Read(&c_header, sizeof(chunk_header), &dwReadOutLen)) {
            LOGD("TST_SparseFile::load: read chunk_header error");
            return NP_FALSE;
        }

        if (s_header.chunk_hdr_sz > sizeof(chunk_header)) {
            /* Skip the remaining bytes in a header that is longer than
             * we expected.
             */
            chunk_data_ss += s_header.chunk_hdr_sz;
            if (TRUE != file->Seek(chunk_data_ss, SEEK_SET, TRUE)) {
                LOGD("TST_SparseFile::load: seek(%llx) error", chunk_data_ss);
                return NP_FALSE;
            }
        }
        else {
            chunk_data_ss += sizeof(chunk_header);
        }

        llchunk_data_sz = s_header.blk_sz * c_header.chunk_sz;
        switch (c_header.chunk_type) {
        case CHUNK_TYPE_RAW:
        {
            // followed by raw data
            // check chunk size
            if (c_header.total_sz != (s_header.chunk_hdr_sz + llchunk_data_sz)) {
                LOGD("TST_SparseFile::load: chunk size error");
                return NP_FALSE;
            }
            {
                TST_SparseChunkInfo info;
                info.chunk_type = c_header.chunk_type;
                info.chunk_data_ss = chunk_data_ss;
                info.chunk_data_ds = chunk_data_ds;
                info.chunk_data_sz = llchunk_data_sz;
                info.fill_val = 0;
                m_chunks.push_back(info);
            }

            chunk_data_ds += llchunk_data_sz;
            chunk_data_ss += llchunk_data_sz;
            m_llTotalSize += llchunk_data_sz;
            if (TRUE != file->Seek(chunk_data_ss, SEEK_SET, TRUE)) {
                LOGD("TST_SparseFile::load: seek(%llx) error", chunk_data_ss);
                return NP_FALSE;
            }
        }
            break;
        case CHUNK_TYPE_DONT_CARE:
        {
#ifndef HAVE_ERASE
            {
                TST_SparseChunkInfo info;
                info.chunk_type = c_header.chunk_type;
                info.chunk_data_ss = chunk_data_ss;
                info.chunk_data_ds = chunk_data_ds;
                info.chunk_data_sz = llchunk_data_sz;
                info.fill_val = 0;
                m_chunks.push_back(info);
            }
            m_llTotalSize += llchunk_data_sz;
#endif
            chunk_data_ds += llchunk_data_sz;
        }
            break;
        case CHUNK_TYPE_FILL:
        {
            // followed by 4 bytes of fill data
            if (c_header.total_sz != (s_header.chunk_hdr_sz + sizeof(DWORD))) {
                LOGD("TST_SparseFile::load: chunk size error");
                return NP_FALSE;
            }
            if (TRUE != file->Read(&fill_val, sizeof(DWORD), &dwReadOutLen)) {
                LOGD("TST_SparseFile::load: read fill_val error");
                return NP_FALSE;
            }
            {
                TST_SparseChunkInfo info;
                info.chunk_type = c_header.chunk_type;
                info.chunk_data_ss = chunk_data_ss;
                info.chunk_data_ds = chunk_data_ds;
                info.chunk_data_sz = llchunk_data_sz;
                info.fill_val = fill_val;
                m_chunks.push_back(info);
            }

            chunk_data_ds += llchunk_data_sz;
            chunk_data_ss += sizeof(DWORD);
            m_llTotalSize += llchunk_data_sz;
        }
            break;
        case CHUNK_TYPE_CRC:
        {
            LOGD("TST_SparseFile::load: Unknown chunk typer");
            return NP_FALSE;
        }
            break;
            /*
            if (c_header.total_sz != s_header.chunk_hdr_sz) {
                LOGD("TST_SparseFile::load: chunk size error");
                return NP_FALSE;
            }
            {
                TST_SparseChunkInfo info;
                info.chunk_type = c_header.chunk_type;
                info.chunk_data_ss = chunk_data_ss;
                info.chunk_data_ds = chunk_data_ds;
                info.chunk_data_sz = llchunk_data_sz;
                info.fill_val = 0;
                m_chunks.push_back(info);
            }
            chunk_data_ds += llchunk_data_sz;
            chunk_data_ss += llchunk_data_sz;
            if (TRUE != file->Seek(chunk_data_ss, SEEK_SET)) {
                LOGD("TST_SparseFile::load: seek(%lx) error", chunk_data_ss);
                return NP_FALSE;
            }
            break;
            */
         default:
            LOGD("TST_SparseFile::load: Unknown chunk typer");
            return NP_FALSE;
            break;
        }
    }
    m_file = file;
    // m_llTotalSize = static_cast<LONGLONG>(s_header.total_blks) * s_header.blk_sz;
    m_bLoad = NP_TRUE;
    return NP_TRUE;
}

LONGLONG
TST_SparseFile::getTotalSize()
{
    LOGD("TST_SparseFile::get m_llTotalSize %lx", m_llTotalSize);
    return m_llTotalSize;
}

VOID
TST_SparseFile::clear()
{
    m_bLoad = NP_FALSE;
    m_file = NULL;
    m_dwChunkIndex = 0;
    m_dwChunkDataPos = 0;
    m_llTotalSize = 0;
    m_chunks.clear();
}

BOOL
TST_SparseFile::Read(LPVOID pBuffer, DWORD dwSize, PDWORD pdwLength)
{
    if (NP_FALSE == m_bLoad || NULL == pBuffer || 0 == dwSize) {
        return FALSE;
    }
    memset(pBuffer, 0x00, dwSize);

    *pdwLength = 0;
    DWORD dwRestSize = dwSize;
    DWORD dwReadSize = 0;       // read total size
    DWORD dwOutSize = 0;        // 1 time read out size
    LONGLONG llDesPos = 0;
    NP_BOOL bEnd = NP_FALSE;
    NP_BOOL bReadRet = NP_FALSE;

    while (0 < dwRestSize) {
        // read not enough, continue read
        bReadRet = read((BYTE*)pBuffer+dwReadSize, dwRestSize, dwOutSize, llDesPos, bEnd);
        if (NP_FALSE == bReadRet) {
            return FALSE;
        }
        dwReadSize += dwOutSize;  // total size add this time read out size
        *pdwLength = dwReadSize;  // set the out size
        if (NP_TRUE == bEnd) {    // read end
            break;
        }

        dwRestSize -= dwOutSize;

    }

    if (dwReadSize != dwSize) {
        // read size is not equal to the size API need
        return FALSE;
    }

    return TRUE;
}

NP_BOOL
TST_SparseFile::read(VOID* pBuff, const DWORD& inLen, DWORD& outLen, LONGLONG& llDesPos, NP_BOOL& bEnd)
{
    if (NP_FALSE == m_bLoad || NULL == pBuff || 0 == inLen) {
        return NP_FALSE;
    }

    outLen = 0;
    DWORD dwReadOutLen = 0;

    if (m_dwChunkIndex >= m_chunks.size()) {
        bEnd = NP_TRUE;
        return NP_TRUE;
    }

    DWORD dwChunkIndex = m_dwChunkIndex;
    DWORD dwChunkDataPos = m_dwChunkDataPos;

    while (dwChunkIndex < m_chunks.size()) {
        TST_SparseChunkInfo info = m_chunks[dwChunkIndex];
        if (dwChunkDataPos >= info.chunk_data_sz || CHUNK_TYPE_CRC == info.chunk_type) {
            ++dwChunkIndex;
            dwChunkDataPos = 0;
            continue;
        }

#ifdef HAVE_ERASE
        if (CHUNK_TYPE_DONT_CARE == info.chunk_type) {
            ++dwChunkIndex;
            dwChunkDataPos = 0;
            continue;
        }
#endif

        DWORD restLen = info.chunk_data_sz - dwChunkDataPos;
        DWORD realLen = (restLen >= inLen) ? inLen : restLen;
        if (CHUNK_TYPE_RAW == info.chunk_type) {
            DWORD chunk_data_ss = info.chunk_data_ss + dwChunkDataPos;
            if (TRUE != m_file->Seek(chunk_data_ss, SEEK_SET, TRUE)) {
                LOGD("TST_SparseFile::read: seek(%lx) error", chunk_data_ss);
                return NP_FALSE;
            }
            if (TRUE != m_file->Read(pBuff, realLen, &dwReadOutLen)) {
                LOGD("TST_SparseFile::read: error");
                return NP_FALSE;
            }
            outLen = realLen;
            llDesPos = info.chunk_data_ds + dwChunkDataPos;
            dwChunkDataPos += realLen;
        }
        else if (CHUNK_TYPE_FILL == info.chunk_type) {
            for(int index = 0; index < (realLen/4); index++) {
                memcpy(((BYTE*)pBuff+index*4), &info.fill_val, sizeof(info.fill_val));
            }
            outLen = realLen;
            llDesPos = info.chunk_data_ds + dwChunkDataPos;
            dwChunkDataPos += realLen;
        }
        else if (CHUNK_TYPE_DONT_CARE == info.chunk_type) {
            memset(pBuff, 0, realLen);
            outLen = realLen;
            llDesPos = info.chunk_data_ds + dwChunkDataPos;
            dwChunkDataPos += realLen;
        }
        else {
        }

        if (info.chunk_data_sz == dwChunkDataPos) {
            ++dwChunkIndex;
            dwChunkDataPos = 0;
        }
        break;
    }

    if (dwChunkIndex >= m_chunks.size()) {
        bEnd = NP_TRUE;
    }

    m_dwChunkIndex = dwChunkIndex;
    m_dwChunkDataPos = dwChunkDataPos;
    return NP_TRUE;
}

