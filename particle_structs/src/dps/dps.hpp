#pragma once
#include <particle_structs.hpp>
#ifdef PP_ENABLE_CAB
#include <Cabana_Core.hpp>
#include "psMemberTypeCabana.h"
#include "dps_input.hpp"
#include <sstream>

namespace pumipic {

  void enable_prebarrier();
  double prebarrier();

  template <class DataTypes, typename MemSpace = DefaultMemSpace>
  class DPS : public ParticleStructure<DataTypes, MemSpace> {
  public:
    using typename ParticleStructure<DataTypes, MemSpace>::execution_space;
    using typename ParticleStructure<DataTypes, MemSpace>::memory_space;
    using typename ParticleStructure<DataTypes, MemSpace>::device_type;
    using typename ParticleStructure<DataTypes, MemSpace>::kkLidView;
    using typename ParticleStructure<DataTypes, MemSpace>::kkGidView;
    using typename ParticleStructure<DataTypes, MemSpace>::kkLidHostMirror;
    using typename ParticleStructure<DataTypes, MemSpace>::kkGidHostMirror;
    using typename ParticleStructure<DataTypes, MemSpace>::MTVs;
    template<std::size_t N>
    using Slice = typename ParticleStructure<DataTypes, MemSpace>::Slice<N>;

    using host_space = Kokkos::HostSpace;
    typedef Kokkos::TeamPolicy<execution_space> PolicyType;
    typedef Kokkos::UnorderedMap<gid_t, lid_t, device_type> GID_Mapping;
    typedef DPS_Input<DataTypes, MemSpace> Input_T;

    using DPS_DT = PS_DTBool<DataTypes>;
    using AoSoA_t = Cabana::AoSoA<DPS_DT,device_type>;

    DPS() = delete;
    DPS(const DPS&) = delete;
    DPS& operator=(const DPS&) = delete;

    DPS( PolicyType& p,
          lid_t num_elements, lid_t num_particles,
          kkLidView particles_per_element,
          kkGidView element_gids,
          kkLidView particle_elements = kkLidView(),
          MTVs particle_info = NULL);
    DPS(DPS_Input<DataTypes, MemSpace>&);
    ~DPS();

    //Functions from ParticleStructure
    using ParticleStructure<DataTypes, MemSpace>::nElems;
    using ParticleStructure<DataTypes, MemSpace>::nPtcls;
    using ParticleStructure<DataTypes, MemSpace>::capacity;
    using ParticleStructure<DataTypes, MemSpace>::numRows;

    template <std::size_t N>
    Slice<N> get() { return Slice<N>(Cabana::slice<N, AoSoA_t>(*aosoa_, "get<>()")); }

    void migrate(kkLidView new_element, kkLidView new_process,
                 Distributor<MemSpace> dist = Distributor<MemSpace>(),
                 kkLidView new_particle_elements = kkLidView(),
                 MTVs new_particle_info = NULL);

    void rebuild(kkLidView new_element, kkLidView new_particle_elements = kkLidView(),
                 MTVs new_particles = NULL);

    template <typename FunctionType>
    void parallel_for(FunctionType& fn, std::string s="");

    void printMetrics() const;
    void printFormat(const char* prefix) const;

    // Do not call these functions:
    AoSoA_t* makeAoSoA(const lid_t capacity, const lid_t num_soa);
    void setNewActive(const lid_t num_particles);
    void createGlobalMapping(const kkGidView element_gids, kkGidView& lid_to_gid, GID_Mapping& gid_to_lid);
    void fillAoSoA(const kkLidView particle_elements, const MTVs particle_info, kkLidView& parentElms);
    void setParentElms(const kkLidView particles_per_element, kkLidView& parentElms);

  private:
    //The User defined Kokkos policy
    PolicyType policy;

    //Variables from ParticleStructure
    using ParticleStructure<DataTypes, MemSpace>::name;
    using ParticleStructure<DataTypes, MemSpace>::num_elems;
    using ParticleStructure<DataTypes, MemSpace>::num_ptcls;
    using ParticleStructure<DataTypes, MemSpace>::capacity_;
    using ParticleStructure<DataTypes, MemSpace>::num_rows;
    using ParticleStructure<DataTypes, MemSpace>::ptcl_data;
    using ParticleStructure<DataTypes, MemSpace>::num_types;
  
    // mappings from row to element gid and back to row
    kkGidView element_to_gid;
    GID_Mapping element_gid_to_lid;
    // number of SoA
    lid_t num_soa_;
    // percentage of capacity to add as padding
    double extra_padding;
    // parent elements for all particles in AoSoA
    kkLidView parentElms_;
    // particle data
    AoSoA_t* aosoa_;
  };

