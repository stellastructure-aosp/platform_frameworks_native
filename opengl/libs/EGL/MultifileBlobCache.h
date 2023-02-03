/*
 ** Copyright 2022, The Android Open Source Project
 **
 ** Licensed under the Apache License, Version 2.0 (the "License");
 ** you may not use this file except in compliance with the License.
 ** You may obtain a copy of the License at
 **
 **     http://www.apache.org/licenses/LICENSE-2.0
 **
 ** Unless required by applicable law or agreed to in writing, software
 ** distributed under the License is distributed on an "AS IS" BASIS,
 ** WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 ** See the License for the specific language governing permissions and
 ** limitations under the License.
 */

#ifndef ANDROID_MULTIFILE_BLOB_CACHE_H
#define ANDROID_MULTIFILE_BLOB_CACHE_H

#include <EGL/egl.h>
#include <EGL/eglext.h>

#include <future>
#include <map>
#include <queue>
#include <string>
#include <thread>
#include <unordered_map>
#include <unordered_set>

namespace android {

struct MultifileHeader {
    EGLsizeiANDROID keySize;
    EGLsizeiANDROID valueSize;
};

struct MultifileEntryStats {
    EGLsizeiANDROID valueSize;
    size_t fileSize;
    time_t accessTime;
};

struct MultifileHotCache {
    int entryFd;
    uint8_t* entryBuffer;
    size_t entrySize;
};

enum class TaskCommand {
    Invalid = 0,
    WriteToDisk,
    Exit,
};

class DeferredTask {
public:
    DeferredTask(TaskCommand command) : mCommand(command) {}

    TaskCommand getTaskCommand() { return mCommand; }

    void initWriteToDisk(std::string fullPath, uint8_t* buffer, size_t bufferSize) {
        mCommand = TaskCommand::WriteToDisk;
        mFullPath = fullPath;
        mBuffer = buffer;
        mBufferSize = bufferSize;
    }

    uint32_t getEntryHash() { return mEntryHash; }
    std::string& getFullPath() { return mFullPath; }
    uint8_t* getBuffer() { return mBuffer; }
    size_t getBufferSize() { return mBufferSize; };

private:
    TaskCommand mCommand;

    // Parameters for WriteToDisk
    uint32_t mEntryHash;
    std::string mFullPath;
    uint8_t* mBuffer;
    size_t mBufferSize;
};

class MultifileBlobCache {
public:
    MultifileBlobCache(size_t maxTotalSize, size_t maxHotCacheSize, const std::string& baseDir);
    ~MultifileBlobCache();

    void set(const void* key, EGLsizeiANDROID keySize, const void* value,
             EGLsizeiANDROID valueSize);
    EGLsizeiANDROID get(const void* key, EGLsizeiANDROID keySize, void* value,
                        EGLsizeiANDROID valueSize);

    void finish();

    size_t getTotalSize() const { return mTotalCacheSize; }
    void trimCache(size_t cacheByteLimit);

private:
    void trackEntry(uint32_t entryHash, EGLsizeiANDROID valueSize, size_t fileSize,
                    time_t accessTime);
    bool contains(uint32_t entryHash) const;
    bool removeEntry(uint32_t entryHash);
    MultifileEntryStats getEntryStats(uint32_t entryHash);

    size_t getFileSize(uint32_t entryHash);
    size_t getValueSize(uint32_t entryHash);

    void increaseTotalCacheSize(size_t fileSize);
    void decreaseTotalCacheSize(size_t fileSize);

    bool addToHotCache(uint32_t entryHash, int fd, uint8_t* entryBufer, size_t entrySize);
    bool removeFromHotCache(uint32_t entryHash);

    bool applyLRU(size_t cacheLimit);

    bool mInitialized;
    std::string mMultifileDirName;

    std::unordered_set<uint32_t> mEntries;
    std::unordered_map<uint32_t, MultifileEntryStats> mEntryStats;
    std::unordered_map<uint32_t, MultifileHotCache> mHotCache;

    size_t mMaxKeySize;
    size_t mMaxValueSize;
    size_t mMaxTotalSize;
    size_t mTotalCacheSize;
    size_t mHotCacheLimit;
    size_t mHotCacheEntryLimit;
    size_t mHotCacheSize;

    // Below are the components used to allow a deferred write

    // Track whether we have pending writes for an entry
    std::multimap<uint32_t, uint8_t*> mDeferredWrites;

    // Functions to work through tasks in the queue
    void processTasks();
    void processTasksImpl(bool* exitThread);
    void processTask(DeferredTask& task);

    // Used by main thread to create work for the worker thread
    void queueTask(DeferredTask&& task);

    // Used by main thread to wait for worker thread to complete all outstanding work.
    void waitForWorkComplete();

    std::thread mTaskThread;
    std::queue<DeferredTask> mTasks;
    std::mutex mWorkerMutex;

    // This condition will block the worker thread until a task is queued
    std::condition_variable mWorkAvailableCondition;

    // This condition will block the main thread while the worker thread still has tasks
    std::condition_variable mWorkerIdleCondition;

    // This bool will track whether all tasks have been completed
    bool mWorkerThreadIdle;
};

}; // namespace android

#endif // ANDROID_MULTIFILE_BLOB_CACHE_H