
TYPED_TEST_P(TcTest, UUIDOpenExclFlagCheck)
{
	const int N = 4;
	const char *PATHS[] = { "PRE-1-open-excl.txt",
	                        "PRE-2-open-excl.txt",
	                        "PRE-3-open-excl.txt",
	                        "PRE-4-open-excl.txt" };
	vfile *files;

	Removev(PATHS, 4);

	files = vec_open_simple(PATHS, N, O_EXCL | O_CREAT, 0);
	EXPECT_NOTNULL(files);
	vec_close(files, N);
}

TYPED_TEST_P(TcTest, UUIDExclFlagCheck)
{
	const int N = 4;
	const char *PATHS[] = { "TcTest-1-open.txt",
	                        "TcTest-2-open.txt",
	                        "TcTest-3-open.txt",
	                        "TcTest-4-open.txt" };
	vfile *files;

	Removev(PATHS, 4);

	files = vec_open_simple(PATHS, N, O_EXCL, 0);
	ASSERT_TRUE(files == NULL);
	vec_close(files, N);
}

TYPED_TEST_P(TcTest, UUIDOpenFlagCheck)
{
	const int N = 4;
	const char *PATHS[] = { "PRE-1-open.txt",
	                        "PRE-2-open.txt",
	                        "PRE-3-open.txt",
	                        "PRE-4-open.txt" };
	vfile *files;

	Removev(PATHS, 4);

	files = vec_open_simple(PATHS, N, O_CREAT, 0);
	EXPECT_NOTNULL(files);
	vec_close(files, N);
}

TYPED_TEST_P(TcTest, UUIDReadFlagCheck)
{
	const int N = 4;
	const char *PATHS[] = { "TcTest-TestFileDesc1.txt",
	                        "TcTest-TestFileDesc2.txt",
	                        "TcTest-TestFileDesc3.txt",
	                        "TcTest-TestFileDesc4.txt" };
	vfile *files;

	Removev(PATHS, 4);

	files = vec_open_simple(PATHS, N, 0, 0);
	ASSERT_TRUE(files == NULL);
	vec_close(files, N);

}