  /**
   * Constructor
   * @param[in] p
   * @param[in] num_elements number of elements
   * @param[in] num_particles number of particles
   * @param[in] particle_per_element view of ints, representing number of particles
   *    in each element
   * @param[in] element_gids view of ints, representing the global ids of each element
   * @param[in] particle_elements view of ints, representing which elements
   *    particle reside in (optional)
   * @param[in] particle_info array of views filled with particle data (optional)
   * @exception num_elements != particles_per_element.size(),
   *    undefined behavior for new_particle_elements.size() != sizeof(new_particles),
   *    undefined behavior for numberoftypes(new_particles) != numberoftypes(DataTypes)
  */
  template <class DataTypes, typename MemSpace>
  DPS<DataTypes, MemSpace>::DPS( PolicyType& p,
                                   lid_t num_elements, lid_t num_particles,
                                   kkLidView particles_per_element,
                                   kkGidView element_gids,
                                   kkLidView particle_elements, // optional
                                   MTVs particle_info) :        // optional
    ParticleStructure<DataTypes, MemSpace>(),
    policy(p),
    element_gid_to_lid(num_elements),
    extra_padding(0.05) // default extra padding at 5%
  {
    assert(num_elements == particles_per_element.size());
    num_elems = num_elements;
    num_rows = num_elems;
    num_ptcls = num_particles;
    int comm_rank;
    MPI_Comm_rank(MPI_COMM_WORLD, &comm_rank);
    if(!comm_rank)
      fprintf(stderr, "building DPS\n");

    // calculate num_soa_ from number of particles + extra padding
    num_soa_ = ceil(ceil(double(num_ptcls)/AoSoA_t::vector_length)*(1+extra_padding));
    // calculate capacity_ from num_soa_ and max size of an SoA
    capacity_ = num_soa_*AoSoA_t::vector_length;
    // initialize appropriately-sized AoSoA
    aosoa_ = makeAoSoA(capacity_, num_soa_);
    // set active mask
    setNewActive(num_ptcls);
    // get global ids
    if (element_gids.size() > 0)
      createGlobalMapping(element_gids, element_to_gid, element_gid_to_lid);
    // populate AoSoA with input data if given
    if (particle_elements.size() > 0 && particle_info != NULL) {
      if(!comm_rank) fprintf(stderr, "initializing DPS data\n");
      fillAoSoA(particle_elements, particle_info, parentElms_); // fill aosoa with data
    }
    else
      setParentElms(particles_per_element, parentElms_);
  }

  template <class DataTypes, typename MemSpace>
  DPS<DataTypes, MemSpace>::DPS(Input_T& input) :        // optional
    ParticleStructure<DataTypes, MemSpace>(input.name),
    policy(input.policy),
    element_gid_to_lid(input.ne)
  {
    num_elems = input.ne;
    num_rows = num_elems;
    num_ptcls = input.np;
    extra_padding = input.extra_padding;

    assert(num_elems == input.ppe.size());

    int comm_rank;
    MPI_Comm_rank(MPI_COMM_WORLD, &comm_rank);
    if(!comm_rank)
      fprintf(stderr, "building DPS\n");

    // calculate num_soa_ from number of particles + extra padding
    num_soa_ = ceil(ceil(double(num_ptcls)/AoSoA_t::vector_length)*(1+extra_padding));
    // calculate capacity_ from num_soa_ and max size of an SoA
    capacity_ = num_soa_*AoSoA_t::vector_length;
    // initialize appropriately-sized AoSoA
    aosoa_ = makeAoSoA(capacity_, num_soa_);
    // set active mask
    setNewActive(num_ptcls);
    // get global ids
    if (input.e_gids.size() > 0)
      createGlobalMapping(input.e_gids, element_to_gid, element_gid_to_lid);
    // populate AoSoA with input data if given
    if (input.particle_elms.size() > 0 && input.p_info != NULL) {
      if(!comm_rank) fprintf(stderr, "initializing DPS data\n");
      fillAoSoA(input.particle_elms, input.p_info, parentElms_); // fill aosoa with data
    }
    else
      setParentElms(input.ppe, parentElms_);
  }

  template <class DataTypes, typename MemSpace>
  DPS<DataTypes, MemSpace>::~DPS() { delete aosoa_; }

  /**
   * a parallel for-loop that iterates through all particles
   * @param[in] fn function of the form fn(elm, particle_id, mask), where
   *    elm is the element the particle is in
   *    particle_id is the overall index of the particle in the structure
   *    mask is 0 if the particle is inactive and 1 if the particle is active
   * @param[in] s string for labelling purposes
  */
  template <class DataTypes, typename MemSpace>
  template <typename FunctionType>
  void DPS<DataTypes, MemSpace>::parallel_for(FunctionType& fn, std::string s) {
    if (nPtcls() == 0)
      return;

    // move function pointer to GPU (if needed)
    FunctionType* fn_d;
    #ifdef PP_USE_CUDA
        cudaMalloc(&fn_d, sizeof(FunctionType));
        cudaMemcpy(fn_d,&fn, sizeof(FunctionType), cudaMemcpyHostToDevice);
    #else
        fn_d = &fn;
    #endif
    kkLidView parentElms_cpy = parentElms_;
    const auto soa_len = AoSoA_t::vector_length;
    const auto activeSliceIdx = aosoa_->number_of_members-1;
    const auto mask = Cabana::slice<activeSliceIdx>(*aosoa_); // get active mask
    Cabana::SimdPolicy<soa_len,execution_space> simd_policy(0, capacity_);
    Cabana::simd_parallel_for(simd_policy,
      KOKKOS_LAMBDA( const lid_t soa, const lid_t ptcl ) {
        const lid_t particle_id = soa*soa_len + ptcl; // calculate overall index
        const lid_t elm = parentElms_cpy(particle_id); // calculate element
        (*fn_d)(elm, particle_id, mask.access(soa,ptcl));
      }, "parallel_for");
  }

