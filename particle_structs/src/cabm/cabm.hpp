#pragma once

#include <Cabana_Core.hpp>
#include <particle_structs.hpp>
#include "cabm_support.hpp"

namespace {
  // class to append member types
  template <typename T, typename... Types>
  struct MemberTypesAppend;

  //Append type to the end
  template <typename T, typename... Types>
  struct MemberTypesAppend<T, Cabana::MemberTypes<Types...> > {
    static constexpr int size = 1 + Cabana::MemberTypes<Types...>::size;
    using type = Cabana::MemberTypes<Types..., T>; //Put T before Types... to put at beginning
  };

}

namespace pumipic {

  template <class DataTypes, typename MemSpace = DefaultMemSpace>
  class CabM : public ParticleStructure<DataTypes, MemSpace> {
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

    //from https://github.com/SCOREC/Cabana/blob/53ad18a030f19e0956fd0cab77f62a9670f31941/core/src/CabanaM.hpp#L18-L19
    using CM_DT = CM_DTInt<DataTypes>;
    using AoSoA_t = Cabana::AoSoA<CM_DT,device_type>;

    CabM() = delete;
    CabM(const CabM&) = delete;
    CabM& operator=(const CabM&) = delete;

    CabM( PolicyType& p,
          lid_t num_elements, lid_t num_particles,
          kkLidView particles_per_element,
          kkGidView element_gids,
          kkLidView particle_elements = kkLidView(),
          MTVs particle_info = NULL);
    ~CabM();

    //Functions from ParticleStructure
    using ParticleStructure<DataTypes, MemSpace>::nElems;
    using ParticleStructure<DataTypes, MemSpace>::nPtcls;
    using ParticleStructure<DataTypes, MemSpace>::capacity;
    using ParticleStructure<DataTypes, MemSpace>::numRows;

    template <std::size_t N>
    Slice<N> get() { return Slice<N>(Cabana::slice<N, AoSoA_t>(aosoa_, "get<>()")); }

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
    kkLidView buildOffset(const kkLidView particles_per_element);
    AoSoA_t makeAoSoA(const lid_t capacity, const lid_t num_soa);
    kkLidView getParentElms( const lid_t num_elements, const lid_t num_soa, const kkLidView offsets );
    void setActive(AoSoA_t &aosoa, const kkLidView particles_per_element,
      const kkLidView parentElms, const kkLidView offsets);
    void createGlobalMapping(kkGidView element_gids, kkGidView& lid_to_gid, GID_Mapping& gid_to_lid);
    void fillAoSoA(kkLidView particle_indices, kkLidView particle_elements, MTVs particle_info);
    void initCabMData(kkLidView particle_elements, MTVs particle_info);

  private:
    //The User defined Kokkos policy
    PolicyType policy;

    //Variables from ParticleStructure
    using ParticleStructure<DataTypes, MemSpace>::num_elems;
    using ParticleStructure<DataTypes, MemSpace>::num_ptcls;
    using ParticleStructure<DataTypes, MemSpace>::capacity_;
    using ParticleStructure<DataTypes, MemSpace>::num_rows;
    using ParticleStructure<DataTypes, MemSpace>::ptcl_data;
    using ParticleStructure<DataTypes, MemSpace>::num_types;

    //mappings from row to element gid and back to row
    kkGidView element_to_gid;
    GID_Mapping element_gid_to_lid;
    lid_t num_soa_; // number of SoA
    kkLidView offsets; // Offsets array into CabM
    kkLidView parentElms_; // Parent elements for each SoA
    AoSoA_t aosoa_;
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
  CabM<DataTypes, MemSpace>::CabM( PolicyType& p,
                                   lid_t num_elements, lid_t num_particles,
                                   kkLidView particles_per_element,
                                   kkGidView element_gids,
                                   kkLidView particle_elements,
                                   MTVs particle_info) :        // optional
    ParticleStructure<DataTypes, MemSpace>(),
    policy(p),
    element_gid_to_lid(num_elements)
  {
    assert(num_elements == particles_per_element.size());
    num_elems = num_elements;
    num_rows = num_elems;
    num_ptcls = num_particles;
    int comm_rank;
    MPI_Comm_rank(MPI_COMM_WORLD, &comm_rank);
    if(!comm_rank)
      fprintf(stderr, "building CabM\n");
    
    // build view of offsets for SoA indices within particle elements
    offsets = buildOffset(particles_per_element);
    // set num_soa_ from the last entry of offsets
    num_soa_ = getLastValue(offsets);
    // calculate capacity_ from num_soa_ and max size of an SoA
    capacity_ = num_soa_*AoSoA_t::vector_length;
    // initialize appropriately-sized AoSoA
    aosoa_ = makeAoSoA(capacity_, num_soa_);
    // get array of parents element indices for particles
    parentElms_ = getParentElms(num_elems, num_soa_, offsets);
    // set active mask
    setActive(aosoa_, particles_per_element, parentElms_, offsets);
    // get global ids
    if (element_gids.size() > 0) {
      createGlobalMapping(element_gids, element_to_gid, element_gid_to_lid);
    }
    // populate AoSoA with input data if given
    lid_t given_particles = particle_elements.size();
    if (given_particles > 0 && particle_info != NULL) {
      if(!comm_rank) fprintf(stderr, "initializing CabM data\n");
      initCabMData(particle_elements, particle_info); // initialize data
    }
  }

