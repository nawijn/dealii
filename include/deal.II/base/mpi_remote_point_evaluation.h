// ------------------------------------------------------------------------
//
// SPDX-License-Identifier: LGPL-2.1-or-later
// Copyright (C) 2021 - 2023 by the deal.II authors
//
// This file is part of the deal.II library.
//
// Part of the source code is dual licensed under Apache-2.0 WITH
// LLVM-exception OR LGPL-2.1-or-later. Detailed license information
// governing the source code and code contributions can be found in
// LICENSE.md and CONTRIBUTING.md at the top level directory of deal.II.
//
// ------------------------------------------------------------------------

#ifndef dealii_mpi_mpi_remote_point_evaluation_h
#define dealii_mpi_mpi_remote_point_evaluation_h

#include <deal.II/base/config.h>

#include <deal.II/base/mpi.h>
#include <deal.II/base/mpi_tags.h>

#include <deal.II/dofs/dof_handler.h>

DEAL_II_NAMESPACE_OPEN

// Forward declarations
namespace GridTools
{
  namespace internal
  {
    template <int dim, int spacedim>
    struct DistributedComputePointLocationsInternal;
  }
} // namespace GridTools

namespace Utilities
{
  namespace MPI
  {
    /**
     * Helper class to access values on non-matching grids.
     *
     * @note The name of the fields are chosen with the method
     *   evaluate_and_process() in mind. Here, quantities are
     *   computed at specified arbitrary positioned points (and even on remote
     *   processes in the MPI universe) cell by cell and these values are sent
     *   to requesting processes, which receive the result and resort the
     *   result according to the points.
     */
    template <int dim, int spacedim = dim>
    class RemotePointEvaluation
    {
    public:
      /**
       * AdditionalData structure that can be used to tweak parameters
       * of RemotePointEvaluation.
       */
      struct AdditionalData
      {
      public:
        /**
         *  Constructor.
         */
        AdditionalData(
          const double       tolerance                              = 1e-6,
          const bool         enforce_unique_mapping                 = false,
          const unsigned int rtree_level                            = 0,
          const std::function<std::vector<bool>()> &marked_vertices = {});

        /**
         * Tolerance in terms of unit cell coordinates for determining all cells
         * around a point passed to RemotePointEvaluation during reinit().
         * Depending on the problem, it might be necessary to adjust the
         * tolerance in order to be able to identify a cell. Floating point
         * arithmetic implies that a point will, in general, not lie exactly on
         * a vertex, edge, or face.
         */
        double tolerance;

        /**
         * Enforce unique mapping, i.e., (one-to-one) relation of points and
         * cells.
         */
        bool enforce_unique_mapping;

        /**
         * RTree level to be used during the construction of the bounding boxes.
         */
        unsigned int rtree_level;

        /**
         * Function that marks relevant vertices to make search of active cells
         * around point more efficient.
         */
        std::function<std::vector<bool>()> marked_vertices;
      };

      /**
       * Constructor.
       *
       * @param additional_data Configure options for RemotePointEvaluation.
       */
      RemotePointEvaluation(
        const AdditionalData &additional_data = AdditionalData());

      /**
       * Constructor. This constructor is deprecated. Use the other constructor
       * taking AdditionalData instead.
       *
       * @param tolerance Tolerance in terms of unit cell coordinates for
       *   determining all cells around a point passed to the class during
       *   reinit(). Depending on the problem, it might be necessary to adjust
       *   the tolerance in order to be able to identify a cell.
       *   Floating point arithmetic implies that a point will, in general, not
       *   lie exactly on a vertex, edge, or face.
       * @param enforce_unique_mapping Enforce unique mapping, i.e.,
       *   (one-to-one) relation of points and cells.
       * @param rtree_level RTree level to be used during the construction of the bounding boxes.
       * @param marked_vertices Function that marks relevant vertices to make search
       *   of active cells around point more efficient.
       *
       * @deprecated
       */
      DEAL_II_DEPRECATED_EARLY_WITH_COMMENT(
        "Use the constructor with AdditionalData struct.")
      RemotePointEvaluation(
        const double       tolerance,
        const bool         enforce_unique_mapping                 = false,
        const unsigned int rtree_level                            = 0,
        const std::function<std::vector<bool>()> &marked_vertices = {});

      /**
       * Destructor.
       */
      ~RemotePointEvaluation();

