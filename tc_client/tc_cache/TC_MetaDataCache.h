//
// TC_MetaDataCache.h
//
// Creating anther version of ExpireLRUCache as Metadata cache,
// by inheriting AbstractCache.h
//
// Definition of the TC_MetaDataCache class.
//

#ifndef TC_MetaDataCache_INCLUDED
#define TC_MetaDataCache_INCLUDED

#include <string>
#include <map>
#include <sys/types.h>
#include <sys/stat.h>
#include "TC_AbstractCache.h"
#include "Poco/StrategyCollection.h"
#include "Poco/SharedPtr.h"
#include "Poco/LRUStrategy.h"
#include "Poco/ExpireStrategy.h"
#include "TC_Debug.h"

using namespace std;
using namespace Poco;

#define MD_REFRESH_TIME 5

class DirEntry {
public:
	string path;
	void *fh;
	struct stat attrs;
	SharedPtr<DirEntry> parent;
	bool has_listdir;
	unordered_map<string, SharedPtr<DirEntry>> children;
	pthread_rwlock_t attrLock;
	time_t timestamp;

	DirEntry(string p, void *f = nullptr, struct stat *a = nullptr) : path(p) {
#ifdef _DEBUG
		cout << "DirEntry: constructor \n";
#endif
 		pthread_rwlock_init(&attrLock, NULL);
		has_listdir = false;
		timestamp = time(NULL);
	}

	~DirEntry() {
#ifdef _DEBUG
		cout << "DirEntry: destructor \n";
#endif
		/*if (attrs != nullptr) {
#ifdef _DEBUG
			cout << "~DirEntry: attrs should be released \n";
#endif
			delete attrs;
		}*/
		if (fh != NULL) {
			free(fh);
		}
		pthread_rwlock_destroy(&attrLock);
	}
};

template < 
	class TKey,
	class TValue,
	class TMutex = FastMutex, 
	class TEventMutex = FastMutex
>
class TC_MetaDataCache: public TC_AbstractCache<TKey, TValue, StrategyCollection<TKey, TValue>, TMutex, TEventMutex>
	/// An TC_MetaDataCache combines LRU caching and time based expire caching.
	/// It cache entries for a fixed time period (per default 10 minutes)
	/// but also limits the size of the cache (per default: 1024).
{
public:
	TC_MetaDataCache(long cacheSize = 1024, Timestamp::TimeDiff expire = 600000): 
		TC_AbstractCache<TKey, TValue, StrategyCollection<TKey, TValue>, TMutex, TEventMutex>(StrategyCollection<TKey, TValue>())
	{
#ifdef _DEBUG
		std::cout << "TC_MetaDataCache - Constructor" << std::endl;
#endif
		this->_strategy.pushBack(new Poco::LRUStrategy<TKey, TValue>(cacheSize));
		this->_strategy.pushBack(new Poco::ExpireStrategy<TKey, TValue>(expire));
	}

	~TC_MetaDataCache()
	{
#ifdef _DEBUG
		std::cout << "TC_MetaDataCache - Destructor" << std::endl;
#endif
	}

private:
	TC_MetaDataCache(const TC_MetaDataCache& aCache);
	TC_MetaDataCache& operator = (const TC_MetaDataCache& aCache);
};

#endif // TC_MetaDataCache_INCLUDED
