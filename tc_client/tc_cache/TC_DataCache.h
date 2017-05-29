//
// TC_DataCache.h
//
// Creating anther version of ExpireLRUCache as Metadata cache,
// by inheriting AbstractCache.h
//
// Definition of the TC_DataCache class.
//
#ifndef TC_DataCache_INCLUDED
#define TC_DataCache_INCLUDED

#include <algorithm>
#include <string>
#include <mutex>
#include <map>
#include <iostream>
#include <unordered_map>
#include <sys/types.h>
#include <sys/stat.h>
#include <unordered_set>
#include "Poco/AbstractCache.h"
#include "Poco/StrategyCollection.h"
#include "Poco/SharedPtr.h"
#include "Poco/ExpireStrategy.h"
#include "Poco/LRUStrategy.h"
#include "TC_Debug.h"
//remove this
using namespace Poco;

#define CACHE_BLOCK_SIZE (256 * 1024)

#define CACHE_EXPIRE_SECONDS (60 * 1000)

#define DATA_REFRESH_TIME_SECONDS 5

/*template <
        class string,
        class DataBlock,
        class FastMutex = FastMutex,
        class  FastMutex = FastMutex
>*/
class DataBlock;

static inline uint32_t Decode(const char* data, size_t l) {
	uint32_t h = 0;
	switch (l) {
	case 4:
		h += static_cast<unsigned char>(data[3]) << 24;
	case 3:
		h += static_cast<unsigned char>(data[2]) << 16;
	case 2:
		h += static_cast<unsigned char>(data[1]) << 8;
	case 1:
		h += static_cast<unsigned char>(data[0]);
		break;
	}
	return h;
}

static inline uint32_t Hash(const std::string& str, uint32_t seed=8887) {
	const uint32_t m = 0xc6a4a793;
	const char* data = str.data();
	size_t n = str.length();
	uint32_t h = seed ^ (n * m);

	while (n > 0) {
		size_t l = std::min<uint32_t>(n, 4);
		uint32_t w = Decode(data, l);
		data += l;
		n -= l;
		h += w;
		h *= m;
		h ^= (h >> 16);
	}

	return h;
}

static inline std::string GetBlockKey(const std::string &path, size_t block_no)
{
  return  path + std::to_string(block_no);
}

// FIXME: use FastMutex or not?
typedef AbstractCache<std::string, DataBlock,
			 StrategyCollection<std::string, DataBlock>, FastMutex,
			 FastMutex> DataCacheBase;

class DataCacheShard : public DataCacheBase
{
	std::mutex mu_;
	std::unordered_map<std::string, std::unordered_set<size_t>> cached_blocks;
public:
  DataCacheShard(long cacheSize = 102400,
	       Timestamp::TimeDiff expire = CACHE_EXPIRE_SECONDS)
      : DataCacheBase(StrategyCollection<std::string, DataBlock>())
	{
#ifdef _DEBUG
                std::cout << "DataCacheShard - Constructor" << std::endl;
#endif
		_strategy.pushBack(
		    new LRUStrategy<std::string, DataBlock>(cacheSize));
		_strategy.pushBack(
		    new ExpireStrategy<std::string, DataBlock>(expire));
	}

        ~DataCacheShard()
        {
#ifdef _DEBUG
                std::cout << "DataCacheShard - Destructor" << std::endl;
#endif
        }

        void AddBlockToMap(const std::string& path, size_t block_no)
        {
		std::lock_guard<std::mutex> lock(mu_);
		cached_blocks[path].insert(block_no);
        }

	void RemoveBlockFromMap(const std::string &path, size_t block_no)
	{
		std::lock_guard<std::mutex> lock(mu_);
		auto it = cached_blocks.find(path);
                if (it != cached_blocks.end())
                        it->second.erase(block_no);
        }
	bool isCached(const std::string& path)
	{
		// FIXME: we need to remove blocks from "cached_blocks"
		// accordingly when the caching library evicts blocks.
		std::lock_guard<std::mutex> lock(mu_);
		auto it = cached_blocks.find(path);
		return (it != cached_blocks.end()) && !it->second.empty();
	}
	void put(const std::string& path, size_t offset, size_t length,
		 char *data);
	void remove(const std::string& path);
	void remove(const std::string& path, size_t offset, size_t length);
	int get(const std::string& path, size_t offset, size_t length,
		char *buf, bool *revalidate);

private:
        DataCacheShard(const DataCacheShard& aCache);
	DataCacheShard &operator=(const DataCacheShard &aCache);
};

class DataBlock {
public:
	char *data;
	size_t len;
	int start_idx;	
	std::string path;
	size_t block_no;
	DataCacheShard *data_cache;
	time_t timestamp;

