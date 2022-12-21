#pragma once

template <int ChunkSize>
struct SparseBuf {
    enum: int { kChunkSize = ChunkSize };
    std::vector<uint8_t*> chunks;
    int32_t bufSize = 0;
    int32_t dataSize = 0;
    uint8_t hash[16];
    ~SparseBuf() {
        this->free();
    }
    void clear()
    {
        dataSize = 0;
    }
    void free() {
        clear();
        for (auto buf: chunks) {
            ::free(buf);
        }
        chunks.clear();
        bufSize = 0;
    }
    bool allocAndClear(size_t size)
    {
        dataSize = 0;
        int nChunks = (size + kChunkSize - 1) / kChunkSize;
        int diff = nChunks - chunks.size();
        if (diff <= 0) {
            return true;
        }
        for (int i = 0; i < diff; i++) {
            auto buf = malloc(kChunkSize);
            if (!buf) {
                free();
                ESP_LOGE("SBUF", "FileData.allocAndClear: Out of memory");
                return false;
            }
            chunks.push_back((uint8_t*)buf);
        }
        bufSize = nChunks * kChunkSize;
        return true;
    }
    uint8_t* appendPtr(int32_t& size) {
        int lastChunk = dataSize / kChunkSize;
        if (lastChunk >= chunks.size()) {
            ESP_LOGE("SBUF", "FileData.appendPtr: Attempted to write past end of buffer");
            return nullptr;
        }
        int32_t chunkOfs = dataSize % kChunkSize;
        size = std::min(size, kChunkSize - chunkOfs);
        return chunks[lastChunk] + chunkOfs;
    }
    void append(const uint8_t* data, size_t size)
    {
        size_t newSize = dataSize + size;
        if (newSize > bufSize) {
            ESP_LOGE("SBUF", "FileData.append: Trying to append past end of allocated buffer");
            return;
        }
        int lastChunk = (dataSize + kChunkSize - 1) / kChunkSize;
        int chunkOfs = dataSize % kChunkSize;
        int writeSize = std::min((int)size, kChunkSize - chunkOfs);
        memcpy(chunks[lastChunk] + chunkOfs, data, writeSize);
        if (writeSize < size) {
            memcpy(chunks[lastChunk+1], data + writeSize, size - writeSize);
        }
        dataSize = newSize;
    }
    void calculateHash()
    {
        struct MD5Context ctx;
        memset(&ctx, 0, sizeof(ctx));
        MD5Init(&ctx);
        auto remain = dataSize;
        for (auto chunk: chunks) {
            if (remain >= kChunkSize) {
                MD5Update(&ctx, chunk, kChunkSize);
                remain -= kChunkSize;
            }
            else if (remain == 0) {
                break;
            }
            else if (remain < kChunkSize) {
                MD5Update(&ctx, chunk, remain);
                break;
            }
        }
        MD5Final(hash, &ctx);
    }
};
