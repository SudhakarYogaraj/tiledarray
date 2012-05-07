#ifndef TILEDARRAY_BINARY_TILED_TENSOR_H__INCLUDED
#define TILEDARRAY_BINARY_TILED_TENSOR_H__INCLUDED

#include <TiledArray/array_base.h>
#include <TiledArray/binary_tensor.h>
#include <TiledArray/unary_tensor.h>
#include <TiledArray/permute_tensor.h>
#include <TiledArray/distributed_storage.h>
#include <TiledArray/bitset.h>
#include <TiledArray/array.h>

namespace TiledArray {

  // Forward declarations

  template <typename> class StaticTiledRange;
  class DynamicTiledRange;

  namespace expressions {

    // Forward declaration
    template <typename, typename, typename>
    class BinaryTiledTensor;

    template <typename LExp, typename RExp, typename Op>
    BinaryTiledTensor<LExp, RExp, Op>
    make_binary_tiled_tensor(const ReadableTiledTensor<LExp>& left, const ReadableTiledTensor<RExp>& right, const Op& op) {
      return BinaryTiledTensor<LExp, RExp, Op>(left.derived(), right.derived(), op);
    }

    namespace {

      /// Select the tiled range type

      /// This helper class selects a tiled range for binary operations. It favors
      /// \c StaticTiledRange over \c DynamicTiledRange to avoid the dynamic memory
      /// allocations used in \c DynamicTiledRange.
      /// \tparam LRange The left tiled range type
      /// \tparam RRange The right tiled range type
      template <typename LRange, typename RRange>
      struct trange_select {
        typedef LRange type; ///< The tiled range type to use

        /// Select the tiled range object

        /// \tparam L The left tiled tensor object type
        /// \tparam R The right tiled tensor object type
        /// \param l The left tiled tensor object
        /// \param r The right tiled tensor object
        /// \return A const reference to the either the \c l or \c r tiled range
        /// object
        template <typename L, typename R>
        static inline const type& trange(const L& l, const R&) {
          return l.trange();
        }
      }; // struct trange_select

      template <typename CS>
      struct trange_select<DynamicTiledRange, StaticTiledRange<CS> > {
        typedef StaticTiledRange<CS> type;

        template <typename L, typename R>
        static inline const type& trange(const L&, const R& r) {
          return r.trange();
        }
      }; // struct trange_select

      /// Select logical and bitwise operation

      /// This class selects the correct bitwise and operation that correspond to
      /// the algebraic addition and subtraction operations.
      /// \tparam Op The binary element operation
      template <typename Op>
      struct shape_select {
        /// is_zero quarry

        /// Comparing two tiles for is_zero() quarry.
        /// \param l Left tile is_zero result.
        /// \param r Right tile is_zero result.
        /// \return True if l and r are true.
        static inline bool is_zero(bool l, bool r) { return l && r; }

        /// Construct a shape

        /// Construct a new bitset for shape.
        /// \param l The left argument shape.
        /// \param r The right argument shape.
        /// \return The result shape.
        static inline TiledArray::detail::Bitset<>
        get_shape(const TiledArray::detail::Bitset<>& l, const TiledArray::detail::Bitset<>& r) {
          return l | r;
        }
      }; // struct shape_select


      /// Select logical and bitwise operation

      /// This class selects the correct bitwise and operation that correspond to
      /// the algebraic multiplication operation.
      /// \tparam Op The binary element operation
      template <typename T>
      struct shape_select<std::multiplies<T> > {
        /// is_zero quarry

        /// Comparing two tiles for is_zero() quarry.
        /// \param l Left tile is_zero result.
        /// \param r Right tile is_zero result.
        /// \return True if l or r are true.
        static bool is_zero(bool l, bool r) { return l || r; }

        /// Construct a shape

        /// Construct a new bitset for shape.
        /// \param l The left argument shape.
        /// \param r The right argument shape.
        /// \return The result shape.
        static TiledArray::detail::Bitset<>
        get_shape(const TiledArray::detail::Bitset<>& l, const TiledArray::detail::Bitset<>& r) {
          return l & r;
        }
      }; // struct shape_select

    } // namespace


