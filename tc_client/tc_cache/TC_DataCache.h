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

#include <string>
#include <map>
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

#define CACHE_BLOCK_SIZE 512

#define CACHE_EXPIRE_SECONDS (60 * 1000000)

#define DATA_REFRESH_TIME_SECONDS 5

/*template <
        class string,
        class DataBlock,
        class FastMutex = FastMutex,
        class  FastMutex = FastMutex
>*/
class DataBlock;

// FIXME: use FastMutex or not?
typedef AbstractCache<std::string, DataBlock,
			 StrategyCollection<std::string, DataBlock>, FastMutex,
			 FastMutex> DataCacheBase;

class TC_DataCache : public DataCacheBase
{
	std::unordered_map<std::string, std::unordered_set<size_t>> cached_blocks;
public:
  TC_DataCache(long cacheSize = 1024,
	       Timestamp::TimeDiff expire = CACHE_EXPIRE_SECONDS)
      : DataCacheBase(StrategyCollection<std::string, DataBlock>())
	{
#ifdef _DEBUG
                std::cout << "TC_DataCache - Constructor" << std::endl;
#endif
		_strategy.pushBack(
		    new LRUStrategy<std::string, DataBlock>(cacheSize));
		_strategy.pushBack(
		    new ExpireStrategy<std::string, DataBlock>(expire));
	}

        ~TC_DataCache()
        {
#ifdef _DEBUG
                std::cout << "TC_DataCache - Destructor" << std::endl;
#endif
        }

        inline void AddBlockToMap(std::string path, size_t block_no)
        {
                cached_blocks[path].insert(block_no);
        }

        inline void RemoveBlockFromMap(std::string path, size_t block_no)
        {
		auto it = cached_blocks.find(path);
                if (it != cached_blocks.end())
                        it->second.erase(block_no);
        }
	bool isCached(std::string path)
	{
		// FIXME: we need to remove blocks from "cached_blocks"
		// accordingly when the caching library evicts blocks.
		auto it = cached_blocks.find(path);
		return (it != cached_blocks.end()) && !it->second.empty();
	}
	void put(const std::string path, size_t offset, size_t length,
		 char *data);
	void remove(std::string path);
	void remove(std::string path, size_t offset, size_t length);
	int get(const std::string path, size_t offset, size_t length,
		char *buf, bool *revalidate);

private:
        TC_DataCache(const TC_DataCache& aCache);
	TC_DataCache &operator=(const TC_DataCache &aCache);
};

class DataBlock {
public:
	char *data;
	size_t len;
	int start_idx;	
	std::string path;
	size_t block_no;
	TC_DataCache *data_cache;
	time_t timestamp;

	DataBlock(char *d, size_t size, int start, std::string p, size_t b,
		  TC_DataCache *dc)
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

void TC_DataCache::put(const std::string path, size_t offset, size_t length,
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
		std::string key = path + std::to_string(offset / CACHE_BLOCK_SIZE);
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
		std::string key = path + std::to_string((offset + i) / CACHE_BLOCK_SIZE);
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

void TC_DataCache::remove(std::string path)
{
	std::unordered_map<std::string, std::unordered_set<size_t> >::iterator it;
	std::string key;

	it = cached_blocks.find(path);
	if (it != cached_blocks.end()) {
		for (const auto &block_no : it->second) {
			key = path + std::to_string(block_no);
			DataCacheBase::remove(key);
#ifdef _DEBUG
			cout << "Removed " << key << std::endl;
#endif
		}
		cached_blocks.erase(path);
	}
	return;
}

void TC_DataCache::remove(std::string path, size_t offset, size_t length)
{
	std::unordered_map<std::string, std::unordered_set<size_t> >::iterator it;
	std::string key;

	it = cached_blocks.find(path);
	if (it != cached_blocks.end()) {
		for (const auto &block_no : it->second) {
			if (block_no < offset / CACHE_BLOCK_SIZE ||
			    block_no >= (length + offset) / CACHE_BLOCK_SIZE)
				continue;
			key = path + std::to_string(block_no);
			DataCacheBase::remove(key);
			RemoveBlockFromMap(path, block_no);
#ifdef _DEBUG
			cout << "Removed " << key << std::endl;
#endif
		}
		if (it->second.empty()) {
			cached_blocks.erase(path);
		}
	}
	return;
}

int TC_DataCache::get(const std::string path, size_t offset, size_t length,
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
		std::string key = path + std::to_string((offset + i) / CACHE_BLOCK_SIZE);
		SharedPtr<DataBlock> ptrElem = DataCacheBase::get(key);
		if (ptrElem.isNull() || ptrElem->start_idx != 0) {
#ifdef _DEBUG
			cout << "Found " << key << "Bytes " << read_len
			     << std::endl;
#endif
			if (time(NULL) - ptrElem->timestamp >= DATA_REFRESH_TIME_SECONDS)
				*revalidate = true;
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

#endif // TC_DataCache_INCLUDED
