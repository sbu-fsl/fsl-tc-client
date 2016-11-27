#include <iostream>
#include <thread>
#include <unistd.h>
#include "../TC_MetaDataCache.h"

using namespace std;

TC_MetaDataCache<string, SharedPtr<DirEntry>> mdCache(1, 1000);

void action0(void) {
	SharedPtr<DirEntry> de1(new DirEntry("/tmp/1"));
	de1->attrs = new struct stat();
	de1->attrs->st_size = 1024;
	cout << "action[0]: adding /tmp/1\n";
	mdCache.add("/tmp/1", de1);
	cout << "action[0]: added /tmp/1, de1->attrs->st_size: " << de1->attrs->st_size << endl;;
}

void action1(void) {
	cout << "action[1]: trying to get /tmp/1\n";
	SharedPtr<DirEntry> *de = mdCache.get("/tmp/1");
	if (de && *de) {
		cout << "action[1]: cache has /tmp/1\n";
		cout << "action[1]: ref count after get: " << (*de).referenceCount() << endl;
		cout << "action[1]: dereferencing path: " << (*de)->path << endl;
	} else {
		cout << "action[1]: cache doesn't have /tmp/1\n";
	}
}

int main() {
	int i;
	thread myThreads[2];

	cout << "main: created mdCache(1, 1000)\n";

	myThreads[0] = thread(action0);	// adds and item
	sleep(2);
	myThreads[1] = thread(action1); // get existing item, this does not increments the ref count

        for (i = 0; i < 2; i++) {
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