    template <typename Left, typename Right, typename Op>
    struct TensorTraits<BinaryTiledTensor<Left, Right, Op> > {
      typedef typename detail::range_select<typename Left::range_type,
          typename Right::range_type>::type range_type;
      typedef typename trange_select<typename Left::trange_type,
          typename Right::trange_type>::type trange_type;
      typedef typename Eval<BinaryTensor<typename Left::value_type,
          typename Right::value_type, Op> >::type value_type;
      typedef TiledArray::detail::DistributedStorage<value_type> storage_type;
      typedef typename storage_type::const_iterator const_iterator; ///< Tensor const iterator
      typedef typename storage_type::future const_reference;
    }; // struct TensorTraits<BinaryTiledTensor<Arg, Op> >

    namespace detail {

      /// Tensor that is composed from two argument tensors

      /// The tensor tiles are constructed with \c BinaryTensor. A binary operator
      /// is used to transform the individual elements of the tiles.
      /// \tparam Left The left argument type
      /// \tparam Right The right argument type
      /// \tparam Op The binary transform operator type.
      template <typename Left, typename Right, typename Op>
      class BinaryTiledTensorImpl {
      public:
        typedef BinaryTiledTensorImpl<Left, Right, Op> BinaryTiledTensorImpl_;
        typedef BinaryTiledTensor<Left, Right, Op> BinaryTiledTensor_;
        typedef Left left_tensor_type;
        typedef Right right_tensor_type;
        typedef ReadableTiledTensor<BinaryTiledTensor_> base;
        typedef typename base::size_type size_type;
        typedef typename base::range_type range_type;
        typedef typename base::eval_type eval_type;
        typedef typename base::pmap_interface pmap_interface;
        typedef typename base::trange_type trange_type;
        typedef typename base::value_type value_type;
        typedef typename base::const_reference const_reference;
        typedef typename base::const_iterator const_iterator;
        typedef TiledArray::detail::DistributedStorage<value_type> storage_type; /// The storage type for this object

      private:
        // Not allowed
        BinaryTiledTensorImpl_& operator=(const BinaryTiledTensorImpl_&);
        BinaryTiledTensorImpl(const BinaryTiledTensorImpl_&);

        /// Tile and task generator

        /// This object is passed to the parallel for_each function in MADNESS.
        /// It generates tasks that evaluates the tile for this tensor.
        /// \tparam The operations type, which is an instantiation of PermOps.
        class EvalLeft {
        public:
          typedef typename left_tensor_type::const_iterator iterator;
          typedef typename left_tensor_type::value_type left_arg_type;
          typedef typename right_tensor_type::value_type right_arg_type;

          EvalLeft(const std::shared_ptr<BinaryTiledTensorImpl_>& pimpl) :
              pimpl_(pimpl)
          { }

          EvalLeft(const EvalLeft& other) :
              pimpl_(other.pimpl_)
          { }

          EvalLeft& operator=(const EvalLeft& other) const {
            pimpl_ = other.pimpl_;
            return *this;
          }

          bool operator()(const iterator& it) const  {
            if(pimpl_->right_.is_zero(it.index())) {
              // Add a task where the right tile is zero and left tile is non-zero
              madness::Future<value_type> value =
                  pimpl_->get_world().taskq.add(& EvalLeft::eval_left, *it, pimpl_->op_);
              pimpl_->data_.set(it.index(), value);
            } else {
              // Add a task where both the left and right tiles are non-zero
              madness::Future<value_type> value =
                  pimpl_->get_world().taskq.add(& EvalLeft::eval, *it,
                      pimpl_->right_[it.index()], pimpl_->op_);
              pimpl_->data_.set(it.index(), value);
            }

            return true;
          }

          template <typename Archive>
          void serialize(const Archive& ar) { TA_ASSERT(false); }

        private:

