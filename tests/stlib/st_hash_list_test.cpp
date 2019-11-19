#include "stlib/st_hash_list.h"
#include "stlib/st_test.h"

using namespace stlib;

typedef StHashList<> HT;

TEST(StStatus, hash_list)
{
	StHashList<> *h = new StHashList<>();

	LOG_TRACE("size : %d", h->HashSize());

	// 使用内存池
	UtilPtrPool<HT> *u = new UtilPtrPool<HT>(1);
	HT *h1 = u->AllocPtr();
	HT *h2 = u->AllocPtr();
	HT *h3 = u->AllocPtr();

	LOG_TRACE("UtilPtrPool : %d", u->Size());

	u->FreePtr(h1);

	LOG_TRACE("UtilPtrPool : %d", u->Size());

	u->FreePtr(h2);

	LOG_TRACE("UtilPtrPool : %d", u->Size());

	u->FreePtr(h3);

	LOG_TRACE("UtilPtrPool : %d", u->Size());
}

// 测试所有的功能
int main(int argc, char* argv[])
{
    return RUN_ALL_TESTS();
}