      /**
       * Set up internal data structures and communication pattern based on
       * a list of points @p points and mesh description (@p tria and @p
       * mapping).
       *
       * @warning This is a collective call that needs to be executed by all
       *   processors in the communicator.
       *
       * @note If you want to be sure that all points have been found, call
       *   all_points_found() after calling this function.
       */
      void
      reinit(const std::vector<Point<spacedim>> &points,
             const Triangulation<dim, spacedim> &tria,
             const Mapping<dim, spacedim>       &mapping);

      /**
       * Set up internal data structures and communication pattern based on
       * GridTools::internal::DistributedComputePointLocationsInternal.
       *
       * This function is called internally by the reinit() function above.
       * Having it as a separate function makes it possible to set up the class
       * if it is known in which cells corresponding reference points are
       * located (e.g. if intersections of cells are known).
       */
      void
      reinit(const GridTools::internal::
               DistributedComputePointLocationsInternal<dim, spacedim> &data,
             const Triangulation<dim, spacedim>                        &tria,
             const Mapping<dim, spacedim> &mapping);

      /**
       * Helper class to store and to access data of points positioned in
       * processed cells.
       */
      class CellData
      {
      public:
        /**
         * Constructor.
         *
         * @param triangulation Triangulation of the domain.
         */
        CellData(const Triangulation<dim, spacedim> &triangulation);

        /**
         * Return an object that can be thought of as an array containing all
         * indices from zero (inclusive) to the total number of cells
         * where points reside given by `cells.size()` (exclusive). This allows
         * one to write code using range-based `for` loops of the following
         * kind:
         *
         * @code
         * for (const auto cell : cell_data.cell_indices())
         *   {
         *     const auto cell_dofs =
         *       cell_data.get_active_cell_iterator(cell)->as_dof_handler_iterator(
         *         dof_handler);
         *
         *     const auto unit_points = cell_data.get_unit_points(cell);
         *     const auto local_value = cell_data.get_data_view(cell, values);
         *
         *     // user code: not shown
         *   }
         * @endcode
         *
         * Here, we are looping over all cells where points reside and
         * use the index to call the functions get_active_cell_iterator(),
         * get_unit_points(), and get_data_view().
         */
        std_cxx20::ranges::iota_view<unsigned int, unsigned int>
        cell_indices() const;

        /**
         * Return active cell iterator of the processed cell @p cell.
         */
        typename Triangulation<dim, spacedim>::active_cell_iterator
        get_active_cell_iterator(const unsigned int cell) const;

        /**
         * Return unit points of the processed cell @p cell.
         */
        ArrayView<const Point<dim>>
        get_unit_points(const unsigned int cell) const;

        /**
         * Return local view of the processed cell @p cell for the vector @p values.
         */
        template <typename T>
        ArrayView<T>
        get_data_view(const unsigned int  cell,
                      const ArrayView<T> &values) const;

        /**
         * Level and index of processed cells.
         */
        std::vector<std::pair<int, int>> cells;

        /**
         * Pointers to the start and end of the (reference) points
         * associated to the cell.
         */
        std::vector<unsigned int> reference_point_ptrs;

        /**
         * Reference points in the interval [0,1]^dim.
         */
        std::vector<Point<dim>> reference_point_values;

      private:
        /**
         * Reference to the underlying triangulation needed for
         * get_active_cell_iterator().
         */
        const Triangulation<dim, spacedim> &triangulation;
      };

      /**
       * Return internal data structure storing the data of points
       * positioned in cells.
       */
      const CellData &
      get_cell_data() const;

      /**
       * Evaluate function @p evaluation_function in the given  points and
       * triangulation. The result is stored in @p output.
       *
       * @note If the map of points to cells is not a
       *   one-to-one relation (is_map_unique()==false), the result needs to be
       *   processed with the help of get_point_ptrs(). This
       *   might be the case if a point coincides with a geometric entity (e.g.,
       *   vertex) that is shared by multiple cells or a point is outside of the
       *   computational domain.
       *
       * @warning This is a collective call that needs to be executed by all
       *   processors in the communicator.
       */
      template <typename T>
      void
      evaluate_and_process(
        std::vector<T> &output,
        std::vector<T> &buffer,
        const std::function<void(const ArrayView<T> &, const CellData &)>
          &evaluation_function) const;