	DataBlock(char *d, size_t size, int start, std::string p, size_t b,
		  DataCacheShard *dc)
	{
		data = (char *) malloc(CACHE_BLOCK_SIZE);
		len = size;
		start_idx = start;
		memcpy(data + start, d, size);
		path = p;
		block_no = b;
		data_cache = dc;
		timestamp = time(NULL);
	}

	~DataBlock() {
#ifdef _DEBUG
		cout << "DataBlock: destructor "<<path<<block_no<<endl;
#endif
		if (data != NULL) {
			free(data);
		}
		data_cache->RemoveBlockFromMap(path, block_no);
#ifdef _DEBUG
		cout<<"Destructor for "<<path<<block_no;
#endif
		//pthread_rwlock_destroy(&dataLock);
	}
};

void DataCacheShard::put(const std::string& path, size_t offset, size_t length,
		       char *data)
{
	size_t i = 0;
	size_t write_len = 0;
	int delta_offset = offset % CACHE_BLOCK_SIZE;
	if (length < CACHE_BLOCK_SIZE) {
		return;
	}
	if (offset % CACHE_BLOCK_SIZE != 0) {
#ifdef _DEBUG
		printf("Not alligned with cache block");
#endif
		offset = offset - delta_offset;
		std::string key = GetBlockKey(path, offset / CACHE_BLOCK_SIZE);
		SharedPtr<DataBlock> ptrElem = DataCacheBase::get(key);
		if (!ptrElem.isNull()) {
			memcpy(ptrElem->data + delta_offset, data,
			       CACHE_BLOCK_SIZE - delta_offset);

			if (ptrElem->start_idx > delta_offset) {
				ptrElem->start_idx = delta_offset;
				ptrElem->len = CACHE_BLOCK_SIZE - delta_offset;
			}
			ptrElem->timestamp = time(NULL);
			DataCacheBase::add(key, ptrElem);
			AddBlockToMap(path, offset / CACHE_BLOCK_SIZE);
#ifdef _DEBUG
			cout << "Updated " << key << std::endl;
#endif
		} else {
			DataBlock *db = new DataBlock(
			    data, CACHE_BLOCK_SIZE - delta_offset, delta_offset,
			    path, offset / CACHE_BLOCK_SIZE, this);
			DataCacheBase::add(key, db);
			AddBlockToMap(path, offset / CACHE_BLOCK_SIZE);
#ifdef _DEBUG
			cout << "Added " << key << std::endl;
#endif
		}
		write_len += CACHE_BLOCK_SIZE - delta_offset;
		i += CACHE_BLOCK_SIZE;
	}
	while (write_len < length) {
		std::string key =
		    GetBlockKey(path, (offset + i) / CACHE_BLOCK_SIZE);
		if (length - write_len < CACHE_BLOCK_SIZE) {
			SharedPtr<DataBlock> ptrElem = DataCacheBase::get(key);
			if (!ptrElem.isNull()) {
				memcpy(ptrElem->data, data + write_len,
				       length - write_len);

				if (ptrElem->start_idx == 0 &&
				    ptrElem->len < length - write_len) {
					ptrElem->len = length - write_len;
				}
				/*else if (ptrElem->start_idx > 0){
					ptrElem->len = length - i +
				}
					//What if within a block CCCCCXCCCCCC
*/
				ptrElem->timestamp = time(NULL);
				DataCacheBase::add(key, ptrElem);
				AddBlockToMap(path,
					      (offset + i) / CACHE_BLOCK_SIZE);
#ifdef _DEBUG
				cout << "Added " << key << std::endl;
#endif
			} else {
				DataBlock *db = new DataBlock(
				    data + write_len, length - write_len, 0,
				    path, (offset + i) / CACHE_BLOCK_SIZE,
				    this);
				DataCacheBase::add(key, db);
				AddBlockToMap(path,
					      (offset + i) / CACHE_BLOCK_SIZE);
#ifdef _DEBUG
				cout << "Added " << key << std::endl;
#endif
			}
			return;
		} else {
			DataBlock *db = new DataBlock(
			    data + write_len, CACHE_BLOCK_SIZE, 0, path,
			    (offset + i) / CACHE_BLOCK_SIZE, this);
			DataCacheBase::add(key, db);
			AddBlockToMap(path, (offset + i) / CACHE_BLOCK_SIZE);
#ifdef _DEBUG
			cout << "Added " << key << std::endl;
#endif
		}
		i += CACHE_BLOCK_SIZE;
		write_len += CACHE_BLOCK_SIZE;
	}
}