          static value_type eval(const left_arg_type& left, const right_arg_type& right, const Op& op) {
            return make_binary_tensor(left, right, op);
          }

          static value_type eval_left(const typename left_tensor_type::value_type& left, const Op& op) {
            return make_unary_tensor(left, std::bind2nd(op,
                typename left_tensor_type::value_type::value_type(0)));
          }

          std::shared_ptr<BinaryTiledTensorImpl_> pimpl_; ///< pimpl to the owning expression object
        }; // class EvalLeft

        class EvalRight {
        public:
          typedef typename right_tensor_type::const_iterator iterator;
          typedef typename left_tensor_type::value_type left_arg_type;
          typedef typename right_tensor_type::value_type right_arg_type;

          typedef const iterator& argument_type;
          typedef bool result_type;

          EvalRight(const std::shared_ptr<BinaryTiledTensorImpl_>& pimpl) : pimpl_(pimpl) { }

          EvalRight(const EvalRight& other) : pimpl_(other.pimpl_) { }

          EvalRight& operator=(const EvalRight& other) const {
            pimpl_ = other.pimpl_;
            return *this;
          }

          result_type operator()(argument_type it) const  {
            if(pimpl_->left_.is_zero(it.index())) {
              // Add a task where the right tile is zero and left tile is non-zero
              madness::Future<value_type> value =
                  pimpl_->get_world().taskq.add(& EvalRight::eval_right, *it,
                  pimpl_->op_);
              pimpl_->data_.set(it.index(), value);
            }

            return true;
          }

          template <typename Archive>
          void serialize(const Archive& ar) { TA_ASSERT(false); }

        private:

          static value_type eval_right(const typename right_tensor_type::value_type& right, const Op& op) {
            return make_unary_tensor(right, std::bind1st(op, typename left_tensor_type::value_type::value_type(0)));
          }

          std::shared_ptr<BinaryTiledTensorImpl_> pimpl_;
        }; // class EvalRight

        /// Task function for generating tile evaluation tasks.

        /// The two parameters are given by futures that ensure the child
        /// arguments have completed before spawning tile tasks.
        /// \note: This task cannot return until all other \c for_each() tasks
        /// have completed. get() blocks this task until for_each() is done
        /// while still processing tasks.
        static bool generate_tasks(std::shared_ptr<BinaryTiledTensorImpl_> me, bool, bool) {
          TA_ASSERT(me->left_.vars() == me->right_.vars());
          TA_ASSERT(me->left_.trange() == me->right_.trange());

          madness::Future<bool> left_done = me->get_world().taskq.for_each(
              madness::Range<typename left_tensor_type::const_iterator>(
                  me->left_.begin(), me->left_.end()), EvalLeft(me));

          madness::Future<bool> right_done = me->get_world().taskq.for_each(
              madness::Range<typename right_tensor_type::const_iterator>(
                  me->right_.begin(), me->right_.end()), EvalRight(me));

          // This task cannot return until all other for_each tasks have completed.
          // Tasks are still being processed.
          return left_done.get() && right_done.get();
        }

      public:

        /// Construct a unary tiled tensor op

        /// \param arg The argument
        /// \param op The element transform operation
        BinaryTiledTensorImpl(const left_tensor_type& left, const right_tensor_type& right, const Op& op) :
          left_(left), right_(right), op_(op),
          data_(left.get_world(), left.size())
        {
          TA_ASSERT(left_.size() == right_.size());
        }

        void set_pmap(const std::shared_ptr<pmap_interface>& pmap) {
          data_.init(pmap);
        }

        /// Evaluate tensor to destination

        /// \tparam Dest The destination tensor type
        /// \param dest The destination to evaluate this tensor to
        template <typename Dest>
        void eval_to(Dest& dest) const {
          TA_ASSERT(range() == dest.range());

          // Add result tiles to dest
          typename pmap_interface::const_iterator end = data_.get_pmap()->end();
          for(typename pmap_interface::const_iterator it = data_.get_pmap()->begin(); it != end; ++it)
            if(! is_zero(*it))
              dest.set(*it, move(*it));
        }

