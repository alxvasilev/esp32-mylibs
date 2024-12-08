#ifndef NVS_HANDLE_HPP_INCLUDED
#define NVS_HANDLE_HPP_INCLUDED

#include <nvs_handle.hpp>
#include <nvs.h>
#include "utils.hpp"
#include <set>

#define MYNVS_LOGD(fmt,...) printf("nvs: " fmt "\n", ##__VA_ARGS__)

class NvsHandle;
struct CommitItem {
    enum { kTimerTicksTillCommit = 2 };
    char* key;
    nvs::ItemType type = nvs::ItemType::ANY;
    uint8_t ticks = kTimerTicksTillCommit;
    inline CommitItem(const char* aKey, nvs::ItemType aType): key(strdup(aKey)), type(aType) {}
    virtual const void* data() const = 0;
    virtual int dataSize() const = 0;
    virtual void update(const void* data, int len) = 0;
    virtual esp_err_t write(nvs::NVSHandle& nvs) = 0;
    virtual ~CommitItem()
    {
        free(key);
    }
};

template <typename T>
struct ValCommitItem: public CommitItem {
    T val;
    ValCommitItem(const char* aKey, const T& aVal) :CommitItem(aKey, nvs::itemTypeOf<T>()), val(aVal) {}
    const void* data() const override { return &val; }
    int dataSize() const override { return sizeof(val); }
    void update(const void *data, int len) override { val = *(T*)data; }
    virtual esp_err_t write(nvs::NVSHandle& nvs) override { return nvs.set_item(key, val); }
};

