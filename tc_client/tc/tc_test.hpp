#include <cstring>

#include "log.h"
#include "path_utils.h"
#include "tc_api.h"
#include "tc_helper.h"
#include "test_util.h"
#include "util/fileutil.h"

#define new_auto_path(fmt, args...)                                            \
	tc_format_path((char *)alloca(PATH_MAX), fmt, ##args)

/**
 * Ensure files or directories do not exist before test.
 */
static inline bool Removev(const char **paths, int count)
{
	return vokay(vec_unlink(paths, count));
}

static inline char *tc_format_path(char *path, const char *format, ...)
{
	va_list args;

	va_start(args, format);
	vsnprintf(path, PATH_MAX, format, args);
	va_end(args);

	return path;
}

static inline void vensure_parent_dir(const char *path)
{
	char dirpath[PATH_MAX];
	slice_t dir = tc_path_dirname(path);
	strncpy(dirpath, dir.data, dir.size);
	dirpath[dir.size] = '\0';
	sca_ensure_dir(dirpath, 0755, NULL);
}

class TcPosixImpl
{
      public:
	static void *tcdata;
	static constexpr const char *POSIX_TEST_DIR = "/tmp/tc_posix_test";
	static void SetUpTestCase()
	{
		tcdata = vinit(NULL, "/tmp/tc-posix.log", 0);
		TCTEST_WARN("Global SetUp of Posix Impl\n");
		util::CreateOrUseDir(POSIX_TEST_DIR);
		if (chdir(POSIX_TEST_DIR) < 0) {
			TCTEST_ERR("chdir failed\n");
		}
	}
	static void TearDownTestCase()
	{
		TCTEST_WARN("Global TearDown of Posix Impl\n");
		vdeinit(tcdata);
		// sleep(120);
	}
	static void SetUp() { TCTEST_WARN("SetUp Posix Impl Test\n"); }
	static void TearDown() { TCTEST_WARN("TearDown Posix Impl Test\n"); }
};

void *TcPosixImpl::tcdata = nullptr;

class TcNFS4Impl
{
      public:
	static void *tcdata;
	static void SetUpTestCase()
	{
		tcdata = vinit(
		    get_tc_config_file((char *)alloca(PATH_MAX), PATH_MAX),
		    "/tmp/tc-nfs4.log", 77);
		TCTEST_WARN("Global SetUp of NFS4 Impl\n");
		/* TODO: recreate test dir if exist */
		EXPECT_OK(sca_ensure_dir("/vfs0/tc_nfs4_test", 0755, NULL));
		sca_chdir("/vfs0/tc_nfs4_test"); /* change to mnt point */
	}
	static void TearDownTestCase()
	{
		TCTEST_WARN("Global TearDown of NFS4 Impl\n");
		vdeinit(tcdata);
	}
	static void SetUp() { TCTEST_WARN("SetUp NFS4 Impl Test\n"); }
	static void TearDown() { TCTEST_WARN("TearDown NFS4 Impl Test\n"); }
};

void *TcNFS4Impl::tcdata = nullptr;

template <typename T> class TcTest : public ::testing::Test
{
      public:
	static void SetUpTestCase() { T::SetUpTestCase(); }
	static void TearDownTestCase() { T::TearDownTestCase(); }
	void SetUp() override { T::SetUp(); }
	void TearDown() override { T::TearDown(); }
};

/* We still need an individual test fixture class for TcTxnTest. This is because
 * we want to store a special var `posix_base` intended for the base path of
 * TC-server's local root. It's not a good idea to test states using NFS api
 * because the metadata cache may not have been flushed after rollback. */
template <typename T> class TcTxnTest : public TcTest<T>
{
      public:
	std::string posix_base;
	std::string nfs_base;

	static void SetUpTestCase() { TcTest<T>::SetUpTestCase(); }

	static void TearDownTestCase() { TcTest<T>::TearDownTestCase(); }

	void SetUp() override
	{
		TcTest<T>::SetUp();
		posix_base = "/tcserver/tc_nfs4_test/";
		nfs_base = "/vfs0/tc_nfs4_test/";
	}

	void TearDown() override { TcTest<T>::TearDown(); }
};

template <typename T> using TcLockTest = TcTxnTest<T>;
