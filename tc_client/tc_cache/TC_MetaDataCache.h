//
// TC_MetaDataCache.h
//
// Creating anther version of ExpireLRUCache, to take on_remove_callback as an input,
// by inheriting AbstractCache.h
//
// Definition of the TC_MetaDataCache class.
//

#ifndef TC_MetaDataCache_INCLUDED
#define TC_MetaDataCache_INCLUDED

#include "TC_AbstractCache.h"
#include "Poco/StrategyCollection.h"
#include "TC_ExpireStrategy.h"
#include "TC_LRUStrategy.h"
#include "TC_Debug.h"

using namespace Poco;

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
	/*
	TC_MetaDataCache(long cacheSize = 1024, Timestamp::TimeDiff expire = 600000): 
		TC_AbstractCache<TKey, TValue, StrategyCollection<TKey, TValue>, TMutex, TEventMutex>(StrategyCollection<TKey, TValue>())
	{
		std::cout << "TC_MetaDataCache - Constructor" << std::endl;
		this->_strategy.pushBack(new TC_LRUStrategy<TKey, TValue>(cacheSize));
		this->_strategy.pushBack(new TC_ExpireStrategy<TKey, TValue>(expire));
	}
	*/
	typedef bool (*on_remove_callback_func)(TValue);
	TC_MetaDataCache(long cacheSize = 1024, Timestamp::TimeDiff expire = 600000, on_remove_callback_func cb_func = NULL): 
		TC_AbstractCache<TKey, TValue, StrategyCollection<TKey, TValue>, TMutex, TEventMutex>(StrategyCollection<TKey, TValue>(), cb_func)
	{
#ifdef _DEBUG
		std::cout << "TC_MetaDataCache - remove CB Constructor" << std::endl;
#endif
		this->_strategy.pushBack(new TC_LRUStrategy<TKey, TValue>(cacheSize));
		this->_strategy.pushBack(new TC_ExpireStrategy<TKey, TValue>(expire));
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
