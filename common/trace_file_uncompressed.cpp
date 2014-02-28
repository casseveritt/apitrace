/**************************************************************************
 *
 * Copyright 2014 Cass Everitt
 * All Rights Reserved.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
 * THE SOFTWARE.
 *
 **************************************************************************/

#include <iostream>
#include <algorithm>

#include <assert.h>
#include <string.h>

#include "trace_file.hpp"

using namespace trace;

#define UNCOMPRESSED_CACHE_SIZE (1<<20)

class UncompressedFile : public File {
public:
    UncompressedFile(const std::string &filename = std::string(),
               File::Mode mode = File::Read);
    virtual ~UncompressedFile();

    virtual bool supportsOffsets() const;
    virtual File::Offset currentOffset();
    virtual void setCurrentOffset(const File::Offset &offset);
protected:
    virtual bool rawOpen(const std::string &filename, File::Mode mode);
    virtual bool rawWrite(const void *buffer, size_t length);
    virtual size_t rawRead(void *buffer, size_t length);
    virtual int rawGetc();
    virtual void rawClose();
    virtual void rawFlush();
    virtual bool rawSkip(size_t length);
    virtual int rawPercentRead();

private:
    inline size_t usedCacheSize() const
    {
        assert(m_cachePtr >= m_cache);
        return m_cachePtr - m_cache;
    }
    inline size_t freeCacheSize() const
    {
        assert(m_cacheSize >= usedCacheSize());
        if (m_cacheSize > 0) {
            return m_cacheSize - usedCacheSize();
        } else {
            return 0;
        }
    }
    inline bool endOfData() const
    {
        return m_stream.eof() && freeCacheSize() == 0;
    }
    void flushWriteCache();
    void flushReadCache(size_t skipLength = 0);
private:
    std::fstream m_stream;
    char *m_cache;
    char *m_cachePtr;
    int m_cacheSize;

    File::Offset m_currentOffset;
    std::streampos m_endPos;
};

UncompressedFile::UncompressedFile(const std::string &filename,
                              File::Mode mode)
    : File(),
      m_cache(new char [UNCOMPRESSED_CACHE_SIZE]),
      m_cachePtr(m_cache),
      m_cacheSize(0)
{
}

UncompressedFile::~UncompressedFile()
{
    close();
    delete [] m_cache;
}

bool UncompressedFile::rawOpen(const std::string &filename, File::Mode mode)
{
    std::ios_base::openmode fmode = std::fstream::binary;
    if (mode == File::Write) {
        fmode |= (std::fstream::out | std::fstream::trunc);
    } else if (mode == File::Read) {
        fmode |= std::fstream::in;
    }

    m_stream.open(filename.c_str(), fmode);

    //read in the initial buffer if we're reading
    if (m_stream.is_open() && mode == File::Read) {
        m_stream.seekg(0, std::ios::end);
        m_endPos = m_stream.tellg();
        m_stream.seekg(0, std::ios::beg);

        // read the snappy file identifier
        unsigned char byte1, byte2;
        m_stream >> byte1;
        m_stream >> byte2;
        assert(byte1 == UNCOMPRESSED_BYTE1 && byte2 == UNCOMPRESSED_BYTE2);
        flushReadCache();
    } else if (m_stream.is_open() && mode == File::Write) {
        // write the uncompressed file identifier
        m_stream << UNCOMPRESSED_BYTE1;
        m_stream << UNCOMPRESSED_BYTE2;
    }

    return m_stream.is_open();
}

bool UncompressedFile::rawWrite(const void *buffer, size_t length)
{
    if (freeCacheSize() > length) {
        memcpy(m_cachePtr, buffer, length);
        m_cachePtr += length;
    } else if (freeCacheSize() == length) {
        memcpy(m_cachePtr, buffer, length);
        m_cachePtr += length;
        flushWriteCache();
    } else {
        size_t sizeToWrite = length;

        while (sizeToWrite >= freeCacheSize()) {
            size_t endSize = freeCacheSize();
            size_t offset = length - sizeToWrite;
            memcpy(m_cachePtr, (const char*)buffer + offset, endSize);
            sizeToWrite -= endSize;
            m_cachePtr += endSize;
            flushWriteCache();
        }
        if (sizeToWrite) {
            size_t offset = length - sizeToWrite;
            memcpy(m_cachePtr, (const char*)buffer + offset, sizeToWrite);
            m_cachePtr += sizeToWrite;
        }
    }

    return true;
}

size_t UncompressedFile::rawRead(void *buffer, size_t length)
{
    if (endOfData()) {
        return 0;
    }

    if (freeCacheSize() >= length) {
        memcpy(buffer, m_cachePtr, length);
        m_cachePtr += length;
    } else {
        size_t sizeToRead = length;
        size_t offset = 0;
        while (sizeToRead) {
            size_t chunkSize = std::min(freeCacheSize(), sizeToRead);
            offset = length - sizeToRead;
            memcpy((char*)buffer + offset, m_cachePtr, chunkSize);
            m_cachePtr += chunkSize;
            sizeToRead -= chunkSize;
            if (sizeToRead > 0) {
                flushReadCache();
            }
        }
    }

    return length;
}

int UncompressedFile::rawGetc()
{
    unsigned char c = 0;
    if (rawRead(&c, 1) != 1)
        return -1;
    return c;
}

void UncompressedFile::rawClose()
{
    if (m_mode == File::Write) {
        flushWriteCache();
    }
    m_stream.close();
    delete [] m_cache;
    m_cache = NULL;
    m_cachePtr = NULL;
}

void UncompressedFile::rawFlush()
{
    assert(m_mode == File::Write);
    flushWriteCache();
    m_stream.flush();
}

void UncompressedFile::flushWriteCache()
{
    size_t inputLength = usedCacheSize();

    if (inputLength) {
        m_stream.write(m_cache, inputLength);
        m_cachePtr = m_cache;
    }
    assert(m_cachePtr == m_cache);
}

void UncompressedFile::flushReadCache(size_t skipLength)
{
    //assert(m_cachePtr == m_cache + m_cacheSize);
    m_currentOffset.chunk = m_stream.tellg();
    m_stream.read((char*)m_cache, UNCOMPRESSED_CACHE_SIZE);
    m_cacheSize = m_stream.gcount();
    m_cachePtr = m_cache;
}

bool UncompressedFile::supportsOffsets() const
{
    return true;
}

File::Offset UncompressedFile::currentOffset()
{
    m_currentOffset.offsetInChunk = m_cachePtr - m_cache;
    return m_currentOffset;
}

void UncompressedFile::setCurrentOffset(const File::Offset &offset)
{
    // to remove eof bit
    m_stream.clear();
    // seek to the start of a chunk
    m_stream.seekg(offset.chunk, std::ios::beg);
    // load the chunk
    flushReadCache();
    // seek within our cache to the correct location within the chunk
    m_cachePtr = m_cache + offset.offsetInChunk;

}

bool UncompressedFile::rawSkip(size_t length)
{
    if (endOfData()) {
        return false;
    }

    if (freeCacheSize() >= length) {
        m_cachePtr += length;
    } else {
        size_t sizeToRead = length;
        while (sizeToRead) {
            size_t chunkSize = std::min(freeCacheSize(), sizeToRead);
            m_cachePtr += chunkSize;
            sizeToRead -= chunkSize;
            if (sizeToRead > 0) {
                flushReadCache(sizeToRead);
            }
        }
    }

    return true;
}

int UncompressedFile::rawPercentRead()
{
    return int(100 * (double(m_stream.tellg()) / double(m_endPos)));
}


File* File::createUncompressed(void) {
    return new UncompressedFile;
}
