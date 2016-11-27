#include <iostream>
#include "../TC_MetaDataCache.h"

using namespace std;

bool on_remove_metadata(SharedPtr<DirEntry> de) {
	cout << "on_remove_metadata: is deprecated \n";
	return true;
}

int main() {
	TC_MetaDataCache<string, SharedPtr<DirEntry>> mdCache(2, 60, on_remove_metadata);
	cout << "main: created mdCache(2, 60, on_remove_metadata)\n";

	SharedPtr<DirEntry> de1(new DirEntry("/tmp/1"));
	// SharedPtr<DirEntry> temp(de1); - this is the way to create a copy
	cout << "main: ref count for de1: " << de1.referenceCount() << endl;
	cout << "main: adding first element /tmp/1\n";
	mdCache.add("/tmp/1", de1);
	cout << "main: added /tmp/1\n";
	cout << "main: ref count for de1: " << de1.referenceCount() << endl;

	SharedPtr<DirEntry> de2(new DirEntry("/tmp/2"));
	cout << "main: adding second element /tmp/2\n";
	mdCache.add("/tmp/2", de2);
	cout << "main: added /tmp/2\n";
	cout << "main: ref count for de2: " << de2.referenceCount() << endl;

	cout << "main: doing get for first element /tmp/1\n";
	SharedPtr<DirEntry> *de = mdCache.get("/tmp/1");
	cout << "main: ref count for de1 after get: " << de1.referenceCount() << endl;
	cout << "main: derefencing path from de1: " << (*de)->path << endl;

	SharedPtr<DirEntry> de3(new DirEntry("/tmp/3"));
	cout << "main: adding third element /tmp/3\n";
	mdCache.add("/tmp/3", de3);
	cout << "main: added /tmp/3\n";
	cout << "main: ref count for de3: " << de3.referenceCount() << endl;

	cout << "main: getting all keys\n";
	set<string> keys = mdCache.getAllKeys();
	set<string>::iterator it;
	for (it = keys.begin(); it != keys.end(); ++it) {
		cout << "main: " << *it << endl;
	}

	cout << "main: clearing cache\n";
	mdCache.clear();
	cout << "main: cache cleared\n";
	
	cout << "main: getting all keys after clearing cache\n";
	keys = mdCache.getAllKeys();
	for (it = keys.begin(); it != keys.end(); ++it) {
		cout << "main: " << *it << endl;
	}
	
	cout << "main: Ends here\n";
	return 0;
}