        /// Evaluate the left argument

        /// \return A future to a bool that will be set once the argument has
        /// been evaluated.
        madness::Future<bool> eval_left(const VariableList& v, const std::shared_ptr<pmap_interface>& pmap) {
          return left_.eval(v, pmap);
        }

        /// Evaluate the right argument

        /// \return A future to a bool that will be set once the argument has
        /// been evaluated.
        madness::Future<bool> eval_right(const VariableList& v, const std::shared_ptr<pmap_interface>& pmap) {
          return right_.eval(v, pmap);
        }

        static madness::Future<bool> generate_tiles(std::shared_ptr<BinaryTiledTensorImpl_> me,
            const madness::Future<bool>& left_done, const madness::Future<bool>& right_done)
        {
          return me->get_world().taskq.add(& BinaryTiledTensorImpl_::generate_tasks, me,
              left_done, right_done, madness::TaskAttributes::hipri());
        }

        /// Tensor tile size array accessor

        /// \return The size array of the tensor tiles
        const range_type& range() const {
          return detail::range_select<typename left_tensor_type::range_type,
              typename right_tensor_type::range_type>::range(left_, right_);
        }

        /// Tensor tile volume accessor

        /// \return The number of tiles in the tensor
        size_type size() const {
          return left_.size();
        }

        /// Query a tile owner

        /// \param i The tile index to query
        /// \return The process ID of the node that owns tile \c i
        ProcessID owner(size_type i) const { return data_.owner(i); }

        /// Query for a locally owned tile

        /// \param i The tile index to query
        /// \return \c true if the tile is owned by this node, otherwise \c false
        bool is_local(size_type i) const { return data_.is_local(i); }

        /// Query for a zero tile

        /// \param i The tile index to query
        /// \return \c true if the tile is zero, otherwise \c false
        bool is_zero(size_type i) const {
          TA_ASSERT(range().includes(i));
          if(is_dense())
            return false;
          return shape_select<Op>::is_zero(is_zero(left_, i), is_zero(right_, i));
        }

        /// Tensor process map accessor

        /// \return A shared pointer to the process map of this tensor
        const std::shared_ptr<pmap_interface>& get_pmap() const { return data_.get_pmap(); }

        /// Query the density of the tensor

        /// \return \c true if the tensor is dense, otherwise false
        bool is_dense() const { return left_.is_dense() || right_.is_dense(); }

        /// Tensor shape accessor

        /// \return A reference to the tensor shape map
        TiledArray::detail::Bitset<> get_shape() const {
          TA_ASSERT(! is_dense());
          return shape_select<Op>::get_shape(left_.get_shape(), right_.get_shape());
        }

        /// Tiled range accessor

        /// \return The tiled range of the tensor
        const trange_type& trange() const {
          return trange_select<typename left_tensor_type::trange_type,
            typename right_tensor_type::trange_type>::trange(left_, right_);
        }

        /// Tile accessor

        /// \param i The tile index
        /// \return Tile \c i
        const_reference operator[](size_type i) const {
          TA_ASSERT(! is_zero(i));
          return data_[i];
        }

        /// Tile move

        /// Tile is removed after it is set.
        /// \param i The tile index
        /// \return Tile \c i
        const_reference move(size_type i) const {
          TA_ASSERT(! is_zero(i));
          return data_.move(i);
        }

        /// Array begin iterator

        /// \return A const iterator to the first element of the array.
        const_iterator begin() const { return data_.begin(); }

        /// Array end iterator

        /// \return A const iterator to one past the last element of the array.
        const_iterator end() const { return data_.end(); }

        /// Variable annotation for the array.
        const VariableList& vars() const { return left_.vars(); }

        madness::World& get_world() const { return data_.get_world(); }

        /// Clear the tile data

        /// Remove all tiles from the tensor.
        /// \note: Any tiles will remain in memory until the last reference
        /// is destroyed.
        void clear() { data_.clear(); }

      private:

