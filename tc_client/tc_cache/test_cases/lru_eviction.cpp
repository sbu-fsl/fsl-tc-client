#include <iostream>
#include "../TC_MetaDataCache.h"

using namespace std;

/*
 * This definition is defined in TC_AbstractCache.h
struct dirEntry {
	string path;
	void *fh;
	struct stat *attrs;
	struct dirEntry *parent;
	unordered_map<string, dirEntry*> childrens;
};
*/

bool on_remove_metadata(dirEntry *de) {
	if (de) {
		cout << "on_remove_metadata: " << de->path << "\n";
		delete de;
	} else {
		cout << "on_remove_metadata: dirEntry is NULL\n";
	}
	return true;
}

int main() {
	TC_MetaDataCache<string, dirEntry*> mdCache(2, 60, on_remove_metadata);
	cout << "main: created mdCache(2, 60, on_remove_metadata)\n";

	dirEntry *de1 = new dirEntry();
	de1->path = "/tmp/1";
	cout << "main: adding first element /tmp/1\n";
	mdCache.add(de1->path, de1);
	cout << "main: added /tmp/1\n";

	dirEntry *de2 = new dirEntry();
	de2->path = "/tmp/2";	
	cout << "main: adding second element /tmp/2\n";
	mdCache.add(de2->path, de2);
	cout << "main: added /tmp/2\n";

	cout << "main: doing get for first element /tmp/1\n";
	mdCache.get("/tmp/1");

	dirEntry *de3 = new dirEntry();
	de3->path = "/tmp/3";	
	cout << "main: adding third element /tmp/3\n";
	mdCache.add(de3->path, de3);
	cout << "main: added /tmp/3\n";

	cout << "main: getting all keys\n";
	set<string> keys = mdCache.getAllKeys();
	set<string>::iterator it;
	for (it = keys.begin(); it != keys.end(); ++it) {
		cout << "main: " << *it << endl;
	}

	return 0;
}
