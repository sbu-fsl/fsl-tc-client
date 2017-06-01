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

#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>


#include <string>
#include <map>
#include <mutex>
#include <unordered_map>
#include <sys/types.h>
#include <sys/stat.h>
#include "Poco/AbstractCache.h"
#include "Poco/StrategyCollection.h"
#include "Poco/SharedPtr.h"
#include "Poco/LRUStrategy.h"
#include "Poco/ExpireStrategy.h"
#include "TC_Debug.h"

#include "tc_api.h"

using namespace std;
using namespace Poco;

#define MD_REFRESH_TIME 5

static inline struct file_handle *copyFH(const struct file_handle *old)
{
	size_t oldLen = old->handle_bytes;
	struct file_handle *fh = nullptr;

	fh = (struct file_handle *)malloc(sizeof(*fh) + oldLen);
	if (fh) {
		fh->handle_bytes = oldLen;
		fh->handle_type = old->handle_type;
		memcpy(fh->f_handle, old->f_handle, oldLen);
	}

	return fh;
}

static inline bool matchTime(const struct timespec *ts1,
			     const struct timespec *ts2)
{
	return (ts1->tv_sec == ts2->tv_sec) && (ts1->tv_nsec == ts2->tv_nsec);
}

class DirEntry {
private:
	mutable std::mutex mu_;
	std::string path_;
	struct file_handle *fh = nullptr;
	struct stat attrs_;
	bool has_listdir_ = false;

public:
	SharedPtr<DirEntry> parent;
	std::unordered_map<std::string, SharedPtr<DirEntry>> children;
	time_t timestamp_;

	DirEntry(const std::string &p, const struct vattrs *va = nullptr,
		 struct file_handle *f = nullptr)
	    : path_(p), fh(f)
	{
#ifdef _DEBUG
		cout << "DirEntry: constructor \n";
#endif
		if (va) {
			vattrs2stat(va, &attrs_);
		}
		timestamp_ = time(NULL);
	}

	DirEntry(const struct vattrs *va, SharedPtr<DirEntry> pa) : parent(pa)
	{
		assert(va->file.type == VFILE_PATH);
		path_ = va->file.path;
		vattrs2stat(va, &attrs_);
		timestamp_ = time(NULL);
	}

	DirEntry(const DirEntry &de)
	    : path_(de.path_), fh(de.fh), attrs_(de.attrs_), parent(de.parent),
	      has_listdir_(de.has_listdir_), children(de.children)
	{
	}

	bool hasDirListed() const {
		std::lock_guard<std::mutex> lock(mu_);
		return has_listdir_;
	}

	void setDirListed(bool listed) {
		std::lock_guard<std::mutex> lock(mu_);
		has_listdir_ = listed;
	}

	void addChild(const std::string &p, SharedPtr<DirEntry> child)
	{
		std::lock_guard<std::mutex> lock(mu_);
		children[p] = child;
	}

	struct vattrs* getAttrs(struct vattrs *va) const
	{
		std::lock_guard<std::mutex> lock(mu_);
		vstat2attrs(&attrs_, va);
		return va;
	}

	time_t getTimestamp() const
	{
		std::lock_guard<std::mutex> lock(mu_);
		return timestamp_;
	}

	void setTimestamp(time_t tm)
	{
		std::lock_guard<std::mutex> lock(mu_);
		timestamp_ = tm;
	}

	// Return false if "validate" failed.
	bool refreshAttrs(const struct vattrs *va, bool validate)
	{
		std::lock_guard<std::mutex> lock(mu_);
		if (validate && !matchTime(&attrs_.st_ctim, &va->ctime)) {
			return false;
		}
		vattrs2stat(va, &attrs_);
		timestamp_ = time(NULL);
		return true;
	}

	size_t getFileSize() const
	{
		std::lock_guard<std::mutex> lock(mu_);
		return attrs_.st_size;
	}

	std::string path() const
	{
		std::lock_guard<std::mutex> lock(mu_);
		return path_;
	}

	/**
	 * Validate memtadata cache by comparing the timestamp of cached entry
	 * with the latest timestamp from the server side.
	 * Return whether the metadata cache entry is valid.
	 */
	bool validate(const struct timespec *ts) const
	{
		std::lock_guard<std::mutex> lock(mu_);
		return matchTime(&attrs_.st_ctim, ts);
	}

	void setAttrsAndParent(const struct vattrs *va, SharedPtr<DirEntry> pa)
	{
		std::lock_guard<std::mutex> lock(mu_);
		vattrs2stat(va, &attrs_);
		parent = pa;
	}

	// The caller owns the return value.
	struct file_handle *copyFileHandle() const
	{
		struct file_handle *h = nullptr;
		std::lock_guard<std::mutex> lock(mu_);
		if (fh) {
			h = copyFH(fh);
		}
		return h;
	}

	~DirEntry() {
#ifdef _DEBUG
		cout << "DirEntry: destructor \n";
#endif
		if (fh) {
			free(fh);
		}
	}
};

template <
	class TKey,
	class TValue,
	class TMutex = FastMutex,
	class TEventMutex = FastMutex
>
class TC_MetaDataCache: public AbstractCache<TKey, TValue, StrategyCollection<TKey, TValue>, TMutex, TEventMutex>
	/// An TC_MetaDataCache combines LRU caching and time based expire caching.
	/// It cache entries for a fixed time period (per default 10 minutes)
	/// but also limits the size of the cache (per default: 1024).
{
public:
	TC_MetaDataCache(long cacheSize = 1024, Timestamp::TimeDiff expire = 600000):
		AbstractCache<TKey, TValue, StrategyCollection<TKey, TValue>, TMutex, TEventMutex>(StrategyCollection<TKey, TValue>())
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
