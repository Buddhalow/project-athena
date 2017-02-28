//
//  FileCache.cpp
//  libraries/model-networking/src
//
//  Created by Zach Pomerantz on 2/21/2017.
//  Copyright 2017 High Fidelity, Inc.
//
//  Distributed under the Apache License, Version 2.0.
//  See the accompanying file LICENSE or http://www.apache.org/licenses/LICENSE-2.0.html
//

#include "FileCache.h"

#include <cstdio>
#include <fstream>
#include <unordered_set>

#include <QDir>

#include <ServerPathUtils.h>

Q_LOGGING_CATEGORY(file_cache, "hifi.file_cache")

static const std::string MANIFEST_NAME = "manifest";

void FileCache::setUnusedFileCacheSize(size_t unusedFilesMaxSize) {
    _unusedFilesMaxSize = std::min(unusedFilesMaxSize, MAX_UNUSED_MAX_SIZE);
    reserve(0);
    emit dirty();
}

void FileCache::setOfflineFileCacheSize(size_t offlineFilesMaxSize) {
    _offlineFilesMaxSize = std::min(offlineFilesMaxSize, MAX_UNUSED_MAX_SIZE);
}

FileCache::FileCache(const std::string& dirname, const std::string& ext, QObject* parent) :
    QObject(parent),
    _dir(createDir(dirname)),
    _ext(ext) {}

FileCache::~FileCache() {
    clear();
}

void fileDeleter(File* file) {
    file->deleter();
}

FilePointer FileCache::writeFile(const Key& key, const char* data, size_t length, void* extra) {
    std::string filepath = getFilepath(key);

    Lock lock(_filesMutex);

    // if file already exists, return it
    FilePointer file = getFile(key);
    if (file) {
        qCWarning(file_cache, "Attempted to overwrite %", key.c_str());
        return file;
    }

    // write the new file
    FILE* saveFile = fopen(filepath.c_str(), "wb");
    if (saveFile != nullptr && fwrite(data, length, 1, saveFile) && fclose(saveFile) == 0) {
        qCInfo(file_cache, "Wrote %s", key.c_str());
        file.reset(createFile(key, filepath, length, extra), &fileDeleter);
        file->_cache = this;
        _files[key] = file;
        _numTotalFiles += 1;
        _totalFilesSize += length;

        emit dirty();
    } else {
        qCWarning(file_cache, "Failed to write %s (%s)", key.c_str(), strerror(errno));
        errno = 0;
    }

    return file;
}

FilePointer FileCache::getFile(const Key& key) {
    FilePointer file;

    Lock lock(_filesMutex);

    // check if file already exists
    const auto it = _files.find(key);
    if (it != _files.cend()) {
        file = it->second.lock();
        if (file) {
            // if it exists, it is active - remove it from the cache
            removeUnusedFile(file);
            qCInfo(file_cache, "Found %s", key.c_str());
            emit dirty();
        } else {
            // if not, remove the weak_ptr
            _files.erase(it);
        }
    }

    return file;
}

File* FileCache::createFile(const Key& key, const std::string& filepath, size_t length, void* extra) {
    return new File(key, filepath, length);
}

std::string FileCache::createDir(const std::string& dirname) {
    QString dirpath = ServerPathUtils::getDataFilePath(dirname.c_str());
    QDir dir(dirpath);

    if (dir.exists()) {
        std::unordered_set<std::string> persistedEntries;
        if (dir.exists(MANIFEST_NAME.c_str())) {
            std::ifstream manifest;
            manifest.open(dir.absoluteFilePath(MANIFEST_NAME.c_str()).toStdString());
            while (manifest.good()) {
                std::string entry;
                manifest >> entry;
                persistedEntries.insert(entry);
                qCInfo(file_cache, "Manifest contents: %s", entry.c_str());
            }
        } else {
            qCWarning(file_cache, "Missing manifest");
        }
        

        foreach(QString filename, dir.entryList()) {
            if (persistedEntries.find(filename.toStdString()) == persistedEntries.cend()) {
                dir.remove(filename);
                qCInfo(file_cache) << "Cleaned" << filename;
            }
        }
        qCDebug(file_cache, "Initiated %s", dirpath.data());
    } else {
        dir.mkpath(dirpath);
        qCDebug(file_cache, "Created %s", dirpath.data());
    }

    return dirpath.toStdString();
}

std::string FileCache::getFilepath(const Key& key) {
    return _dir + '/' + key + '.' + _ext;
}

void FileCache::addUnusedFile(const FilePointer file) {
    {
        Lock lock(_filesMutex);
        _files[file->getKey()] = file;
    }

    reserve(file->getLength());
    file->_LRUKey = ++_lastLRUKey;

    {
        Lock lock(_unusedFilesMutex);
        _unusedFiles.insert({ file->_LRUKey, file });
        _numUnusedFiles += 1;
        _unusedFilesSize += file->getLength();
    }

    emit dirty();
}

void FileCache::removeUnusedFile(const FilePointer file) {
    Lock lock(_unusedFilesMutex);
    const auto it = _unusedFiles.find(file->_LRUKey);
    if (it != _unusedFiles.cend()) {
        _unusedFiles.erase(it);
        _numUnusedFiles -= 1;
        _unusedFilesSize -= file->getLength();
    }
}

void FileCache::reserve(size_t length) {
    Lock unusedLock(_unusedFilesMutex);
    while (!_unusedFiles.empty() &&
            _unusedFilesSize + length > _unusedFilesMaxSize) {
        auto it = _unusedFiles.begin();
        auto file = it->second;
        auto length = file->getLength();

        unusedLock.unlock();
        {
            file->_cache = nullptr;
            Lock lock(_filesMutex);
            _files.erase(file->getKey());
        }
        unusedLock.lock();

        _unusedFiles.erase(it);
        _numTotalFiles -= 1;
        _numUnusedFiles -= 1;
        _totalFilesSize -= length;
        _unusedFilesSize -= length;

        unusedLock.unlock();
        evictedFile(file);
        unusedLock.lock();
    }
}

void FileCache::clear() {
    try {
        std::string manifestPath= _dir + '/' + MANIFEST_NAME;
        std::ofstream manifest(manifestPath);

        bool firstEntry = true;

        {
            Lock lock(_unusedFilesMutex);
            for (const auto& val : _unusedFiles) {
                const FilePointer& file = val.second;
                file->_cache = nullptr;

                if (_unusedFilesSize > _offlineFilesMaxSize) {
                    _unusedFilesSize -= file->getLength();
                } else {
                    if (!firstEntry) {
                        manifest << '\n';
                    }
                    firstEntry = false;
                    manifest << file->getKey();

                    file->_shouldPersist = true;
                    qCInfo(file_cache, "Persisting %s", file->getKey().c_str());
                }
            }
        }

        {
            Lock lock(_filesMutex);
            for (const auto& val : _files) {
                const FilePointer& file = val.second
        }
    } catch (std::exception& e) {
        qCWarning(file_cache, "Failed to write manifest (%s)", e.what());
        for (const auto& val : _unusedFiles) {
            val.second->_shouldPersist = false;
        }
    }

    Lock lock(_unusedFilesMutex);
    _unusedFiles.clear();
}

void File::deleter() {
    if (_cache) {
        FilePointer self(this, &fileDeleter);
        _cache->addUnusedFile(self);
    } else {
        deleteLater();
    }
}

File::~File() {
    QFile file(getFilepath().c_str());
    if (file.exists() && !_shouldPersist) {
        qCInfo(file_cache, "Unlinked %s", getFilepath().c_str());
        file.remove();
    }
}