        template <typename A>
        bool is_zero(const A& arg, size_type i) const {
          if(arg.is_dense())
            return false;

          return arg.is_zero(i);
        }

        left_tensor_type left_; ///< Left argument
        right_tensor_type right_; ///< Right argument
        storage_type data_; ///< Store temporary data
        Op op_; ///< binary element operator
      }; // class BinaryTiledTensorImpl

    } // namespace detail


    /// Tensor that is composed from two argument tensors

    /// The tensor tiles are constructed with \c BinaryTensor. A binary operator
    /// is used to transform the individual elements of the tiles.
    /// \tparam Left The left argument type
    /// \tparam Right The right argument type
    /// \tparam Op The binary transform operator type.
    template <typename Left, typename Right, typename Op>
    class BinaryTiledTensor : public ReadableTiledTensor<BinaryTiledTensor<Left, Right, Op> > {
    public:
      typedef BinaryTiledTensor<Left, Right, Op> BinaryTiledTensor_;
      typedef Left left_tensor_type;
      typedef Right right_tensor_type;
      typedef ReadableTiledTensor<BinaryTiledTensor_> base;
      typedef typename base::size_type size_type;
      typedef typename base::range_type range_type;
      typedef typename base::eval_type eval_type;
      typedef typename base::pmap_interface pmap_interface;
      typedef typename base::trange_type trange_type;
      typedef typename base::value_type value_type;
      typedef typename base::const_reference const_reference;
      typedef typename base::const_iterator const_iterator;

    private:
      typedef detail::BinaryTiledTensorImpl<Left, Right, Op> impl_type;

    public:

      BinaryTiledTensor() : pimpl_() { }

      /// Construct a unary tiled tensor op

      /// \param arg The argument
      /// \param op The element transform operation
      BinaryTiledTensor(const left_tensor_type& left, const right_tensor_type& right, const Op& op) :
        pimpl_(new impl_type(left, right, op),
            madness::make_deferred_deleter<impl_type>(left.get_world()))
      { }

      /// Construct a unary tiled tensor op

      /// \param arg The argument
      /// \param op The element transform operation
      BinaryTiledTensor(const BinaryTiledTensor_& other) :
          pimpl_(other.pimpl_)
      { }

      /// Assignment operator

      /// Assignment makes a shallow copy of \c other.
      /// \param other The binary tensor to be copied.
      /// \return A reference to this object
      BinaryTiledTensor_& operator=(const BinaryTiledTensor_& other) {
        pimpl_ = other.pimpl_;
        return *this;
      }

      /// Evaluate tensor to destination

      /// \tparam Dest The destination tensor type
      /// \param dest The destination to evaluate this tensor to
      template <typename Dest>
      void eval_to(Dest& dest) const {
        TA_ASSERT(pimpl_);
        pimpl_->eval_to(dest);
      }

      madness::Future<bool> eval(const VariableList& v, const std::shared_ptr<pmap_interface>& pmap) {
        TA_ASSERT(pimpl_);
        pimpl_->set_pmap(pmap);
        return impl_type::generate_tiles(pimpl_, pimpl_->eval_left(v, pmap->clone()),
            pimpl_->eval_right(v, pmap->clone()));
      }

      /// Tensor tile size array accessor

      /// \return The size array of the tensor tiles
      const range_type& range() const {
        TA_ASSERT(pimpl_);
        return pimpl_->range();
      }

      /// Tensor tile volume accessor

      /// \return The number of tiles in the tensor
      size_type size() const {
        TA_ASSERT(pimpl_);
        return pimpl_->size();
      }

      /// Query a tile owner

      /// \param i The tile index to query
      /// \return The process ID of the node that owns tile \c i
      ProcessID owner(size_type i) const {
        TA_ASSERT(pimpl_);
        return pimpl_->owner(i);
      }

      /// Query for a locally owned tile

      /// \param i The tile index to query
      /// \return \c true if the tile is owned by this node, otherwise \c false
      bool is_local(size_type i) const {
        TA_ASSERT(pimpl_);
        return pimpl_->is_local(i);
      }

