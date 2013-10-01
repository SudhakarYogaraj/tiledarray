/*
 *  This file is a part of TiledArray.
 *  Copyright (C) 2013  Virginia Tech
 *
 *  This program is free software: you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation, either version 3 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program.  If not, see <http://www.gnu.org/licenses/>.
 *
 *  Justus Calvin
 *  Department of Chemistry, Virginia Tech
 *
 *  unary_eval.cpp
 *  August 8, 2013
 *
 */

#include "TiledArray/dist_eval/unary_eval.h"
#include "unit_test_config.h"
#include "TiledArray/array.h"
#include "TiledArray/dist_eval/array_eval.h"
#include "TiledArray/dense_shape.h"
#include "array_fixture.h"

using namespace TiledArray;
using namespace TiledArray::expressions;

// Array evaluator fixture
struct UnaryEvalImplFixture : public TiledRangeFixture {
  typedef Array<int, GlobalFixture::dim> ArrayN;
  typedef math::Noop<ArrayN::value_type::eval_type,
      ArrayN::value_type::eval_type, true> array_op_type;
  typedef detail::ArrayEvalImpl<ArrayN, array_op_type, DensePolicy> array_eval_impl_type;
  typedef detail::DistEval<array_eval_impl_type::value_type, DensePolicy> dist_eval_type;
  typedef math::Scal<ArrayN::value_type::eval_type, ArrayN::value_type::eval_type,
      true> op_type;
  typedef detail::UnaryEvalImpl<dist_eval_type, op_type, DensePolicy> impl_type;


  UnaryEvalImplFixture() :
    array(*GlobalFixture::world, tr),
    arg(std::shared_ptr<array_eval_impl_type::DistEvalImpl_>(
        new array_eval_impl_type(array, DenseShape(), array.get_pmap(), Permutation(), array_op_type())))
  {
    // Fill array with random data
    for(ArrayN::range_type::const_iterator it = array.range().begin(); it != array.range().end(); ++it)
      if(array.is_local(*it)) {
        ArrayN::value_type tile(array.trange().make_tile_range(*it));
        for(ArrayN::value_type::iterator tile_it = tile.begin(); tile_it != tile.end(); ++tile_it)
          *tile_it = GlobalFixture::world->rand() % 101;
        array.set(*it, tile); // Fill the tile at *it (the index)
      }
  }

  ~UnaryEvalImplFixture() { }

   ArrayN array;
   dist_eval_type arg;
}; // ArrayEvalFixture

BOOST_FIXTURE_TEST_SUITE( unary_eval_suite, UnaryEvalImplFixture )

BOOST_AUTO_TEST_CASE( constructor )
{
  BOOST_REQUIRE_NO_THROW(impl_type(arg, DenseShape(), arg.pmap(), Permutation(), op_type(3)));

  typedef detail::DistEval<dist_eval_type::eval_type, DensePolicy> dist_eval_type1;

  dist_eval_type1 unary(
      std::shared_ptr<typename impl_type::DistEvalImpl_>(
          new impl_type(arg, DenseShape(), arg.pmap(), Permutation(), op_type(3))));

  BOOST_CHECK_EQUAL(& unary.get_world(), GlobalFixture::world);
  BOOST_CHECK(unary.pmap() == arg.pmap());
  BOOST_CHECK_EQUAL(unary.range(), tr.tiles());
  BOOST_CHECK_EQUAL(unary.trange(), tr);
  BOOST_CHECK_EQUAL(unary.size(), tr.tiles().volume());
  BOOST_CHECK(unary.is_dense());
  for(std::size_t i = 0; i < tr.tiles().volume(); ++i)
    BOOST_CHECK(! unary.is_zero(i));

  typedef detail::DistEval<dist_eval_type1::eval_type, DensePolicy> dist_eval_type2;
  typedef detail::UnaryEvalImpl<dist_eval_type2, op_type, DensePolicy> impl_type2;


  BOOST_REQUIRE_NO_THROW(impl_type2(unary, DenseShape(), arg.pmap(), Permutation(), op_type(5)));

  dist_eval_type2 unary2(
      std::shared_ptr<typename dist_eval_type2::impl_type>(
          new impl_type2(unary, DenseShape(), unary.pmap(), Permutation(), op_type(5))));


  BOOST_CHECK_EQUAL(& unary2.get_world(), GlobalFixture::world);
  BOOST_CHECK(unary2.pmap() == arg.pmap());
  BOOST_CHECK_EQUAL(unary2.range(), tr.tiles());
  BOOST_CHECK_EQUAL(unary2.trange(), tr);
  BOOST_CHECK_EQUAL(unary2.size(), tr.tiles().volume());
  BOOST_CHECK(unary2.is_dense());
  for(std::size_t i = 0; i < tr.tiles().volume(); ++i)
    BOOST_CHECK(! unary2.is_zero(i));
}

BOOST_AUTO_TEST_SUITE_END()
