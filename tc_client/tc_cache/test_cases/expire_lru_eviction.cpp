#include <iostream>
#include <unistd.h>
#include "../TC_MetaDataCache.h"

using namespace std;

bool on_remove_metadata(SharedPtr<DirEntry> de) {
        cout << "on_remove_metadata: is deprecated \n";
        return true;
}

int main() {
	TC_MetaDataCache<string, SharedPtr<DirEntry>> mdCache(2, 60, on_remove_metadata);
	cout << "main: created mdCache(2, 60, on_remove_metadata)\n";

	cout << "main: adding first element /tmp/1\n";
	mdCache.add("/tmp/1", new DirEntry("/tmp/1"));
	cout << "main: added /tmp/1\n";

	cout << "main: adding second element /tmp/2\n";
	mdCache.add("/tmp/2", new DirEntry("/tmp/2"));
	cout << "main: added /tmp/2\n";

	cout << "main: doing get for first element /tmp/1\n";
        SharedPtr<DirEntry> *de =  mdCache.get("/tmp/1");

	cout << "main: sleeping for 60 ms\n";
	usleep(60000);
	cout << "main: woke up from 60 ms sleep\n";

        cout << "main: adding third element /tmp/3\n";
	mdCache.add("/tmp/3", new DirEntry("/tmp/3"));
        cout << "main: added /tmp/3\n";

	cout << "main: getting all keys\n";
	set<string> keys = mdCache.getAllKeys();
	set<string>::iterator it;
	for (it = keys.begin(); it != keys.end(); ++it) {
		cout << "main: " << *it << endl;
	}

	return 0;
}
