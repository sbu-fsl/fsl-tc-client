#include <iostream>
#include <thread>
#include <unistd.h>
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

TC_MetaDataCache<string, dirEntry*> mdCache(1, 6000, on_remove_metadata);

void action0(void) {
	dirEntry *de1 = new dirEntry();
	de1->path = "/tmp/1";
	cout << "action[0]: adding /tmp/1\n";
	mdCache.add(de1->path, de1);
	cout << "action[0]: added /tmp/1\n";
}

void action1(void) {
	cout << "action[1]: trying to get /tmp/1\n";
        Poco::SharedPtr<dirEntry*> ptrElem = mdCache.get("/tmp/1");
	if (ptrElem) {
		cout << "action[1]: cache has /tmp/1\n";
		cout << "action[1]: ptrElem ref count before sleep = " << ptrElem.referenceCount() << endl;
		cout << "action[1]: path before sleep: " << (*ptrElem)->path << endl;
		sleep(2);
		cout << "action[1]: ptrElem ref count after sleep = " << ptrElem.referenceCount() << endl;
		cout << "action[1]: path after sleep: " << (*ptrElem)->path << endl;
	} else {
		cout << "action[1]: cache doesn't have /tmp/1\n";
	}
}

void action2(void) {
	dirEntry *de2 = new dirEntry();
	de2->path = "/tmp/2";	
	cout << "action[2]: adding /tmp/2\n";
	mdCache.add(de2->path, de2);
	cout << "action[2]: added /tmp/2\n";
}


int main() {
	int i;
	thread myThreads[3];

	cout << "main: created mdCache(1, 6000, on_remove_metadata)\n";

	myThreads[0] = thread(action0);	// adds and item
	sleep(1);
	myThreads[1] = thread(action1); // get existing item, this will increase its ref count
	sleep(1);
	myThreads[2] = thread(action2);	// add another item which would result in eviction of previously added item

        for (i = 0; i < 3; i++) {
                myThreads[i].join();
        }

	cout << "main: getting all keys\n";
	set<string> keys = mdCache.getAllKeys();
	set<string>::iterator it;
	for (it = keys.begin(); it != keys.end(); ++it) {
		cout << "main: " << *it << endl;
	}

	return 0;
}