void DataCacheShard::remove(const std::string& path)
{
	std::unordered_map<std::string, std::unordered_set<size_t> >::iterator it;
	std::unordered_set<size_t> blocks;

	{
		std::lock_guard<std::mutex> lock(mu_);
		it = cached_blocks.find(path);
		if (it == cached_blocks.end())
			return;
		blocks.swap(it->second);
		cached_blocks.erase(it);
	}

	for (size_t block_no : blocks) {
		std::string key = GetBlockKey(path, block_no);
		DataCacheBase::remove(key);
#ifdef _DEBUG
		cout << "Removed " << key << std::endl;
#endif
	}
}

void DataCacheShard::remove(const std::string& path, size_t offset, size_t length)
{
	std::unordered_map<std::string, std::unordered_set<size_t> >::iterator it;

	{
		std::lock_guard<std::mutex> lock(mu_);
		it = cached_blocks.find(path);
		if (it == cached_blocks.end())
			return;
	}

	for (const auto &block_no : it->second) {
		if (block_no < offset / CACHE_BLOCK_SIZE ||
		    block_no >= (length + offset) / CACHE_BLOCK_SIZE)
			continue;
		std::string key = GetBlockKey(path, block_no);
		DataCacheBase::remove(key);
		RemoveBlockFromMap(path, block_no);
#ifdef _DEBUG
		cout << "Removed " << key << std::endl;
#endif
	}


	if (it->second.empty()) {
		std::lock_guard<std::mutex> lock(mu_);
		cached_blocks.erase(path);
	}
}

int DataCacheShard::get(const std::string &path, size_t offset, size_t length,
			char *buf, bool *revalidate)
{
	size_t i = 0;
	size_t read_len = 0;
	size_t delta_offset = offset % CACHE_BLOCK_SIZE;
	*revalidate = false;

	if (length < CACHE_BLOCK_SIZE) {
		return 0;
	}
	while (true) {
		if (offset % CACHE_BLOCK_SIZE != 0)
			offset = offset - delta_offset;
		std::string key = GetBlockKey(path, (offset + i) / CACHE_BLOCK_SIZE);
		SharedPtr<DataBlock> ptrElem = DataCacheBase::get(key);
		if (ptrElem.isNull() || ptrElem->start_idx != 0) {
#ifdef _DEBUG
			cout << "Found " << key << "Bytes " << read_len
			     << std::endl;
#endif
			return read_len;
		}
		if (ptrElem->len - delta_offset < length - read_len) {
			memcpy(buf + read_len, ptrElem->data + delta_offset,
			       ptrElem->len - delta_offset);
			read_len += ptrElem->len - delta_offset;
		} else {
			memcpy(buf + read_len, ptrElem->data + delta_offset,
			       length - read_len);
			read_len = length;
		}
		if (time(NULL) - ptrElem->timestamp >= DATA_REFRESH_TIME_SECONDS)
			*revalidate = true;
		// Handle case when less len needs to be copied
		delta_offset = 0;
		if (length <= read_len) {
#ifdef _DEBUG
			cout << "Found " << key << "Bytes " << read_len
			     << std::endl;
#endif
			return read_len;
		}
		i += CACHE_BLOCK_SIZE;
	}
}

const int kNumCacheShards = 16;

class TC_DataCache
{
public:
	TC_DataCache(long cacheSize = 102400,
			Timestamp::TimeDiff expire = CACHE_EXPIRE_SECONDS) {
		long sizePerShard = cacheSize / kNumCacheShards;
		for (int i = 0; i < kNumCacheShards; ++i) {
			shards_[i] = new DataCacheShard(sizePerShard, expire);
		}
	}
	~TC_DataCache() {
		for (int i = 0; i < kNumCacheShards; ++i) {
			delete shards_[i];
		}
	}
	void AddBlockToMap(const std::string &path, size_t block_no) {
		shard(path)->AddBlockToMap(path, block_no);
	}
	void RemoveBlockFromMap(const std::string &path, size_t block_no) {
		shard(path)->RemoveBlockFromMap(path, block_no);
	}
	bool isCached(const std::string &path) {
		return shard(path)->isCached(path);
	}
	void put(const std::string &path, size_t offset, size_t length,
		 char *data) {
		shard(path)->put(path, offset, length, data);
	}
	void remove(const std::string &path) {
		shard(path)->remove(path);
	}
	void remove(const std::string &path, size_t offset, size_t length) {
		shard(path)->remove(path, offset, length);
	}
	int get(const std::string &path, size_t offset, size_t length,
		char *buf, bool *revalidate) {
		return shard(path)->get(path, offset, length, buf, revalidate);
	}
	void clear() {
		for (int i = 0; i < kNumCacheShards; ++i) {
			shards_[i]->clear();
		}
	}

private:
	DataCacheShard* shard(const std::string& path) {
		return shards_[Hash(path) % kNumCacheShards];
	}

	DataCacheShard* shards_[kNumCacheShards];
};

#endif // TC_DataCache_INCLUDED