struct BlobCommitItem: public CommitItem {
    unique_ptr_mfree<uint8_t> mData;
    int mSize;
    BlobCommitItem(const char* aKey, const void* data, int size, nvs::ItemType aType)
    : CommitItem(aKey, aType), mData((uint8_t*)malloc(size ? size : 1)), mSize(size)
    {
        myassert(aType == nvs::ItemType::SZ || aType == nvs::ItemType::BLOB_DATA);
        memcpy(mData.get(), data, size);
    }
    const void* data() const override { return mData.get(); }
    int dataSize() const override { return mSize; }
    void update(const void *data, int size) override
    {
        if (size != mSize) {
            mData.reset((uint8_t*)realloc(mData.release(), size ? size : 1));
            mSize = size;
        }
        memcpy(mData.get(), data, size);
    }
    virtual esp_err_t write(nvs::NVSHandle& nvs) override
    {
        return type == nvs::ItemType::SZ
            ? nvs.set_string(key, (const char*)mData.get())
            : nvs.set_blob(key, mData.get(), mSize);
    }
};
class NvsHandle
{
    friend class CommitItem;
    friend class NvsIterator;
protected:
    struct Compare {
        using is_transparent = std::true_type;
        bool operator()(const char* a, CommitItem* b) const { return strcmp(a, b->key) < 0; }
        bool operator()(CommitItem* a, const char* b) const { return strcmp(a->key, b) < 0; }
        bool operator()(const char* a, const char* b) const { return strcmp(a, b) < 0; }
        bool operator()(const CommitItem* a, const CommitItem* b) const { return strcmp(a->key, b->key) < 0; }
    };
    Mutex mMutex;
    std::unique_ptr<nvs::NVSHandle> mHandle;
    std::set<CommitItem*, Compare> mCommitItems;
    TickType_t mTimerPeriod = 0;
    TimerHandle_t mTimer;
    unique_ptr_mfree<char> mNsName; // needed only to create an iterator - the NVSHandle doesn't provide access to ns and partition names
    bool mTimerIsRunning = false;
    static const char* tag() { static const char* sTag = "nvs-handle"; return sTag; }
    esp_err_t readStringOrBlob(const char* key, void* data, int& len, nvs::ItemType type)
    {
        {
            MutexLocker locker(mMutex);
            auto it = mCommitItems.find(key);
            if (it != mCommitItems.end()) {
                auto& item = **it;
                if (item.type != type) {
                    return ESP_ERR_NVS_TYPE_MISMATCH;
                }
                auto itemSize = item.dataSize();
                if (len < itemSize) {
                    return ESP_ERR_NVS_INVALID_LENGTH;
                }
                memcpy(data, item.data(), itemSize);
                len = itemSize;
                return ESP_OK;
            }
        }
        size_t itemSize = 0;
        auto err = mHandle->get_item_size(type, key, itemSize);
        if (err != ESP_OK) {
            return err;
        }
        if (len < itemSize) {
            return ESP_ERR_NVS_INVALID_LENGTH;
        }
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wignored-qualifiers"
        err = (type == nvs::ItemType::SZ) ? mHandle->get_string(key, (char*)data, (const size_t)len) : mHandle->get_blob(key, data, (const size_t)len);
#pragma GCC diagnostic pop
        if (err != ESP_OK) {
            return err;
        }
        len = itemSize;
        return ESP_OK;
    }
    int getStringOrBlobSize(const char* key, nvs::ItemType type)
    {
        {
            MutexLocker locker(mMutex);
            auto it = mCommitItems.find(key);
            if (it != mCommitItems.end()) {
                auto& item = **it;
                if (item.type != type) {
                    ESP_LOGW(tag(), "%s: item type doesn't match", __FUNCTION__);
                    return -2;
                }
                return item.dataSize();
            }
        }
        size_t itemSize = 0;
        auto err = mHandle->get_item_size(type, key, itemSize);
        if (err == ESP_OK) {
            return itemSize;
        }
        if (err != ESP_ERR_NVS_NOT_FOUND) {
            ESP_LOGE(tag(), "Error getting string/blob size: %s", esp_err_to_name(err));
            return -2;
        }
        return -1;
    }
    esp_err_t writeStringOrBlob(const char* key, const void* data, int len, nvs::ItemType type, bool writeDirect)
    {
        MutexLocker locker(mMutex);
        if (!mTimer) {
            writeDirect = true;
        }
        auto it = mCommitItems.find(key);
        if (it != mCommitItems.end()) {
            // found in cache
            MYNVS_LOGD("Cache hit for '%s'", key);
            if (writeDirect) {
                mCommitItems.erase(it);
                delete *it;
            }
            else {
                auto& item = **it;
                if (item.type != type) {
                    return ESP_ERR_NVS_TYPE_MISMATCH;
                }
                else {
                    if (type == nvs::ItemType::SZ) {
                        len = strlen((const char*)data) + 1;
                    }
                    MYNVS_LOGD("...updating cached item");
                    item.update(data, len);
                    item.ticks = CommitItem::kTimerTicksTillCommit;
                    heap_caps_check_integrity_all(true);
                    return ESP_OK;
                }
            }
        }
        // not found in cache, or deleted it from cache because of writeDirect
        if (writeDirect) {
            return (type == nvs::ItemType::SZ)
                ? mHandle->set_string(key, (const char*)data)
                : mHandle->set_blob(key, data, len);
        }
        else {
            if (type == nvs::ItemType::SZ) {
                len = strlen((const char*)data) + 1;
            }
            mCommitItems.insert(new BlobCommitItem(key, data, len, type));
            startTimer();
            return ESP_OK;
        }
    }
    void startTimer()
    {
        if (!mTimerIsRunning) {
            myassert(mTimer);
            xTimerStart(mTimer, portMAX_DELAY);
            mTimerIsRunning = true;
            MYNVS_LOGD("Timer start");
        }
    }
    void stopTimer()
    {
        if (mTimerIsRunning) {
            myassert(mTimer);
            xTimerStop(mTimer, portMAX_DELAY);
            mTimerIsRunning = false;
            MYNVS_LOGD("Timer stop");
        }
    }
    uint32_t setAutoCommitOsTicks(uint32_t newPeriod)
    {
        if (newPeriod == mTimerPeriod) {
            return newPeriod;
        }
        auto oldPeriod = mTimerPeriod;
        mTimerPeriod = newPeriod;
        if (mTimerPeriod) {
            if (mTimer) {
                xTimerChangePeriod(mTimer, mTimerPeriod, portMAX_DELAY);
            }
            else {
                createTimer();
            }
        }
        else { // disable commit delay
            deleteTimer();
            commit();
        }
        return oldPeriod;
    }
    static void onCommitTimer(TimerHandle_t xTimer)
    {
        MYNVS_LOGD("onCommitTimer");
        auto& self = *(NvsHandle*)pvTimerGetTimerID(xTimer);
        {
            MutexLocker locker(self.mMutex);
            auto& items = self.mCommitItems;
            for (auto it = items.begin(); it != items.end();) {
                auto item = *it;
                if (--item->ticks <= 0) {
                    item->write(*self.mHandle);
                    it = items.erase(it);
                    MYNVS_LOGD("Commit: wrote item '%s'", item->key);
                    delete item;
                }
                else {
                    it++;
                }
            }
            if (self.mCommitItems.empty()) {
                self.stopTimer();
            }
        }
        self.mHandle->commit();
    }
public:
    NvsHandle(const char* nsName, int commitDly, bool writable) = delete;
    NvsHandle(const char* nsName, bool writable, int commitDly)
    : mNsName(strdup(nsName))
    {
        esp_err_t err;
        mHandle.reset(nvs::open_nvs_handle(nsName, writable ? NVS_READWRITE : NVS_READONLY, &err).release());
        if (err != ESP_OK) {
            mHandle.reset();
            ESP_LOGE(tag(), "Error opening NVS handle: %s", esp_err_to_name(err));
        }
        mTimerPeriod = (commitDly * configTICK_RATE_HZ + 1) / 2;
        if (mTimerPeriod) {
            createTimer();
        }
        else {
            mTimer = nullptr;
        }
        MYNVS_LOGD("Created NvsHandle for namespace '%s' with commit interval %lu ticks", nsName, mTimerPeriod);
    }
    void createTimer() {
        mTimer = xTimerCreate("nvs-commit", mTimerPeriod ? mTimerPeriod : pdMS_TO_TICKS(10000),
                 pdTRUE, this, &NvsHandle::onCommitTimer);
    }
    void deleteTimer() {
        if (mTimer) {
            xTimerDelete(mTimer, portMAX_DELAY);
            mTimer = nullptr;
            mTimerIsRunning = false;
        }
    }
    operator bool() const { return mHandle != nullptr; }
    bool isValid() const { return mHandle != nullptr; }
    nvs::NVSHandle* handle() { return mHandle.get(); }
    virtual ~NvsHandle()
    {
        MutexLocker locker(mMutex);
        if (mHandle) {
            commit(); // stops the timer
        }
        else {
            stopTimer();
        }
        deleteTimer();
        mHandle.reset();
    }
    void commit()
    {
        MutexLocker locker(mMutex);
        stopTimer();
        auto& items = mCommitItems;
        for (auto it = items.begin(); it != items.end(); it++) {
            auto item = *it;
            item->write(*mHandle);
            delete item;
        }
        items.clear();
        mHandle->commit();
    }
    void enableAutoCommit(uint32_t delaySec)
    {
        auto newPeriod = (delaySec * configTICK_RATE_HZ + 1) / 2;
        MutexLocker locker(mMutex);
        setAutoCommitOsTicks(newPeriod);
    }
    esp_err_t readString(const char* key, char* str, int& len) { return readStringOrBlob(key, str, len, nvs::ItemType::SZ); }
    int getStringSize(const char* key) { return getStringOrBlobSize(key, nvs::ItemType::SZ); }
    esp_err_t readBlob(const char* key, void* data, int& len) { return readStringOrBlob(key, data, len, nvs::ItemType::BLOB_DATA); }
    int getBlobSize(const char* key) { return getStringOrBlobSize(key, nvs::ItemType::BLOB_DATA); }
    esp_err_t writeString(const char* key, const char* str, bool writeDirect=false) {
        return writeStringOrBlob(key, str, -1, nvs::ItemType::SZ, writeDirect);
    }
    esp_err_t writeBlob(const char* key, void* data, int len, bool writeDirect=false) {
        return writeStringOrBlob(key, data, len, nvs::ItemType::BLOB_DATA, writeDirect);
    }
    template<typename T>
    esp_err_t read(const char* key, T& val)
    {
        MutexLocker locker(mMutex);
        auto it = mCommitItems.find(key);
        if (it != mCommitItems.end()) {
            auto& item = **it;
            if (item.type != nvs::itemTypeOf<T>()) {
                return ESP_ERR_NVS_TYPE_MISMATCH;
            }
            val = *(T*)item.data();
            return ESP_OK;
        }
        return mHandle->get_item<T>(key, val);
    }
    template<typename T>
    esp_err_t write(const char* key, const T& val, bool writeDirect=false)
    {
        MutexLocker locker(mMutex);
        if (!mTimer) {
            writeDirect = true;
        }
        auto it = mCommitItems.find(key);
        if (it != mCommitItems.end()) {
            if (writeDirect) {
                mCommitItems.erase(it);
                delete *it;
            }
            auto& item = **it;
            if (nvs::itemTypeOf<T>() != item.type) {
                return ESP_ERR_NVS_TYPE_MISMATCH;
            }
            item.update(&val, sizeof(val));
            item.ticks = CommitItem::kTimerTicksTillCommit;
            return ESP_OK;
        }
        if (writeDirect) {
            return mHandle->set_item(key, val);
        }
        bool inserted = mCommitItems.insert(new ValCommitItem<T>(key, val)).second;
        myassert(inserted);
        startTimer();
        return ESP_OK;
    }
    esp_err_t eraseKey(const char* key) {
        {
            MutexLocker locker(mMutex);
            auto it = mCommitItems.find(key);
            if (it != mCommitItems.end()) {
                auto item = *it;
                mCommitItems.erase(it);
                delete item;
            }
        }
        return mHandle->erase_item(key);
    }
    template <typename T>
    T readDefault(const char* key, T defVal) {
        T val;
        auto err = read(key, val);
        return (err == ESP_OK) ? val : defVal;
    }
    static const char* valTypeToStr(nvs_type_t type)
    {
        switch(type) {
            case NVS_TYPE_U8: return "u8";
            case NVS_TYPE_I8: return "i8";
            case NVS_TYPE_U16: return "u16";
            case NVS_TYPE_I16: return "i16";
            case NVS_TYPE_U32: return "u32";
            case NVS_TYPE_I32: return "i32";
            case NVS_TYPE_U64: return "u64";
            case NVS_TYPE_I64: return "i64";
            case NVS_TYPE_STR: return "str";
            case NVS_TYPE_BLOB: return "blob";
            case NVS_TYPE_ANY: return "any";
            default: return "(invalid)";
        }
    }
    static bool typeIsNumeric(nvs_type_t type) { return (type & 0xe0) == 0; } // 0x1x or 0x0x
    static bool typeIsSignedInt(nvs_type_t type) { return (type & 0xf0) == 0x10; } // 0x1x
    static bool typeIsUnsignedInt(nvs_type_t type) { return (type & 0xf0) == 0; } // 0x0x
    template <class T>
    esp_err_t numValToStr(const char* key, DynBuffer& buf)
    {
        T val;
        auto err = read(key, val);
        if (err != ESP_OK) {
            return err;
        }
        auto str = std::to_string(val);
        buf.appendStr(str.c_str());
        return ESP_OK;
    }
    bool valToString(const char* key, nvs_type_t type, DynBuffer& buf, bool quoteStr=true)
    {
        if (type == NVS_TYPE_STR) {
            int len = getStringSize(key);
            if (len < 0) {
                return false;
            }
            if (quoteStr) {
                auto wptr = buf.getAppendPtr(len + 2);
                *wptr = '"';
                auto err = readString(key, wptr + 1, len);
                if (err) {
                    *wptr = 0;
                    return err;
                }
                wptr[len] = '"';
                buf.expandDataSize(len + 1);
            } else {
                auto wptr = buf.getAppendPtr(len);
                auto err = readString(key, wptr, len);
                if (err != ESP_OK) {
                    *wptr = 0;
                    return err;
                }
                buf.expandDataSize(len-1);
            }
            return ESP_OK;
        }
        else if (type == NVS_TYPE_BLOB) {
            buf.appendStr("\"(BLOB)\"");
            return ESP_OK;
        }
        switch(type) {
            case NVS_TYPE_U8: return numValToStr<uint8_t>(key, buf);
            case NVS_TYPE_I8: return numValToStr<int8_t>(key, buf);
            case NVS_TYPE_U16: return numValToStr<uint16_t>(key, buf);
            case NVS_TYPE_I16: return numValToStr<int16_t>(key, buf);
            case NVS_TYPE_U32: return numValToStr<uint32_t>(key, buf);
            case NVS_TYPE_I32: return numValToStr<int32_t>(key, buf);
            case NVS_TYPE_U64: return numValToStr<uint64_t>(key, buf);
            case NVS_TYPE_I64: return numValToStr<int64_t>(key, buf);
            default: return ESP_ERR_INVALID_ARG;
        }
    }
    esp_err_t writeValueFromString(const char* key, const char* type, const char* strVal)
    {
        switch (type[0]) {
            case 'i': {
                auto sz = type + 1;
                if (strcmp(sz, "64") == 0) {
                    errno = 0;
                    auto val = strtoll(strVal, nullptr, 10);
                    if (!val && errno != 0) {
                        return ESP_ERR_INVALID_ARG;
                    }
                    return write(key, val);
                }
                errno = 0;
                long val = strtol(strVal, nullptr, 10);
                if (!val && errno != 0) {
                    return ESP_ERR_INVALID_ARG;
                }
                if (strcmp(sz, "8") == 0) {
                    return write(key, (int8_t)val);
                } else if (strcmp(sz, "16") == 0) {
                    return write(key, (int16_t)val);
                } else if (strcmp(sz, "32") == 0) {
                    return write(key, (int32_t)val);
                } else {
                    return ESP_ERR_INVALID_ARG;
                }
            }
            case 'u': {
                auto sz = type + 1;
                if (strcmp(sz, "64") == 0) {
                    errno = 0;
                    uint64_t val = strtoull(strVal, nullptr, 10);
                    if (!val && errno != 0) {
                        return ESP_ERR_INVALID_ARG;
                    }
                    return write(key, val);
                }
                errno = 0;
                unsigned long val = strtoul(strVal, nullptr, 10);
                if (!val && errno != 0) {
                    return ESP_ERR_INVALID_ARG;
                }
                if (strcmp(sz, "8") == 0) {
                    return write(key, (uint8_t)val);
                } else if (strcmp(sz, "16") == 0) {
                    return write(key, (uint16_t)val);
                } else if (strcmp(sz, "32") == 0) {
                    return write(key, (uint32_t)val);
                } else {
                    return ESP_ERR_INVALID_ARG;
                }
            }
            case 's': {
                return writeString(key, strVal);
            }
            default:
                return ESP_ERR_NOT_SUPPORTED;
        }
    }
};