      /**
       * Same as above but with the result provided as return value and
       * without external allocation of a user-provided buffer.
       */
      template <typename T>
      std::vector<T>
      evaluate_and_process(
        const std::function<void(const ArrayView<T> &, const CellData &)>
          &evaluation_function) const;

      /**
       * This method is the inverse of the method evaluate_and_process(). It
       * makes the data at the points, provided by @p input, available in the
       * function @p evaluation_function.
       *
       * @warning This is a collective call that needs to be executed by all
       *   processors in the communicator.
       */
      template <typename T>
      void
      process_and_evaluate(
        const std::vector<T> &input,
        std::vector<T>       &buffer,
        const std::function<void(const ArrayView<const T> &, const CellData &)>
          &evaluation_function) const;

      /**
       * Same as above but without external allocation of a user-provided
       * buffer.
       */
      template <typename T>
      void
      process_and_evaluate(
        const std::vector<T> &input,
        const std::function<void(const ArrayView<const T> &, const CellData &)>
          &evaluation_function) const;

      /**
       * Return a CRS-like data structure to determine the position of the
       * result corresponding a point and the amount.
       */
      const std::vector<unsigned int> &
      get_point_ptrs() const;

      /**
       * Return if points and cells have a one-to-one relation. This is not the
       * case if a point is not owned by any cell (the point is outside of the
       * domain) or if multiple cells own the point (the point is positioned
       * on a geometric entity shared by neighboring cells).
       */
      bool
      is_map_unique() const;

      /**
       * Return if all points could be found in the domain.
       */
      bool
      all_points_found() const;

      /**
       * Return if point @p i could be found in the domain.
       */
      bool
      point_found(const unsigned int i) const;

      /**
       * Return the Triangulation object used during reinit().
       */
      const Triangulation<dim, spacedim> &
      get_triangulation() const;

      /**
       * Return the Mapping object used during reinit().
       */
      const Mapping<dim, spacedim> &
      get_mapping() const;

      /**
       * Return if the internal data structures have been set up and if yes
       * whether they are still valid (and not invalidated due to changes of the
       * Triangulation).
       */
      bool
      is_ready() const;

    private:
      /**
       * Additional data with basic settings.
       */
      const AdditionalData additional_data;

      /**
       * Storage for the status of the triangulation signal.
       */
      boost::signals2::connection tria_signal;

      /**
       * Flag indicating if the reinit() function has been called and if yes
       * the triangulation has not been modified since then (potentially
       * invalidating the communication pattern).
       */
      bool ready_flag;

      /**
       * Reference to the Triangulation object used during reinit().
       */
      SmartPointer<const Triangulation<dim, spacedim>> tria;

      /**
       * Reference to the Mapping object used during reinit().
       */
      SmartPointer<const Mapping<dim, spacedim>> mapping;

      /**
       * (One-to-one) relation of points and cells.
       */
      bool unique_mapping;

      /**
       * Cache if all points passed in during reinit() have been found.
       */
      bool all_points_found_flag;

      /**
       * Since for each point multiple or no results can be available, the
       * pointers in this vector indicate the first and last entry associated
       * with a point in a CRS-like fashion.
       */
      std::vector<unsigned int> point_ptrs;

      /**
       * Permutation index within a recv buffer.
       */
      std::vector<unsigned int> recv_permutation;

      /**
       * Pointers of ranges within a receive buffer that are filled by ranks
       * specified by recv_ranks.
       */
      std::vector<unsigned int> recv_ptrs;

      /**
       * Ranks from where data is received.
       */
      std::vector<unsigned int> recv_ranks;

      /**
       * Point data sorted according to cells so that evaluation (incl. reading
       * of degrees of freedoms) needs to be performed only once per cell.
       */
      std::unique_ptr<CellData> cell_data;

      /**
       * Permutation index within a send buffer.
       */
      std::vector<unsigned int> send_permutation;

      /**
       * Ranks to send to.
       */
      std::vector<unsigned int> send_ranks;

      /**
       * Pointers of ranges within a send buffer to be sent to the ranks
       * specified by send_ranks.
       */
      std::vector<unsigned int> send_ptrs;
    };



    template <int dim, int spacedim>
    template <typename T>
    ArrayView<T>
    RemotePointEvaluation<dim, spacedim>::CellData::get_data_view(
      const unsigned int  cell,
      const ArrayView<T> &values) const
    {
      AssertIndexRange(cell, cells.size());
      return {values.data() + reference_point_ptrs[cell],
              reference_point_ptrs[cell + 1] - reference_point_ptrs[cell]};
    }



