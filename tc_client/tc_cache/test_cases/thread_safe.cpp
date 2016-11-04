#include <iostream>
#include <thread>
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

TC_MetaDataCache<string, dirEntry*> mdCache(2, 60, on_remove_metadata);

void action0(void) {
	dirEntry *de1 = new dirEntry();
	de1->path = "/tmp/1";
	cout << "action[0]: adding /tmp/1\n";
	mdCache.add(de1->path, de1);
	cout << "action[0]: added /tmp/1\n";
}

void action1(void) {
	dirEntry *de2 = new dirEntry();
	de2->path = "/tmp/2";	
	cout << "action[1]: adding /tmp/2\n";
	mdCache.add(de2->path, de2);
	cout << "action[1]: added /tmp/2\n";
}

void action2(void) {
	dirEntry *de3 = new dirEntry();
        de3->path = "/tmp/3";
        cout << "action[2]: adding /tmp/3\n";
        mdCache.add(de3->path, de3);
        cout << "action[2]: added /tmp/3\n";
}

void action3(void) {
	cout << "action[3]: trying to get /tmp/1\n";
        Poco::SharedPtr<dirEntry*> ptrElem = mdCache.get("/tmp/1");
	if (ptrElem) {
		cout << "action[3]: cache has /tmp/1\n";
	} else {
		cout << "action[3]: cache doesn't have /tmp/1\n";
	}
}

void action4(void) {
	cout << "action[4]: trying to get /tmp/2\n";
        Poco::SharedPtr<dirEntry*> ptrElem = mdCache.get("/tmp/2");
	if (ptrElem) {
		cout << "action[4]: cache has /tmp/2\n";
	} else {
		cout << "action[4]: cache doesn't have /tmp/2\n";
	}
}

void action5(void) {
	cout << "action[5]: trying to get /tmp/3\n";
	Poco::SharedPtr<dirEntry*> ptrElem = mdCache.get("/tmp/3");
	if (ptrElem) {
		cout << "action[5]: cache has /tmp/3\n";
	} else {
		cout << "action[5]: cache doesn't have /tmp/3\n";
	}
}

int main() {
	int i;
	thread myThreads[6];
	// typedef void (*action_callback_func)(void);
	// action_callback_func[6] = {action0, action1, action2, action3, action4, action5};

	cout << "main: created mdCache(2, 60, on_remove_metadata)\n";

	cout << "main: creating 6 threads. first 3 tries to add item, other 3 tries to access those items.\n";
	/*
        for (i = 0; i < 6; i++) {
                myThreads[i] = thread(action_callback_func[i]);
        }
	*/
	myThreads[0] = thread(action0);
	myThreads[1] = thread(action1);
	myThreads[2] = thread(action2);
	myThreads[3] = thread(action3);
	myThreads[4] = thread(action4);
	myThreads[5] = thread(action5);

        for (i = 0; i < 6; i++) {
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
