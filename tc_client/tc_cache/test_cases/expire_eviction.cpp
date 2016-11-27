#include <iostream>
#include <unistd.h>
#include "../TC_MetaDataCache.h"

using namespace std;

int main() {
	TC_MetaDataCache<string, SharedPtr<DirEntry>> mdCache(2, 60);
	cout << "main: created mdCache(2, 60)\n";

	cout << "main: adding first element /tmp/1\n";
	mdCache.add("/tmp/1", new DirEntry("/tmp/1"));
	cout << "main: added /tmp/1\n";

	cout << "main: sleeping for 30 ms\n";
	usleep(30000);
	cout << "main: woke up from 30 ms sleep\n";

	cout << "main: adding second element /tmp/2\n";
	mdCache.add("/tmp/2", new DirEntry("/tmp/2"));
	cout << "main: added /tmp/2\n";

	cout << "main: sleeping for 30 ms\n";
	usleep(30000);
	cout << "main: woke up from 30 ms sleep\n";

	cout << "main: getting all keys\n";
	set<string> keys = mdCache.getAllKeys();
	set<string>::iterator it;
	for (it = keys.begin(); it != keys.end(); ++it) {
		cout << "main: " << *it << endl;
	}

	return 0;
}