    template <int dim, int spacedim>
    template <typename T>
    void
    RemotePointEvaluation<dim, spacedim>::evaluate_and_process(
      std::vector<T> &output,
      std::vector<T> &buffer,
      const std::function<void(const ArrayView<T> &, const CellData &)>
        &evaluation_function) const
    {
#ifndef DEAL_II_WITH_MPI
      Assert(false, ExcNeedsMPI());
      (void)output;
      (void)buffer;
      (void)evaluation_function;
#else
      static CollectiveMutex      mutex;
      CollectiveMutex::ScopedLock lock(mutex, tria->get_communicator());

      const unsigned int my_rank =
        Utilities::MPI::this_mpi_process(tria->get_communicator());

      // allocate memory for output and buffer
      output.resize(point_ptrs.back());
      buffer.resize(std::max(send_permutation.size() * 2,
                             point_ptrs.back() + send_permutation.size()));

      // ... for evaluation
      ArrayView<T> buffer_eval(buffer.data(), send_permutation.size());

      // ... for communication
      ArrayView<T> buffer_comm(buffer.data() + send_permutation.size(),
                               send_permutation.size());

      // evaluate functions at points
      evaluation_function(buffer_eval, *cell_data);

      // sort for communication
      const auto my_rank_local_recv_ptr =
        std::find(recv_ranks.begin(), recv_ranks.end(), my_rank);

      if (my_rank_local_recv_ptr != recv_ranks.end())
        {
          const unsigned int my_rank_local_recv =
            std::distance(recv_ranks.begin(), my_rank_local_recv_ptr);
          const unsigned int my_rank_local_send = std::distance(
            send_ranks.begin(),
            std::find(send_ranks.begin(), send_ranks.end(), my_rank));
          const unsigned int  start = send_ptrs[my_rank_local_send];
          const unsigned int  end   = send_ptrs[my_rank_local_send + 1];
          const unsigned int *recv_ptr =
            recv_permutation.data() + recv_ptrs[my_rank_local_recv];
          for (unsigned int i = 0; i < send_permutation.size(); ++i)
            {
              const unsigned int send_index = send_permutation[i];

              // local data -> can be copied to output directly
              if (start <= send_index && send_index < end)
                output[recv_ptr[send_index - start]] = buffer_eval[i];
              else // data to be sent
                buffer_comm[send_index] = buffer_eval[i];
            }
        }
      else
        {
          for (unsigned int i = 0; i < send_permutation.size(); ++i)
            buffer_comm[send_permutation[i]] = buffer_eval[i];
        }

      // send data
      std::vector<std::vector<char>> send_buffer;
      send_buffer.reserve(send_ranks.size());

      std::vector<MPI_Request> send_requests;
      send_requests.reserve(send_ranks.size());

      for (unsigned int i = 0; i < send_ranks.size(); ++i)
        {
          if (send_ranks[i] == my_rank)
            continue;

          send_requests.emplace_back(MPI_Request());

          send_buffer.emplace_back(Utilities::pack(
            std::vector<T>(buffer_comm.begin() + send_ptrs[i],
                           buffer_comm.begin() + send_ptrs[i + 1]),
            false));

          const int ierr = MPI_Isend(send_buffer.back().data(),
                                     send_buffer.back().size(),
                                     MPI_CHAR,
                                     send_ranks[i],
                                     internal::Tags::remote_point_evaluation,
                                     tria->get_communicator(),
                                     &send_requests.back());
          AssertThrowMPI(ierr);
        }

      // receive data
      std::vector<char> buffer_char;

      for (unsigned int i = 0; i < recv_ranks.size(); ++i)
        {
          if (recv_ranks[i] == my_rank)
            continue;

          // receive remote data
          MPI_Status status;
          int        ierr = MPI_Probe(MPI_ANY_SOURCE,
                               internal::Tags::remote_point_evaluation,
                               tria->get_communicator(),
                               &status);
          AssertThrowMPI(ierr);

          int message_length;
          ierr = MPI_Get_count(&status, MPI_CHAR, &message_length);
          AssertThrowMPI(ierr);

          buffer_char.resize(message_length);

          ierr = MPI_Recv(buffer_char.data(),
                          buffer_char.size(),
                          MPI_CHAR,
                          status.MPI_SOURCE,
                          internal::Tags::remote_point_evaluation,
                          tria->get_communicator(),
                          MPI_STATUS_IGNORE);
          AssertThrowMPI(ierr);

          // unpack data
          const auto buffer =
            Utilities::unpack<std::vector<T>>(buffer_char, false);

          // write data into output vector
          const auto ptr =
            std::find(recv_ranks.begin(), recv_ranks.end(), status.MPI_SOURCE);

          Assert(ptr != recv_ranks.end(), ExcNotImplemented());

          const unsigned int j = std::distance(recv_ranks.begin(), ptr);

          AssertDimension(buffer.size(), recv_ptrs[j + 1] - recv_ptrs[j]);

          for (unsigned int i = recv_ptrs[j], c = 0; i < recv_ptrs[j + 1];
               ++i, ++c)
            output[recv_permutation[i]] = buffer[c];
        }

      // make sure all messages have been sent
      if (!send_requests.empty())
        {
          const int ierr = MPI_Waitall(send_requests.size(),
                                       send_requests.data(),
                                       MPI_STATUSES_IGNORE);
          AssertThrowMPI(ierr);
        }
#endif
    }


