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
#include "TC_AbstractCache.h"
#include "Poco/StrategyCollection.h"
#include "Poco/SharedPtr.h"
#include "TC_ExpireStrategy.h"
#include "TC_LRUStrategy.h"
#include "TC_Debug.h"
//remove this
using namespace std;
using namespace Poco;

#define CACHE_BLOCK_SIZE 512

class DataBlock {
public:
	char *data;
	int len;
	int start_idx;	
	//pthread_rwlock_t dataLock;

	DataBlock(char *d, int size, int start) {
#ifdef _DEBUG
		cout << "DataBlock: constructor \n";
#endif
 		//pthread_rwlock_init(&attrLock, NULL);
		data = (char *) malloc(CACHE_BLOCK_SIZE);
		len = size;
		start_idx = start;
		memcpy(data + start, d, size);
	}

	~DataBlock() {
#ifdef _DEBUG
		cout << "DataBlock: destructor \n";
#endif
		if (data != NULL) {
			free(data);
		}
		//pthread_rwlock_destroy(&dataLock);
	}
};

template < 
	class TKey,
	class TValue,
	class TMutex = FastMutex, 
	class TEventMutex = FastMutex
>
class TC_DataCache: public TC_AbstractCache<TKey, TValue, StrategyCollection<TKey, TValue>, TMutex, TEventMutex>
	/// An TC_DataCache combines LRU caching and time based expire caching.
	/// It cache entries for a fixed time period (per default 10 minutes)
	/// but also limits the size of the cache (per default: 1024).
{
public:
	TC_DataCache(long cacheSize = 1024, Timestamp::TimeDiff expire = 600000): 
		TC_AbstractCache<TKey, TValue, StrategyCollection<TKey, TValue>, TMutex, TEventMutex>(StrategyCollection<TKey, TValue>())
	{
#ifdef _DEBUG
		std::cout << "TC_DataCache - Constructor" << std::endl;
#endif
		this->_strategy.pushBack(new TC_LRUStrategy<TKey, TValue>(cacheSize));
		this->_strategy.pushBack(new TC_ExpireStrategy<TKey, TValue>(expire));
	}

	~TC_DataCache()
	{
#ifdef _DEBUG
		std::cout << "TC_DataCache - Destructor" << std::endl;
#endif
	}

	void put(const string path, size_t offset, size_t length, char *data)
 	{
		size_t i = 0;
		size_t write_len = 0;
		int block_size;
		int delta_offset = offset % CACHE_BLOCK_SIZE;
		if (length < CACHE_BLOCK_SIZE) {
			return;
		}
		if (offset % CACHE_BLOCK_SIZE != 0) {
			offset = offset - delta_offset;
			string key = path + to_string(offset/CACHE_BLOCK_SIZE);
			SharedPtr<DataBlock> ptrElem =
				TC_AbstractCache<TKey, TValue, StrategyCollection<TKey, TValue>,
                                                TMutex, TEventMutex>::get(key);
			if (!ptrElem.isNull()) {
				memcpy(ptrElem->data + delta_offset,
					data, CACHE_BLOCK_SIZE - delta_offset);

				if (ptrElem->start_idx > delta_offset) {
					ptrElem->start_idx = delta_offset;
					ptrElem->len = CACHE_BLOCK_SIZE - delta_offset;
				}

				TC_AbstractCache<TKey, TValue, StrategyCollection<TKey, TValue>,
                                                        TMutex, TEventMutex>::add(key, ptrElem);
			}
			else {
				DataBlock *db = new DataBlock(data,
					CACHE_BLOCK_SIZE - delta_offset,
					delta_offset);
				TC_AbstractCache<TKey, TValue, StrategyCollection<TKey, TValue>,
                                                        TMutex, TEventMutex>::add(key, db);
			}
			write_len += CACHE_BLOCK_SIZE - delta_offset;
			i += CACHE_BLOCK_SIZE;
		}
		while(write_len < length) {
			string key = path + to_string((offset+i)/CACHE_BLOCK_SIZE);
			if (length - write_len < CACHE_BLOCK_SIZE) {
				SharedPtr<DataBlock> ptrElem =
                                       TC_AbstractCache<TKey, TValue, StrategyCollection<TKey, TValue>,
                                               TMutex, TEventMutex>::get(key);
                               	if (!ptrElem.isNull()) {
					memcpy(ptrElem->data,
						data + write_len, length - write_len);

					if (ptrElem->start_idx == 0 &&
						ptrElem->len < length - write_len) {
						ptrElem->len = length - write_len;
					}
					/*else if (ptrElem->start_idx > 0){
						ptrElem->len = length - i + 
					}
						//What if within a block CCCCCXCCCCCC
*/
                                       	TC_AbstractCache<TKey, TValue, StrategyCollection<TKey, TValue>,
                                               	        TMutex, TEventMutex>::add(key, ptrElem);
                               	}
				else {
					DataBlock *db = new DataBlock(data + write_len, length - write_len, 0);
					TC_AbstractCache<TKey, TValue, StrategyCollection<TKey, TValue>,
						TMutex, TEventMutex>::add(key, db);
				}
				return;
			}
			else {
				DataBlock *db = new DataBlock(data + write_len, CACHE_BLOCK_SIZE, 0);
				TC_AbstractCache<TKey, TValue, StrategyCollection<TKey, TValue>,
						TMutex, TEventMutex>::add(key, db);
			}
			i += CACHE_BLOCK_SIZE;
			write_len += CACHE_BLOCK_SIZE;
		}
	}

	void remove(string path)
	{
		return;
	}

	void remove(string path, size_t offset, size_t length)
	{
		return;
	}

	int get(const string path, size_t offset, size_t length, char *buf)
	{
                size_t i = 0;
		size_t read_len = 0;
		size_t delta_offset = offset % CACHE_BLOCK_SIZE;

		if (length < CACHE_BLOCK_SIZE || offset % CACHE_BLOCK_SIZE != 0) {
			return 0;
		}
		while(true) {
			if (offset % CACHE_BLOCK_SIZE != 0)
				offset = offset - delta_offset;
			string key = path + to_string((offset+i)/CACHE_BLOCK_SIZE);
                        SharedPtr<DataBlock> ptrElem = 
					TC_AbstractCache<TKey, TValue, StrategyCollection<TKey, TValue>, TMutex, TEventMutex>::get(key);
                        if (ptrElem.isNull() || ptrElem->start_idx != 0) {
                                return read_len;
                        }
                        memcpy(buf + read_len, ptrElem->data + delta_offset, ptrElem->len - delta_offset);
			//Handle case when less len needs to be copied
			read_len += ptrElem->len - delta_offset;
			delta_offset = 0;
                        if (length <= read_len){
                                return read_len;
                        }
                        i += CACHE_BLOCK_SIZE;
                }
	}

private:
	TC_DataCache(const TC_DataCache& aCache);
	TC_DataCache& operator = (const TC_DataCache& aCache);
};

#endif // TC_DataCache_INCLUDED
