#include <xenium/reclamation/hazard_pointer.hpp>
#include <xenium/reclamation/hazard_eras.hpp>
#include <xenium/reclamation/epoch_based.hpp>
#include <xenium/reclamation/new_epoch_based.hpp>
#include <xenium/reclamation/quiescent_state_based.hpp>
#include <xenium/reclamation/debra.hpp>
#include <xenium/reclamation/generic_epoch_based.hpp>
#include <xenium/reclamation/stamp_it.hpp>
#include <xenium/vyukov_hash_map.hpp>

#include <gtest/gtest.h>

#include <vector>
#include <thread>

namespace {

template <typename Reclaimer>
struct VyukovHashMap : ::testing::Test
{
  using hash_map = xenium::vyukov_hash_map<int, int,
    xenium::policy::reclaimer<Reclaimer>>;
  hash_map map{8};
};

using Reclaimers = ::testing::Types<
    xenium::reclamation::hazard_pointer<xenium::reclamation::static_hazard_pointer_policy<3>>,
    xenium::reclamation::hazard_eras<xenium::reclamation::static_hazard_eras_policy<3>>,
    xenium::reclamation::epoch_based<10>,
    xenium::reclamation::new_epoch_based<10>,
    xenium::reclamation::quiescent_state_based,
    xenium::reclamation::debra<20>,
    xenium::reclamation::stamp_it,
    xenium::reclamation::epoch_based2<>,
    xenium::reclamation::new_epoch_based2<>,
    xenium::reclamation::debra2<>
  >;
TYPED_TEST_CASE(VyukovHashMap, Reclaimers);

TYPED_TEST(VyukovHashMap, emplace_returns_true_for_successful_insert)
{
  EXPECT_TRUE(this->map.emplace(42, 42));
}

TYPED_TEST(VyukovHashMap, emplace_returns_false_for_failed_insert)
{
  this->map.emplace(42, 42);
  EXPECT_FALSE(this->map.emplace(42, 43));
}

TYPED_TEST(VyukovHashMap, try_get_value_returns_false_key_is_not_found)
{
  int v;
  EXPECT_FALSE(this->map.try_get_value(42, v));
}

TYPED_TEST(VyukovHashMap, try_get_value_returns_true_and_sets_result_if_matching_entry_exists)
{
  this->map.emplace(42, 43);
  int v;
  EXPECT_TRUE(this->map.try_get_value(42, v));
  EXPECT_EQ(v, 43);
}

TYPED_TEST(VyukovHashMap, erase_nonexisting_element_returns_false)
{
  EXPECT_FALSE(this->map.erase(42));
}

TYPED_TEST(VyukovHashMap, erase_existing_element_returns_true_and_removes_element)
{
  this->map.emplace(42, 43);
  EXPECT_TRUE(this->map.erase(42));
  EXPECT_FALSE(this->map.erase(42));
}

TYPED_TEST(VyukovHashMap, map_grows_if_needed)
{
  for (int i = 0; i < 10000; ++i)
    EXPECT_TRUE(this->map.emplace(i, i));
}

/*

TYPED_TEST(VyukovHashMap, containts_returns_false_for_non_existing_element)
{
  EXPECT_FALSE(this->map.contains(43));
}

TYPED_TEST(VyukovHashMap, contains_returns_true_for_existing_element)
{
  this->map.emplace(42, 43);
  EXPECT_TRUE(this->map.contains(42));
}
*/
namespace
{
#ifdef DEBUG
  const int MaxIterations = 4000;
#else
  const int MaxIterations = 40000;
#endif
}

TYPED_TEST(VyukovHashMap, parallel_usage)
{
  using Reclaimer = TypeParam;

  using hash_map = xenium::vyukov_hash_map<int, int,
    xenium::policy::reclaimer<Reclaimer>>;
  hash_map map(8);

  static constexpr int keys_per_thread = 8;

  std::vector<std::thread> threads;
  for (int i = 0; i < 8; ++i)
  {
    threads.push_back(std::thread([i, &map]
    {
      for (int k = i * keys_per_thread; k < (i + 1) * keys_per_thread; ++k) {
        for (int j = 0; j < MaxIterations / keys_per_thread; ++j) {
          typename Reclaimer::region_guard critical_region{};
          EXPECT_TRUE(map.emplace(k, k));
          for (int x = 0; x < 10; ++x) {
            int v = 0;
            EXPECT_TRUE(map.try_get_value(k, v));
            EXPECT_EQ(v, k);
          }
          EXPECT_TRUE(map.erase(k));
        }
      }
    }));
  }

  for (auto& thread : threads)
    thread.join();
}

TYPED_TEST(VyukovHashMap, parallel_usage_with_same_values)
{
  using Reclaimer = TypeParam;

  using hash_map = xenium::vyukov_hash_map<int, int,
    xenium::policy::reclaimer<Reclaimer>>;
  hash_map map(8);

  std::vector<std::thread> threads;
  for (int i = 0; i < 8; ++i)
  {
    threads.push_back(std::thread([&map]
    {
      for (int j = 0; j < MaxIterations / 10; ++j)
        for (int i = 0; i < 10; ++i)
        {
          int k = i;
          typename Reclaimer::region_guard critical_region{};
          map.emplace(k, i);
          int v = 0;
          if (map.try_get_value(k, v))
            EXPECT_EQ(v, k);
          map.erase(k);
        }
    }));
  }

  for (auto& thread : threads)
    thread.join();
}

}