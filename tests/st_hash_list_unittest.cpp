#include "gtest/googletest/include/gtest/gtest.h"
#include "../include/st_hash_list.h"

ST_NAMESPACE_USING

typedef HashList<> HT;

TEST(HashListTest, hash_list)
{
	HashList<> *h = new HashList<>();

	LOG_TRACE("size : %d", h->HashSize());

	// 使用内存池
	UtilsPtrPool<HT> *u = new UtilsPtrPool<HT>(1);
	HT *h1 = u->AllocPtr();
	HT *h2 = u->AllocPtr();
	HT *h3 = u->AllocPtr();

	LOG_TRACE("UtilsPtrPool : %d", u->Size());

	u->FreePtr(h1);

	LOG_TRACE("UtilsPtrPool : %d", u->Size());

	u->FreePtr(h2);

	LOG_TRACE("UtilsPtrPool : %d", u->Size());

	u->FreePtr(h3);

	LOG_TRACE("UtilsPtrPool : %d", u->Size());
}

// 测试所有的功能
int main(int argc, char* argv[])
{
    testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}