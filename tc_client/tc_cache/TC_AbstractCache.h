//
// TC_AbstractCache.h
//
// Library: Foundation
// Package: Cache
// Module:  AbstractCache
//
// Definition of the TC_AbstractCache class.
//


#ifndef TC_AbstractCache_INCLUDED
#define TC_AbstractCache_INCLUDED

#include "Poco/KeyValueArgs.h"
#include "Poco/ValidArgs.h" 
#include "Poco/Mutex.h"
#include "Poco/Exception.h"
#include "Poco/FIFOEvent.h"
#include "Poco/EventArgs.h"
#include "Poco/Delegate.h"
#include "Poco/SharedPtr.h"
#include <map>
#include <set>
#include <cstddef>
#include <string> 
#include <unordered_map>
#include <iostream>
#include "TC_Debug.h"

using namespace Poco;

template <class TKey, class TValue, class TStrategy, class TMutex = FastMutex, class TEventMutex = FastMutex>
class TC_AbstractCache
	/// An TC_AbstractCache is the interface of all caches. 
{
public:
	FIFOEvent<const KeyValueArgs<TKey, TValue >, TEventMutex > Add;
	FIFOEvent<const KeyValueArgs<TKey, TValue >, TEventMutex > Update;
	FIFOEvent<const TKey, TEventMutex>                         Remove;
	FIFOEvent<const TKey, TEventMutex>                         Get;
	FIFOEvent<const EventArgs, TEventMutex>                    Clear;

	typedef std::map<TKey, SharedPtr<TValue > > DataHolder;
	typedef typename DataHolder::iterator       Iterator;
	typedef typename DataHolder::const_iterator ConstIterator;
	typedef std::set<TKey>                      KeySet;

	TC_AbstractCache()
	{
#ifdef _DEBUG
		std::cout << "TC_AbstractCache - default constructor \n";
#endif
		initialize();
	}

	TC_AbstractCache(const TStrategy& strat): _strategy(strat)
	{
#ifdef _DEBUG
		std::cout << "TC_AbstractCache - strategy constructor \n";
#endif
		initialize();
	}

	virtual ~TC_AbstractCache()
	{
		try
		{
			uninitialize();
		}
		catch (...)
		{
			poco_unexpected();
		}
	}

	void add(const TKey& key, const TValue& val)
		/// Adds the key value pair to the cache.
		/// If for the key already an entry exists, it will be overwritten.
	{
#ifdef _DEBUG
		std::cout << "TC_AbstractCache - add \n";
#endif
		typename TMutex::ScopedLock lock(_mutex);
		doAdd(key, val);
	}

	void update(const TKey& key, const TValue& val)
		/// Adds the key value pair to the cache. Note that adding a NULL SharedPtr will fail!
		/// If for the key already an entry exists, it will be overwritten.
		/// The difference to add is that no remove or add events are thrown in this case, 
		/// just a simply silent update is performed
		/// If the key doesnot exist the behavior is equal to add, ie. an add event is thrown
	{
#ifdef _DEBUG
		std::cout << "TC_AbstractCache - update \n";
#endif
		typename TMutex::ScopedLock lock(_mutex);
		doUpdate(key, val);
	}

	void add(const TKey& key, SharedPtr<TValue > val)
		/// Adds the key value pair to the cache. Note that adding a NULL SharedPtr will fail!
		/// If for the key already an entry exists, it will be overwritten, ie. first a remove event
		/// is thrown, then a add event
	{
#ifdef _DEBUG
		std::cout << "TC_AbstractCache - add \n";
#endif
		typename TMutex::ScopedLock lock(_mutex);
		doAdd(key, val);
	}

	void update(const TKey& key, SharedPtr<TValue > val)
		/// Adds the key value pair to the cache. Note that adding a NULL SharedPtr will fail!
		/// If for the key already an entry exists, it will be overwritten.
		/// The difference to add is that no remove or add events are thrown in this case, 
		/// just an Update is thrown
		/// If the key doesnot exist the behavior is equal to add, ie. an add event is thrown
	{
#ifdef _DEBUG
		std::cout << "TC_AbstractCache - update \n";
#endif
		typename TMutex::ScopedLock lock(_mutex);
		doUpdate(key, val);
	}

	void remove(const TKey& key)
		/// Removes an entry from the cache. If the entry is not found,
		/// the remove is ignored.
	{
#ifdef _DEBUG
		std::cout << "TC_AbstractCache - remove \n";
#endif
		typename TMutex::ScopedLock lock(_mutex);
		Iterator it = _data.find(key);
		doRemove(it);
	}

	bool has(const TKey& key) const
		/// Returns true if the cache contains a value for the key.
	{
#ifdef _DEBUG
		std::cout << "TC_AbstractCache - has \n";
#endif
		typename TMutex::ScopedLock lock(_mutex);
		return doHas(key);
	}

	SharedPtr<TValue> get(const TKey& key)
		/// Returns a SharedPtr of the value. The SharedPointer will remain valid
		/// even when cache replacement removes the element.
		/// If for the key no value exists, an empty SharedPtr is returned.
	{
#ifdef _DEBUG
		std::cout << "TC_AbstractCache - get \n";
#endif
		typename TMutex::ScopedLock lock(_mutex);
		return doGet (key);
	}

	void clear()
		/// Removes all elements from the cache.
	{
#ifdef _DEBUG
		std::cout << "TC_AbstractCache - clear \n";
#endif
		typename TMutex::ScopedLock lock(_mutex);
		doClear();
	}

	std::size_t size()
		/// Returns the number of cached elements
	{
#ifdef _DEBUG
		std::cout << "TC_AbstractCache - size \n";
#endif
		typename TMutex::ScopedLock lock(_mutex);
		doReplace();
		return _data.size();
	}

	void forceReplace()
		/// Forces cache replacement. Note that Poco's cache strategy use for efficiency reason no background thread
		/// which periodically triggers cache replacement. Cache Replacement is only started when the cache is modified
		/// from outside, i.e. add is called, or when a user tries to access an cache element via get.
		/// In some cases, i.e. expire based caching where for a long time no access to the cache happens,
		/// it might be desirable to be able to trigger cache replacement manually.
	{
#ifdef _DEBUG
		std::cout << "TC_AbstractCache - forceReplace \n";
#endif
		typename TMutex::ScopedLock lock(_mutex);
		doReplace();
	}

	std::set<TKey> getAllKeys()
		/// Returns a copy of all keys stored in the cache
	{
#ifdef _DEBUG
		std::cout << "TC_AbstractCache - getAllKeys \n";
#endif
		typename TMutex::ScopedLock lock(_mutex);
		doReplace();
		ConstIterator it = _data.begin();
		ConstIterator itEnd = _data.end();
		std::set<TKey> result;
		for (; it != itEnd; ++it)
			result.insert(it->first);

		return result;
	}

protected:
	mutable FIFOEvent<ValidArgs<TKey> > IsValid;
	mutable FIFOEvent<KeySet>           Replace;

	void initialize()
		/// Sets up event registration.
	{
#ifdef _DEBUG
		std::cout << "TC_AbstractCache - Initialize\n";
#endif
		Add		+= Delegate<TStrategy, const KeyValueArgs<TKey, TValue> >(&_strategy, &TStrategy::onAdd);
		Update	+= Delegate<TStrategy, const KeyValueArgs<TKey, TValue> >(&_strategy, &TStrategy::onUpdate);
		Remove	+= Delegate<TStrategy, const TKey>(&_strategy, &TStrategy::onRemove);
		Get		+= Delegate<TStrategy, const TKey>(&_strategy, &TStrategy::onGet);
		Clear	+= Delegate<TStrategy, const EventArgs>(&_strategy, &TStrategy::onClear);
		IsValid	+= Delegate<TStrategy, ValidArgs<TKey> >(&_strategy, &TStrategy::onIsValid);
		Replace	+= Delegate<TStrategy, KeySet>(&_strategy, &TStrategy::onReplace);
	}

	void uninitialize()
		/// Reverts event registration.
	{
#ifdef _DEBUG
		std::cout << "TC_AbstractCache - uninitialize \n";
#endif
		Add		-= Delegate<TStrategy, const KeyValueArgs<TKey, TValue> >(&_strategy, &TStrategy::onAdd );
		Update	-= Delegate<TStrategy, const KeyValueArgs<TKey, TValue> >(&_strategy, &TStrategy::onUpdate);
		Remove	-= Delegate<TStrategy, const TKey>(&_strategy, &TStrategy::onRemove);
		Get		-= Delegate<TStrategy, const TKey>(&_strategy, &TStrategy::onGet);
		Clear	-= Delegate<TStrategy, const EventArgs>(&_strategy, &TStrategy::onClear);
		IsValid	-= Delegate<TStrategy, ValidArgs<TKey> >(&_strategy, &TStrategy::onIsValid);
		Replace	-= Delegate<TStrategy, KeySet>(&_strategy, &TStrategy::onReplace);
	}

	void doAdd(const TKey& key, const TValue& val)
		/// Adds the key value pair to the cache.
		/// If for the key already an entry exists, it will be overwritten.
	{
#ifdef _DEBUG
		std::cout << "TC_AbstractCache - doAdd \n";
#endif
		Iterator it = _data.find(key);
		doRemove(it);

		KeyValueArgs<TKey, TValue> args(key, val);
		Add.notify(this, args);
		_data.insert(std::make_pair(key, SharedPtr<TValue>(new TValue(val))));
		
		doReplace();
	}

	void doAdd(const TKey& key, SharedPtr<TValue>& val)
		/// Adds the key value pair to the cache.
		/// If for the key already an entry exists, it will be overwritten.
	{
#ifdef _DEBUG
		std::cout << "TC_AbstractCache - doAdd shared ptr \n";
#endif
		Iterator it = _data.find(key);
		doRemove(it);

		KeyValueArgs<TKey, TValue> args(key, *val);
		Add.notify(this, args);
		_data.insert(std::make_pair(key, val));
		
		doReplace();
	}

	void doUpdate(const TKey& key, const TValue& val)
		/// Adds the key value pair to the cache.
		/// If for the key already an entry exists, it will be overwritten.
	{
#ifdef _DEBUG
		std::cout << "TC_AbstractCache - doUpdate \n";
#endif
		KeyValueArgs<TKey, TValue> args(key, val);
		Iterator it = _data.find(key);
		if (it == _data.end())
		{
			Add.notify(this, args);
			_data.insert(std::make_pair(key, SharedPtr<TValue>(new TValue(val))));
		}
		else
		{
			Update.notify(this, args);
			it->second = SharedPtr<TValue>(new TValue(val));
		}
		
		doReplace();
	}

	void doUpdate(const TKey& key, SharedPtr<TValue>& val)
		/// Adds the key value pair to the cache.
		/// If for the key already an entry exists, it will be overwritten.
	{
#ifdef _DEBUG
		std::cout << "TC_AbstractCache - doUpdate \n";
#endif
		KeyValueArgs<TKey, TValue> args(key, *val);
		Iterator it = _data.find(key);
		if (it == _data.end())
		{
			Add.notify(this, args);
			_data.insert(std::make_pair(key, val));
		}
		else
		{
			Update.notify(this, args);
			it->second = val;
		}
		
		doReplace();
	}

	void doRemove(Iterator it) 
		/// Removes an entry from the cache. If the entry is not found
		/// the remove is ignored.
	{
#ifdef _DEBUG
		std::cout << "TC_AbstractCache - doRemove \n";
#endif
		if (it != _data.end())
		{
			Remove.notify(this, it->first);
#ifndef _DEBUG
			std::cout << "removing " << (*(it->second))->path  << ", new ref count: " \
				  << (it->second).referenceCount() << std::endl;
#endif
			_data.erase(it);
		}
	}

	bool doHas(const TKey& key) const
		/// Returns true if the cache contains a value for the key
	{
#ifdef _DEBUG
		std::cout << "TC_AbstractCache - doHas \n";
#endif
		// ask the strategy if the key is valid
		ConstIterator it = _data.find(key);
		bool result = false;

		if (it != _data.end())
		{
			ValidArgs<TKey> args(key);
			IsValid.notify(this, args);
			result = args.isValid();
		}

		return result;
	}

	SharedPtr<TValue> doGet(const TKey& key) 
		/// Returns a SharedPtr of the cache entry, returns 0 if for
		/// the key no value was found
	{
#ifdef _DEBUG
		std::cout << "TC_AbstractCache - doGet \n";
#endif
		Iterator it = _data.find(key);
		SharedPtr<TValue> result;

		if (it != _data.end())
		{	
			// inform all strategies that a read-access to an element happens
			Get.notify(this, key);
			// ask all strategies if the key is valid
			ValidArgs<TKey> args(key);
			IsValid.notify(this, args);

			if (!args.isValid())
			{
				doRemove(it);
			}
			else
			{
				result = it->second;
			}
		}

		return result;
	}

	void doClear()
	{
#ifdef _DEBUG
		std::cout << "TC_AbstractCache - doClear \n";
#endif
		static EventArgs _emptyArgs;
		Clear.notify(this, _emptyArgs);
		_data.clear();
	}

	void doReplace()
	{
#ifdef _DEBUG
		std::cout << "TC_AbstractCache - doReplace \n";
#endif
		std::set<TKey> delMe;
		Replace.notify(this, delMe);
		// delMe contains the to be removed elements
		typename std::set<TKey>::const_iterator it    = delMe.begin();
		typename std::set<TKey>::const_iterator endIt = delMe.end();

		for (; it != endIt; ++it)
		{
			Iterator itH = _data.find(*it);
			doRemove(itH);
		}
	}

	TStrategy		_strategy;
	mutable DataHolder	_data;
	mutable TMutex		_mutex;

private:
	TC_AbstractCache(const TC_AbstractCache& aCache);
	TC_AbstractCache& operator = (const TC_AbstractCache& aCache);
};

#endif // TC_AbstractCache_INCLUDED