  template <class DataTypes, typename MemSpace>
  void DPS<DataTypes, MemSpace>::printMetrics() const {
    // Sum number of empty cells
    const auto activeSliceIdx = aosoa_->number_of_members-1;
    auto mask = Cabana::slice<activeSliceIdx>(*aosoa_);
    kkLidView padded_cells("num_padded_cells",1);
    Kokkos::parallel_for("count_padding", capacity_,
      KOKKOS_LAMBDA(const lid_t ptcl_id) {
        Kokkos::atomic_fetch_add(&padded_cells(0), !mask(ptcl_id));
      });
    lid_t num_padded = getLastValue<lid_t>(padded_cells);

    int comm_rank;
    MPI_Comm_rank(MPI_COMM_WORLD, &comm_rank);
    char buffer[1000];
    char* ptr = buffer;
    // Header
    ptr += sprintf(ptr, "Metrics (Rank %d)\n", comm_rank);
    // Sizes
    ptr += sprintf(ptr, "Number of Elements %d, Number of SoA %d, Number of Particles %d, Capacity %d\n",
                   num_elems, num_soa_, num_ptcls, capacity_);
    // Padded Cells
    ptr += sprintf(ptr, "Padded Cells <Tot %%> %d %.3f%%\n", num_padded,
                   num_padded * 100.0 / capacity_);
    printf("%s\n", buffer);
  }

  template <class DataTypes, typename MemSpace>
  void DPS<DataTypes, MemSpace>::printFormat(const char* prefix) const {
    fprintf(stderr, "[WARNING] printFormat not yet implemented!\n");
  }

}

// Separate files with DPS member function implementations
#include "dps_buildFns.hpp"
#include "dps_rebuild.hpp"
#include "dps_migrate.hpp"

#else
namespace pumipic {
  /*A dummy version of DPS when pumi-pic is built without Cabana so operations
    can compile without ifdef guards. The operations will report a message stating
    that the structure will not work.
  */
  template <class DataTypes, typename MemSpace = DefaultMemSpace>
  class DPS : public ParticleStructure<DataTypes, MemSpace> {
  public:
    using typename ParticleStructure<DataTypes, MemSpace>::execution_space;
    using typename ParticleStructure<DataTypes, MemSpace>::memory_space;
    using typename ParticleStructure<DataTypes, MemSpace>::device_type;
    using typename ParticleStructure<DataTypes, MemSpace>::kkLidView;
    using typename ParticleStructure<DataTypes, MemSpace>::kkGidView;
    using typename ParticleStructure<DataTypes, MemSpace>::kkLidHostMirror;
    using typename ParticleStructure<DataTypes, MemSpace>::kkGidHostMirror;
    using typename ParticleStructure<DataTypes, MemSpace>::MTVs;
    template<std::size_t N>
    using Slice = typename ParticleStructure<DataTypes, MemSpace>::Slice<N>;

    using host_space = Kokkos::HostSpace;
    typedef Kokkos::TeamPolicy<execution_space> PolicyType;
    typedef Kokkos::UnorderedMap<gid_t, lid_t, device_type> GID_Mapping;

    DPS() = delete;
    DPS(const DPS&) = delete;
    DPS& operator=(const DPS&) = delete;

    DPS( PolicyType& p,
          lid_t num_elements, lid_t num_particles,
          kkLidView particles_per_element,
          kkGidView element_gids,
          kkLidView particle_elements = kkLidView(),
          MTVs particle_info = NULL) {reportError();}
    ~DPS() {}

    //Functions from ParticleStructure
    using ParticleStructure<DataTypes, MemSpace>::nElems;
    using ParticleStructure<DataTypes, MemSpace>::nPtcls;
    using ParticleStructure<DataTypes, MemSpace>::capacity;
    using ParticleStructure<DataTypes, MemSpace>::numRows;

    template <std::size_t N>
    Slice<N> get() { reportError(); return Slice<N>();}

    void migrate(kkLidView new_element, kkLidView new_process,
                 Distributor<MemSpace> dist = Distributor<MemSpace>(),
                 kkLidView new_particle_elements = kkLidView(),
                 MTVs new_particle_info = NULL) {reportError();}

    void rebuild(kkLidView new_element, kkLidView new_particle_elements = kkLidView(),
                 MTVs new_particles = NULL) {reportError();}

    template <typename FunctionType>
    void parallel_for(FunctionType& fn, std::string s="") {reportError();}

    void printMetrics() const {reportError();}
    void printFormat(const char* prefix) const {reportError();}

  private:
    void reportError() const {fprintf(stderr, "[ERROR] pumi-pic was built "
                                      "without Cabana so the DPS structure "
                                      "can not be used\n");}
  };
}
#endif // PP_ENABLE_CAB