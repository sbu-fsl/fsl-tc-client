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
#include <sys/types.h>
#include <sys/stat.h>
#include <unordered_set>
#include "TC_AbstractCache.h"
#include "Poco/StrategyCollection.h"
#include "Poco/SharedPtr.h"
#include "TC_ExpireStrategy.h"
#include "TC_LRUStrategy.h"
#include "TC_Debug.h"
//remove this
using namespace Poco;

#define CACHE_BLOCK_SIZE 512

/*template <
        class string,
        class DataBlock,
        class FastMutex = FastMutex,
        class  FastMutex = FastMutex
>*/
class DataBlock;

// FIXME: use FastMutex or not?
typedef TC_AbstractCache<std::string, DataBlock,
			 StrategyCollection<std::string, DataBlock>, FastMutex,
			 FastMutex> DataCacheBase;

class TC_DataCache : public DataCacheBase
{
	std::unordered_map<std::string, unordered_set<size_t>> cached_blocks;
public:
  TC_DataCache(long cacheSize = 1024, Timestamp::TimeDiff expire = 600000)
      : DataCacheBase(StrategyCollection<std::string, DataBlock>())
	{
#ifdef _DEBUG
                std::cout << "TC_DataCache - Constructor" << std::endl;
#endif
		_strategy.pushBack(
		    new TC_LRUStrategy<std::string, DataBlock>(cacheSize));
		_strategy.pushBack(
		    new TC_ExpireStrategy<std::string, DataBlock>(expire));
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
                if (cached_blocks.find(path) != cached_blocks.end())
                        cached_blocks[path].erase(block_no);
        }
	bool isCached(std::string path)
	{
		return (cached_blocks.find(path) != cached_blocks.end());
	}
	void put(const std::string path, size_t offset, size_t length,
		 char *data);
	void remove(std::string path);
	void remove(std::string path, size_t offset, size_t length);
	int get(const std::string path, size_t offset, size_t length,
		char *buf);

private:
        TC_DataCache(const TC_DataCache& aCache);
        TC_DataCache& operator = (const TC_DataCache& aCache);
};

class DataBlock {
public:
	char *data;
	int len;
	int start_idx;	
	std::string path;
	size_t block_no;
	TC_DataCache *data_cache;
	//pthread_rwlock_t dataLock;

	DataBlock(char *d, int size, int start, std::string p, size_t b,
		  TC_DataCache *dc)
	{
		//pthread_rwlock_init(&attrLock, NULL);
		data = (char *) malloc(CACHE_BLOCK_SIZE);
		len = size;
		start_idx = start;
		memcpy(data + start, d, size);
		path = p;
		block_no = b;
		data_cache = dc;
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
	int block_size;
	int delta_offset = offset % CACHE_BLOCK_SIZE;
	if (length < CACHE_BLOCK_SIZE) {
		return;
	}
	if (offset % CACHE_BLOCK_SIZE != 0) {
#ifdef _DEBUG
		printf("Not alligned with cache block");
#endif
		offset = offset - delta_offset;
		string key = path + to_string(offset / CACHE_BLOCK_SIZE);
		SharedPtr<DataBlock> ptrElem = DataCacheBase::get(key);
		if (!ptrElem.isNull()) {
			memcpy(ptrElem->data + delta_offset, data,
			       CACHE_BLOCK_SIZE - delta_offset);

			if (ptrElem->start_idx > delta_offset) {
				ptrElem->start_idx = delta_offset;
				ptrElem->len = CACHE_BLOCK_SIZE - delta_offset;
			}

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
		string key = path + to_string((offset + i) / CACHE_BLOCK_SIZE);
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

void TC_DataCache::remove(string path)
{
	unordered_map<string, unordered_set<size_t> >::iterator it;
	string key;

	it = cached_blocks.find(path);
	if (it != cached_blocks.end()) {
		for (const auto &block_no : it->second) {
			key = path + to_string(block_no);
			DataCacheBase::remove(key);
#ifdef _DEBUG
			cout << "Removed " << key << std::endl;
#endif
		}
		cached_blocks.erase(path);
	}
	return;
}

void TC_DataCache::remove(string path, size_t offset, size_t length)
{
	unordered_map<string, unordered_set<size_t> >::iterator it;
	string key;

	it = cached_blocks.find(path);
	if (it != cached_blocks.end()) {
		for (const auto &block_no : it->second) {
			if (block_no < offset / CACHE_BLOCK_SIZE ||
			    block_no >= (length + offset) / CACHE_BLOCK_SIZE)
				continue;
			key = path + to_string(block_no);
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

int TC_DataCache::get(const string path, size_t offset, size_t length,
		      char *buf)
{
	size_t i = 0;
	size_t read_len = 0;
	size_t delta_offset = offset % CACHE_BLOCK_SIZE;

	if (length < CACHE_BLOCK_SIZE) {
		return 0;
	}
	while (true) {
		if (offset % CACHE_BLOCK_SIZE != 0)
			offset = offset - delta_offset;
		string key = path + to_string((offset + i) / CACHE_BLOCK_SIZE);
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
