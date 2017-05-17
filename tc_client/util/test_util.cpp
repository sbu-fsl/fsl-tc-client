#include "test_util.h"

#include <sys/types.h>
#include <unistd.h>
#include <fcntl.h>

#include <list>

char *getRandomBytes(int N)
{
	int fd;
	char *buf;
	ssize_t ret;
	ssize_t n;

	buf = (char *)malloc(N);
	if (!buf) {
		return NULL;
	}

	fd = open("/dev/urandom", O_RDONLY);
	if (fd < 0) {
		free(buf);
		return NULL;
	}

	n = 0;
	while (n < N) {
		ret = read(fd, buf + n, std::min<int>(16384, N - n));
		if (ret < 0) {
			free(buf);
			close(fd);
			return NULL;
		}
		n += ret;
	}

	close(fd);
	return buf;
}

void DoParallel(int nthread, std::function<void(int)> worker)
{
	std::list<std::thread> threads;
	for (int i = 0; i < nthread; ++i) {
		threads.emplace_back(worker, i);
	}
	for (auto it = threads.begin(); it != threads.end(); ++it) {
		it->join();
	}
}