    template <int dim, int spacedim>
    template <typename T>
    std::vector<T>
    RemotePointEvaluation<dim, spacedim>::evaluate_and_process(
      const std::function<void(const ArrayView<T> &, const CellData &)>
        &evaluation_function) const
    {
      std::vector<T> output;
      std::vector<T> buffer;

      this->evaluate_and_process(output, buffer, evaluation_function);

      return output;
    }



    template <int dim, int spacedim>
    template <typename T>
    void
    RemotePointEvaluation<dim, spacedim>::process_and_evaluate(
      const std::vector<T> &input,
      std::vector<T>       &buffer,
      const std::function<void(const ArrayView<const T> &, const CellData &)>
        &evaluation_function) const
    {
#ifndef DEAL_II_WITH_MPI
      Assert(false, ExcNeedsMPI());
      (void)input;
      (void)buffer;
      (void)evaluation_function;
#else
      static CollectiveMutex      mutex;
      CollectiveMutex::ScopedLock lock(mutex, tria->get_communicator());

      const unsigned int my_rank =
        Utilities::MPI::this_mpi_process(tria->get_communicator());

      // invert permutation matrices (TODO: precompute)
      std::vector<unsigned int> recv_permutation_inv(recv_permutation.size());
      for (unsigned int c = 0; c < recv_permutation.size(); ++c)
        recv_permutation_inv[recv_permutation[c]] = c;

      std::vector<unsigned int> send_permutation_inv(send_permutation.size());
      for (unsigned int c = 0; c < send_permutation.size(); ++c)
        send_permutation_inv[send_permutation[c]] = c;

      // allocate memory for buffer
      const auto &point_ptrs = this->get_point_ptrs();
      AssertDimension(input.size(), point_ptrs.size() - 1);
      buffer.resize(std::max(send_permutation.size() * 2,
                             point_ptrs.back() + send_permutation.size()));

      // ... for evaluation
      ArrayView<T> buffer_eval(buffer.data(), send_permutation.size());

      // ... for communication
      ArrayView<T> buffer_comm(buffer.data() + send_permutation.size(),
                               point_ptrs.back());

      // sort for communication (and duplicate data if necessary)

      const auto my_rank_local_recv_ptr =
        std::find(recv_ranks.begin(), recv_ranks.end(), my_rank);

      if (my_rank_local_recv_ptr != recv_ranks.end())
        {
          // optimize the case if we have our own rank among the possible list
          const unsigned int my_rank_local_recv =
            std::distance(recv_ranks.begin(), my_rank_local_recv_ptr);
          const unsigned int my_rank_local_send = std::distance(
            send_ranks.begin(),
            std::find(send_ranks.begin(), send_ranks.end(), my_rank));

          const unsigned int  start = recv_ptrs[my_rank_local_recv];
          const unsigned int  end   = recv_ptrs[my_rank_local_recv + 1];
          const unsigned int *send_ptr =
            send_permutation_inv.data() + send_ptrs[my_rank_local_send];
          for (unsigned int i = 0, c = 0; i < point_ptrs.size() - 1; ++i)
            {
              const unsigned int next = point_ptrs[i + 1];
              for (unsigned int j = point_ptrs[i]; j < next; ++j, ++c)
                {
                  const unsigned int recv_index = recv_permutation_inv[c];

                  // local data -> can be copied to final buffer directly
                  if (start <= recv_index && recv_index < end)
                    buffer_eval[send_ptr[recv_index - start]] = input[i];
                  else // data to be sent
                    buffer_comm[recv_index] = input[i];
                }
            }
        }
      else
        {
          for (unsigned int i = 0, c = 0; i < point_ptrs.size() - 1; ++i)
            for (unsigned int j = point_ptrs[i]; j < point_ptrs[i + 1];
                 ++j, ++c)
              buffer_comm[recv_permutation_inv[c]] = input[i];
        }

      // send data
      std::vector<std::vector<char>> send_buffer;
      send_buffer.reserve(recv_ranks.size());

      std::vector<MPI_Request> send_requests;
      send_requests.reserve(recv_ranks.size());

      for (unsigned int i = 0; i < recv_ranks.size(); ++i)
        {
          if (recv_ranks[i] == my_rank)
            continue;

          send_requests.push_back(MPI_Request());

          send_buffer.emplace_back(Utilities::pack(
            std::vector<T>(buffer_comm.begin() + recv_ptrs[i],
                           buffer_comm.begin() + recv_ptrs[i + 1]),
            false));

          const int ierr = MPI_Isend(send_buffer.back().data(),
                                     send_buffer.back().size(),
                                     MPI_CHAR,
                                     recv_ranks[i],
                                     internal::Tags::remote_point_evaluation,
                                     tria->get_communicator(),
                                     &send_requests.back());
          AssertThrowMPI(ierr);
        }

      // receive data
      std::vector<char> recv_buffer;

      for (unsigned int i = 0; i < send_ranks.size(); ++i)
        {
          if (send_ranks[i] == my_rank)
            continue;

          // receive remote data
          MPI_Status status;
          int        ierr = MPI_Probe(MPI_ANY_SOURCE,
                               internal::Tags::remote_point_evaluation,
                               tria->get_communicator(),
                               &status);
          AssertThrowMPI(ierr);

          int message_length;
          ierr = MPI_Get_count(&status, MPI_CHAR, &message_length);
          AssertThrowMPI(ierr);

          recv_buffer.resize(message_length);

          ierr = MPI_Recv(recv_buffer.data(),
                          recv_buffer.size(),
                          MPI_CHAR,
                          status.MPI_SOURCE,
                          internal::Tags::remote_point_evaluation,
                          tria->get_communicator(),
                          MPI_STATUS_IGNORE);
          AssertThrowMPI(ierr);

          // unpack data
          const auto recv_buffer_unpacked =
            Utilities::unpack<std::vector<T>>(recv_buffer, false);

          // write data into buffer vector
          const auto ptr =
            std::find(send_ranks.begin(), send_ranks.end(), status.MPI_SOURCE);

          Assert(ptr != send_ranks.end(), ExcNotImplemented());

          const unsigned int j = std::distance(send_ranks.begin(), ptr);

          AssertDimension(recv_buffer_unpacked.size(),
                          send_ptrs[j + 1] - send_ptrs[j]);

          for (unsigned int i = send_ptrs[j], c = 0; i < send_ptrs[j + 1];
               ++i, ++c)
            buffer_eval[send_permutation_inv[i]] = recv_buffer_unpacked[c];
        }

      if (!send_requests.empty())
        {
          const int ierr = MPI_Waitall(send_requests.size(),
                                       send_requests.data(),
                                       MPI_STATUSES_IGNORE);
          AssertThrowMPI(ierr);
        }

      // evaluate function at points
      evaluation_function(buffer_eval, *cell_data);
#endif
    }



    template <int dim, int spacedim>
    template <typename T>
    void
    RemotePointEvaluation<dim, spacedim>::process_and_evaluate(
      const std::vector<T> &input,
      const std::function<void(const ArrayView<const T> &, const CellData &)>
        &evaluation_function) const
    {
      std::vector<T> buffer;
      this->process_and_evaluate(input, buffer, evaluation_function);
    }

  } // end of namespace MPI
} // end of namespace Utilities


DEAL_II_NAMESPACE_CLOSE

#endif
