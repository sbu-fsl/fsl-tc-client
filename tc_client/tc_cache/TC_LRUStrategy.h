//
// TC_LRUStrategy.h
//
// based on: 
// Library: Foundation
// Package: Cache
// Module:  LRUStrategy
//
// Definition of the TC_LRUStrategy class.
//

#ifndef TC_LRUStrategy_INCLUDED
#define TC_LRUStrategy_INCLUDED


#include "Poco/KeyValueArgs.h"
#include "Poco/ValidArgs.h"
#include "Poco/AbstractStrategy.h"
#include "Poco/EventArgs.h"
#include "Poco/Exception.h"
#include <list>
#include <map>
#include <cstddef>
#include "TC_Debug.h"

using namespace Poco;

template <class TKey, class TValue>
class TC_LRUStrategy: public AbstractStrategy<TKey, TValue>
	/// An TC_LRUStrategy implements least recently used cache replacement.
{
public:
	typedef std::list<TKey>                   Keys;
	typedef typename Keys::iterator           Iterator;
	typedef typename Keys::const_iterator     ConstIterator;
	typedef std::map<TKey, Iterator>          KeyIndex;
	typedef typename KeyIndex::iterator       IndexIterator;
	typedef typename KeyIndex::const_iterator ConstIndexIterator;

public:
	TC_LRUStrategy(std::size_t size): 
		_size(size)
	{
		if (_size < 1) throw InvalidArgumentException("size must be > 0");
	}

	~TC_LRUStrategy()
	{
	}

	void onAdd(const void*, const KeyValueArgs <TKey, TValue>& args)
	{
#ifdef _DEBUG
		std::cout << "TC_LRUStrategy - onAdd \n";
#endif
		_keys.push_front(args.key());
		std::pair<IndexIterator, bool> stat = _keyIndex.insert(std::make_pair(args.key(), _keys.begin()));
		if (!stat.second)
		{
			stat.first->second = _keys.begin();
		}
	}

	void onRemove(const void*, const TKey& key)
	{
#ifdef _DEBUG
		std::cout << "TC_LRUStrategy - onRemove \n";
#endif
		IndexIterator it = _keyIndex.find(key);

		if (it != _keyIndex.end())
		{
			_keys.erase(it->second);
			_keyIndex.erase(it);
		}
	}

	void onGet(const void*, const TKey& key)
	{
#ifdef _DEBUG
		std::cout << "TC_LRUStrategy - onGet \n";
#endif
		// LRU: in case of an hit, move to begin
		IndexIterator it = _keyIndex.find(key);

		if (it != _keyIndex.end())
		{
			_keys.splice(_keys.begin(), _keys, it->second); //_keys.erase(it->second)+_keys.push_front(key);
			it->second = _keys.begin();
		}
	}

	void onClear(const void*, const EventArgs& args)
	{
#ifdef _DEBUG
		std::cout << "TC_LRUStrategy - onClear \n";
#endif
		_keys.clear();
		_keyIndex.clear();
	}

	void onIsValid(const void*, ValidArgs<TKey>& args)
	{
#ifdef _DEBUG
		std::cout << "TC_LRUStrategy - onIsValid \n";
#endif
		if (_keyIndex.find(args.key()) == _keyIndex.end())
		{
			args.invalidate();
		}
	}

	void onReplace(const void*, std::set<TKey>& elemsToRemove)
	{
#ifdef _DEBUG
		std::cout << "TC_LRUStrategy - onReplace \n";
#endif
		// Note: replace only informs the cache which elements
		// it would like to remove!
		// it does not remove them on its own!
		std::size_t curSize = _keyIndex.size();

		if (curSize < _size)
		{
			return;
		}

		std::size_t diff = curSize - _size;
		Iterator it = --_keys.end(); //--keys can never be invoked on an empty list due to the minSize==1 requirement of LRU
		std::size_t i = 0;

		while (i++ < diff) 
		{
			elemsToRemove.insert(*it);
			if (it != _keys.begin())
			{
				--it;
			}
		}
	}

protected:
	std::size_t _size;     /// Number of keys the cache can store.
	Keys        _keys;
	KeyIndex    _keyIndex; /// For faster access to _keys
};

#endif // TC_LRUStrategy_INCLUDED
