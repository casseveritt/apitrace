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
    std::fstream m_stream;
};

UncompressedFile::UncompressedFile(const std::string &filename,
                              File::Mode mode)
    : File()
{
}

UncompressedFile::~UncompressedFile()
{
    close();
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
        m_stream.seekg(0, std::ios::beg);

        // read the uncompressed file identifier
        unsigned char byte1, byte2;
        m_stream >> byte1;
        m_stream >> byte2;
        assert(byte1 == UNCOMPRESSED_BYTE1 && byte2 == UNCOMPRESSED_BYTE2);
    } else if (m_stream.is_open() && mode == File::Write) {
        // write the uncompressed file identifier
        m_stream << UNCOMPRESSED_BYTE1;
        m_stream << UNCOMPRESSED_BYTE2;
    }
 
    return m_stream.is_open();
}

bool UncompressedFile::rawWrite(const void *buffer, size_t length)
{
    m_stream.write( (const char *)buffer, length ); 
    return true;
}

size_t UncompressedFile::rawRead(void *buffer, size_t length)
{
    m_stream.read( (char *)buffer, length );
    return m_stream.gcount();
}

int UncompressedFile::rawGetc()
{
    char c = 0;
    m_stream.get( c );
    return *reinterpret_cast<unsigned char *>(&c);
}

void UncompressedFile::rawClose()
{
    m_stream.close();
}

void UncompressedFile::rawFlush()
{
    assert(m_mode == File::Write);
    m_stream.flush();
}

bool UncompressedFile::supportsOffsets() const
{
    return true;
}

File::Offset UncompressedFile::currentOffset()
{
    File::Offset o; 
    o.chunk = m_stream.tellg();
    o.offsetInChunk = 0;
    return o;
}

void UncompressedFile::setCurrentOffset(const File::Offset &offset)
{
    // to remove eof bit
    m_stream.clear();
    // seek to the start of a chunk
    m_stream.seekg(offset.chunk, std::ios::beg);
}

bool UncompressedFile::rawSkip(size_t length)
{
    m_stream.seekg( length, std::ios::cur );
    return true;
}

int UncompressedFile::rawPercentRead()
{
    double cur(m_stream.tellg());
    m_stream.seekg( 0, std::ios::end );
    double end(m_stream.tellg());
    m_stream.seekg( cur, std::ios::beg );
    return int( 100.0 * cur / end );
}


File* File::createUncompressed(void) {
    return new UncompressedFile;
}