class NvsIterator: public nvs_entry_info_t {
protected:
    NvsHandle& mNvs;
    nvs_iterator_t mIter;
    TickType_t mOrigCommitDelay;
public:
    operator bool() const { return mIter != nullptr; }
    NvsIterator(NvsHandle& nvs, nvs_type_t type, bool getInfo=true)
    :mNvs(nvs)
    {
        MutexLocker locker(nvs.mMutex);
        mOrigCommitDelay = nvs.setAutoCommitOsTicks(0); // disables commit delay and commits current pending items
        auto err = nvs_entry_find("nvs", nvs.mNsName.get(), type, &mIter);
        if (err) {
            mIter = nullptr;
            ESP_LOGE(nvs.tag(), "Error obtaining iterator: %s", esp_err_to_name(err));
            return;
        }
        if (getInfo) {
            nvs_entry_info(mIter, this);
        }
        else {
            memset(static_cast<nvs_entry_info_t*>(this), 0, sizeof(nvs_entry_info_t));
        }
    }
    ~NvsIterator()
    {
        if (mIter) {
            nvs_release_iterator(mIter);
            mIter = nullptr;
        }
        MutexLocker locker(mNvs.mMutex);
        mNvs.setAutoCommitOsTicks(mOrigCommitDelay);
    }
    bool next(bool getInfo=true)
    {
        auto err = nvs_entry_next(&mIter);
        if (err) {
            if (mIter) {
                nvs_release_iterator(mIter);
            }
            if (err != ESP_ERR_NVS_NOT_FOUND) {
                ESP_LOGW(mNvs.tag(), "nvs_entry_next returned error %s", esp_err_to_name(err));
            }
            return false;
        }
        if (getInfo) {
            nvs_entry_info(mIter, this);
        }
        return true;
    }
};

#endif
