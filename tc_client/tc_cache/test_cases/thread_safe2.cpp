#include <iostream>
#include <thread>
#include "../TC_MetaDataCache.h"

using namespace std;

TC_MetaDataCache<string, SharedPtr<DirEntry>> mdCache(2, 60);

void action(int n) {
	switch(n) {
		case 0 :
		{
			cout << "action[0]: adding /tmp/1\n";
			mdCache.add("/tmp/1", new DirEntry("/tmp/1"));
			cout << "action[0]: added /tmp/1\n";
		}
			break;
		case 1 :
		{
			cout << "action[1]: adding /tmp/2\n";
			mdCache.add("/tmp/2", new DirEntry("/tmp/2"));
			cout << "action[1]: added /tmp/2\n";
		}
			break;
		case 2 :
		{
		        cout << "action[2]: adding /tmp/3\n";
			mdCache.add("/tmp/3", new DirEntry("/tmp/3"));
		        cout << "action[2]: added /tmp/3\n";
		}
			break;
		case 3 :
		{
			cout << "action[3]: trying to get /tmp/1\n";
			SharedPtr<DirEntry> *de = mdCache.get("/tmp/1");
		        if (de && *de) {
                		cout << "action[3]: cache has " << (*de)->path << endl;
			} else {
				cout << "action[3]: cache doesn't have /tmp/1\n";
			}
		}
			break;
		case 4 :
		{
			cout << "action[4]: trying to get /tmp/2\n";
			SharedPtr<DirEntry> *de = mdCache.get("/tmp/2");
		        if (de && *de) {
                		cout << "action[4]: cache has " << (*de)->path << endl;
			} else {
				cout << "action[4]: cache doesn't have /tmp/2\n";
			}
		}
			break;
		case 5 :
		{
			cout << "action[5]: trying to get /tmp/3\n";
			SharedPtr<DirEntry> *de = mdCache.get("/tmp/3");
		        if (de && *de) {
                		cout << "action[5]: cache has " << (*de)->path << endl;
			} else {
				cout << "action[5]: cache doesn't have /tmp/3\n";
			}
		}
			break;
	}
}

int main() {
	int i;
	thread myThreads[6];

	cout << "main: created mdCache(2, 60)\n";
	cout << "main: creating 6 threads. first 3 tries to add item, other 3 tries to access those items.\n";

        for (i = 0; i < 6; i++) {
                myThreads[i] = thread(action, i);
        }

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