  template <class DataTypes, typename MemSpace>
  CabM<DataTypes, MemSpace>::~CabM() { }

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
  void CabM<DataTypes, MemSpace>::parallel_for(FunctionType& fn, std::string s) {
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
    const auto activeSliceIdx = aosoa_.number_of_members-1;
    const auto mask = Cabana::slice<activeSliceIdx>(aosoa_); // get active mask
    Cabana::SimdPolicy<soa_len,execution_space> simd_policy( 0, capacity_);
    Cabana::simd_parallel_for(simd_policy,
      KOKKOS_LAMBDA( const lid_t soa, const lid_t ptcl ) {
        const lid_t elm = parentElms_cpy(soa); // calculate element
        const lid_t particle_id = soa*soa_len + ptcl; // calculate overall index
        (*fn_d)(elm, particle_id, mask.access(soa,ptcl));
      }, "parallel_for");
  }

  template <class DataTypes, typename MemSpace>
  void CabM<DataTypes, MemSpace>::printMetrics() const {
    // Sum number of empty cells
    const auto activeSliceIdx = aosoa_.number_of_members-1;
    auto mask = Cabana::slice<activeSliceIdx>(aosoa_);
    kkLidView padded_cells("num_padded_cells",1);
    Kokkos::parallel_for("count_padding", capacity_,
      KOKKOS_LAMBDA(const lid_t ptcl_id) {
        Kokkos::atomic_fetch_add(&padded_cells(0), !mask(ptcl_id));
      });
    // Sum number of empty elements
    kkLidHostMirror offsets_host = deviceToHost(offsets);
    lid_t num_empty_elements = 0;
    if (num_soa_ == 0)
      num_empty_elements = num_elems;
    else {
      for (int i = 0; i < num_elems; i++) {
        if (i != 0 && (offsets_host(i) == offsets_host(i-1)) )
          num_empty_elements++;
      }
    }

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
    // Empty Elements
    ptr += sprintf(ptr, "Empty Elements <Tot %%> %d %.3f%%\n", num_empty_elements,
                   num_empty_elements * 100.0 / num_elems);

    printf("%s\n", buffer);
  }

  template <class DataTypes, typename MemSpace>
  void CabM<DataTypes, MemSpace>::printFormat(const char* prefix) const {
    kkGidHostMirror element_to_gid_host = deviceToHost(element_to_gid);
    kkLidHostMirror offsets_host = deviceToHost(offsets);
    kkLidHostMirror parents_host = deviceToHost(parentElms_);
    const auto soa_len = AoSoA_t::vector_length;

    kkLidView mask(Kokkos::ViewAllocateWithoutInitializing("offsets_host"), capacity_);
    const auto activeSliceIdx = aosoa_.number_of_members-1;
    auto mask_slice = Cabana::slice<activeSliceIdx>(aosoa_);
    Kokkos::parallel_for("copy_mask", capacity_,
      KOKKOS_LAMBDA(const lid_t ptcl_id) {
        mask(ptcl_id) = mask_slice(ptcl_id);
      });
    kkLidHostMirror mask_host = deviceToHost(mask);

    char buffer[10000];
    char* ptr = buffer;
    ptr += sprintf(ptr, "%s\n", prefix);
    ptr += sprintf(ptr,"Particle Structures CabM\n");
    ptr += sprintf(ptr,"Number of Elements: %d.\nNumber of SoA: %d.\nNumber of Particles: %d.", num_elems, num_soa_, num_ptcls);
    lid_t last_soa = -1;
    lid_t last_elm = -1;
    for (int i = 0; i < capacity_; i++) {
      lid_t soa = i / soa_len;
      lid_t elm = parents_host(soa);
      if (last_soa == soa)
        ptr += sprintf(ptr," %d", mask_host(i));
      else {
        if (last_elm != elm)
          ptr += sprintf(ptr,"\n  Element %d(%d) | %d", elm, element_to_gid_host(elm), mask_host(i));
        else
          ptr += sprintf(ptr,"\n                 | %d", mask_host(i));
      }

      last_soa = soa;
      last_elm = elm;
    }
    printf("%s\n", buffer);
  }

}

// Separate files with CabM member function implementations
#include "cabm_buildFns.hpp"
#include "cabm_rebuild.hpp"
#include "cabm_migrate.hpp"