      /// Query for a zero tile

      /// \param i The tile index to query
      /// \return \c true if the tile is zero, otherwise \c false
      bool is_zero(size_type i) const {
        TA_ASSERT(pimpl_);
        return pimpl_->is_zero(i);
      }

      /// Tensor process map accessor

      /// \return A shared pointer to the process map of this tensor
      const std::shared_ptr<pmap_interface>& get_pmap() const {
        TA_ASSERT(pimpl_);
        return pimpl_->get_pmap();
      }

      /// Query the density of the tensor

      /// \return \c true if the tensor is dense, otherwise false
      bool is_dense() const {
        TA_ASSERT(pimpl_);
        return pimpl_->is_dense();
      }

      /// Tensor shape accessor

      /// \return A reference to the tensor shape map
      TiledArray::detail::Bitset<> get_shape() const {
        TA_ASSERT(pimpl_);
        return pimpl_->get_shape();
      }

      /// Tiled range accessor

      /// \return The tiled range of the tensor
      const trange_type& trange() const {
        TA_ASSERT(pimpl_);
        return pimpl_->trange();
      }

      /// Tile accessor

      /// \param i The tile index
      /// \return Tile \c i
      const_reference operator[](size_type i) const {
        TA_ASSERT(pimpl_);
        return pimpl_->operator[](i);
      }

      /// Tile move

      /// Tile is removed after it is set.
      /// \param i The tile index
      /// \return Tile \c i
      const_reference move(size_type i) const {
        TA_ASSERT(pimpl_);
        return pimpl_->move(i);
      }

      /// Array begin iterator

      /// \return A const iterator to the first element of the array.
      const_iterator begin() const {
        TA_ASSERT(pimpl_);
        return pimpl_->begin();
      }

      /// Array end iterator

      /// \return A const iterator to one past the last element of the array.
      const_iterator end() const {
        TA_ASSERT(pimpl_);
        return pimpl_->end();
      }

      /// Variable annotation for the array.
      const VariableList& vars() const {
        TA_ASSERT(pimpl_);
        return pimpl_->vars();
      }

      madness::World& get_world() const {
        TA_ASSERT(pimpl_);
        return pimpl_->get_world();
      }

      /// Release tensor data

      /// Clear all tensor data from memory. This is equivalent to
      /// \c BinaryTiledTensor().swap(*this) .
      void release() {
        if(pimpl_) {
          pimpl_->clear();
          pimpl_.reset();
        }
      }

      template <typename Archive>
      void serialize(const Archive&) { TA_ASSERT(false); }

    private:
      std::shared_ptr<impl_type> pimpl_;
    }; // class BinaryTiledTensor


  }  // namespace expressions
}  // namespace TiledArray

namespace madness {
  namespace archive {

    template <typename Archive, typename T>
    struct ArchiveStoreImpl;
    template <typename Archive, typename T>
    struct ArchiveLoadImpl;

    template <typename Archive, typename Left, typename Right, typename Op>
    struct ArchiveStoreImpl<Archive, std::shared_ptr<TiledArray::expressions::detail::BinaryTiledTensorImpl<Left, Right, Op> > > {
      static void store(const Archive&, const std::shared_ptr<TiledArray::expressions::detail::BinaryTiledTensorImpl<Left, Right, Op> >&) {
        TA_ASSERT(false);
      }
    };

    template <typename Archive, typename Left, typename Right, typename Op>
    struct ArchiveLoadImpl<Archive, std::shared_ptr<TiledArray::expressions::detail::BinaryTiledTensorImpl<Left, Right, Op> > > {
      static void load(const Archive&, std::shared_ptr<TiledArray::expressions::detail::BinaryTiledTensorImpl<Left, Right, Op> >&) {
        TA_ASSERT(false);
      }
    };
  } // namespace archive
} // namespace madness

#endif // TILEDARRAY_BINARY_TILED_TENSOR_H__INCLUDED
