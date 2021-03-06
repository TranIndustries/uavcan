/*
 * Copyright (C) 2014 Pavel Kirienko <pavel.kirienko@gmail.com>
 */

#include <string>
#include <cstdio>
#include <memory>
#include <gtest/gtest.h>
#include <uavcan/util/map.hpp>


static std::string toString(long x)
{
    char buf[80];
    std::snprintf(buf, sizeof(buf), "%li", x);
    return std::string(buf);
}

static bool oddValuePredicate(const std::string& key, const std::string& value)
{
    EXPECT_FALSE(key.empty());
    EXPECT_FALSE(value.empty());
    const int num = atoi(value.c_str());
    return num & 1;
}

struct KeyFindPredicate
{
    const std::string target;
    KeyFindPredicate(std::string target) : target(target) { }
    bool operator()(const std::string& key, const std::string&) const { return key == target; }
};

struct ValueFindPredicate
{
    const std::string target;
    ValueFindPredicate(std::string target) : target(target) { }
    bool operator()(const std::string&, const std::string& value) const { return value == target; }
};


TEST(Map, Basic)
{
    using uavcan::Map;

    static const int POOL_BLOCKS = 3;
    uavcan::PoolAllocator<uavcan::MemPoolBlockSize * POOL_BLOCKS, uavcan::MemPoolBlockSize> pool;
    uavcan::PoolManager<2> poolmgr;
    poolmgr.addPool(&pool);

    typedef Map<std::string, std::string, 2> MapType;
    std::auto_ptr<MapType> map(new MapType(poolmgr));

    // Empty
    ASSERT_FALSE(map->access("hi"));
    map->remove("foo");
    ASSERT_EQ(0, pool.getNumUsedBlocks());

    // Static insertion
    ASSERT_EQ("a", *map->insert("1", "a"));
    ASSERT_EQ("b", *map->insert("2", "b"));
    ASSERT_EQ(0, pool.getNumUsedBlocks());
    ASSERT_EQ(2, map->getNumStaticPairs());
    ASSERT_EQ(0, map->getNumDynamicPairs());

    // Dynamic insertion
    ASSERT_EQ("c", *map->insert("3", "c"));
    ASSERT_EQ(1, pool.getNumUsedBlocks());

    ASSERT_EQ("d", *map->insert("4", "d"));
    ASSERT_EQ(1, pool.getNumUsedBlocks());       // Assuming that at least 2 items fit one block
    ASSERT_EQ(2, map->getNumStaticPairs());
    ASSERT_EQ(2, map->getNumDynamicPairs());

    // Making sure everything is here
    ASSERT_EQ("a", *map->access("1"));
    ASSERT_EQ("b", *map->access("2"));
    ASSERT_EQ("c", *map->access("3"));
    ASSERT_EQ("d", *map->access("4"));
    ASSERT_FALSE(map->access("hi"));

    // Modifying existing entries
    *map->access("1") = "A";
    *map->access("2") = "B";
    *map->access("3") = "C";
    *map->access("4") = "D";
    ASSERT_EQ("A", *map->access("1"));
    ASSERT_EQ("B", *map->access("2"));
    ASSERT_EQ("C", *map->access("3"));
    ASSERT_EQ("D", *map->access("4"));

    // Finding some keys
    ASSERT_EQ("1", *map->findFirstKey(KeyFindPredicate("1")));
    ASSERT_EQ("2", *map->findFirstKey(KeyFindPredicate("2")));
    ASSERT_EQ("3", *map->findFirstKey(KeyFindPredicate("3")));
    ASSERT_EQ("4", *map->findFirstKey(KeyFindPredicate("4")));
    ASSERT_FALSE(map->findFirstKey(KeyFindPredicate("nonexistent_key")));

    // Finding some values
    ASSERT_EQ("1", *map->findFirstKey(ValueFindPredicate("A")));
    ASSERT_EQ("2", *map->findFirstKey(ValueFindPredicate("B")));
    ASSERT_EQ("3", *map->findFirstKey(ValueFindPredicate("C")));
    ASSERT_EQ("4", *map->findFirstKey(ValueFindPredicate("D")));
    ASSERT_FALSE(map->findFirstKey(KeyFindPredicate("nonexistent_value")));

    // Removing one static
    map->remove("1");                             // One of dynamics now migrates to the static storage
    map->remove("foo");                           // There's no such thing anyway
    ASSERT_EQ(1, pool.getNumUsedBlocks());
    ASSERT_EQ(2, map->getNumStaticPairs());
    ASSERT_EQ(1, map->getNumDynamicPairs());

    ASSERT_FALSE(map->access("1"));
    ASSERT_EQ("B", *map->access("2"));
    ASSERT_EQ("C", *map->access("3"));
    ASSERT_EQ("D", *map->access("4"));

    // Removing another static
    map->remove("2");
    ASSERT_EQ(2, map->getNumStaticPairs());
    ASSERT_EQ(0, map->getNumDynamicPairs());
    ASSERT_EQ(0, pool.getNumUsedBlocks());       // No dynamic entries left

    ASSERT_FALSE(map->access("1"));
    ASSERT_FALSE(map->access("2"));
    ASSERT_EQ("C", *map->access("3"));
    ASSERT_EQ("D", *map->access("4"));

    // Adding some new dynamics
    unsigned max_key_integer = 0;
    for (int i = 0; i < 100; i++)
    {
        const std::string key   = toString(i);
        const std::string value = toString(i);
        std::string* res = map->insert(key, value);  // Will override some from the above
        if (res == NULL)
        {
            ASSERT_LT(2, i);
            break;
        }
        else
        {
            ASSERT_EQ(value, *res);
        }
        max_key_integer = unsigned(i);
    }
    std::cout << "Max key/value: " << max_key_integer << std::endl;
    ASSERT_LT(4, max_key_integer);

    // Making sure there is true OOM
    ASSERT_EQ(0, pool.getNumFreeBlocks());
    ASSERT_FALSE(map->insert("nonexistent", "value"));
    ASSERT_FALSE(map->access("nonexistent"));
    ASSERT_FALSE(map->access("value"));

    // Removing odd values - nearly half of them
    ASSERT_EQ(2, map->getNumStaticPairs());
    const unsigned num_dynamics_old = map->getNumDynamicPairs();
    map->removeWhere(oddValuePredicate);
    ASSERT_EQ(2, map->getNumStaticPairs());
    const unsigned num_dynamics_new = map->getNumDynamicPairs();
    std::cout << "Num of dynamic pairs reduced from " << num_dynamics_old << " to " << num_dynamics_new << std::endl;
    ASSERT_LT(num_dynamics_new, num_dynamics_old);

    // Making sure there's no odd values left
    for (unsigned kv_int = 0; kv_int <= max_key_integer; kv_int++)
    {
        const std::string* val = map->access(toString(kv_int));
        if (val)
        {
            ASSERT_FALSE(kv_int & 1);
        }
        else
        {
            ASSERT_TRUE(kv_int & 1);
        }
    }

    // Making sure the memory will be released
    map.reset();
    ASSERT_EQ(0, pool.getNumUsedBlocks());
}


TEST(Map, NoStatic)
{
    using uavcan::Map;

    static const int POOL_BLOCKS = 3;
    uavcan::PoolAllocator<uavcan::MemPoolBlockSize * POOL_BLOCKS, uavcan::MemPoolBlockSize> pool;
    uavcan::PoolManager<2> poolmgr;
    poolmgr.addPool(&pool);

    typedef Map<std::string, std::string> MapType;
    std::auto_ptr<MapType> map(new MapType(poolmgr));

    // Empty
    ASSERT_FALSE(map->access("hi"));
    map->remove("foo");
    ASSERT_EQ(0, pool.getNumUsedBlocks());

    // Static insertion
    ASSERT_EQ("a", *map->insert("1", "a"));
    ASSERT_EQ("b", *map->insert("2", "b"));
    ASSERT_EQ(1, pool.getNumUsedBlocks());
    ASSERT_EQ(0, map->getNumStaticPairs());
    ASSERT_EQ(2, map->getNumDynamicPairs());
}
