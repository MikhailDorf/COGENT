#ifndef  _GKOPS_H_
#define  _GKOPS_H_

#undef BLOB_ERROR_TEST

#if CFG_DIM==2
#define CFG_NAMESPACE D2
#define PDIM 4
#endif
#if CFG_DIM==3
#define CFG_NAMESPACE D3
#define PDIM 5
#endif
#define VEL_DIM 2
#define VEL_NAMESPACE D2

#include "KineticSpecies.H"
#include "GKVlasov.H"

#include "GKCollisions.H"
#include "GKNeutrals.H"
#include "GKTransport.H"

#include "GKUnit.H"
#include "PhaseGeom.H"
#include "GKSystemIC.H"
#include "GKSystemBC.H"
#include "GlobalDOF.H"

#undef CH_SPACEDIM
#define CH_SPACEDIM VEL_DIM
#undef CH_SPACEDIM
#define CH_SPACEDIM PDIM

#undef CH_SPACEDIM
#define CH_SPACEDIM CFG_DIM
#include "MagGeom.H"
#include "BoltzmannElectron.H"
#include "NewGKPoissonBoltzmann.H"
#include "GKFluidOp.H"
#include "GKFieldOp.H"
#undef CH_SPACEDIM
#define CH_SPACEDIM PDIM

#include "ParmParse.H"
#include "Vector.H"
#include "CH_HDF5.H"
#include "RefCountedPtr.H"
#include "ProblemDomain.H"

#include "GKTimeIntegration.H"
#include "TimeIntegrator.H"
#include "TiRK.H"
#include "TiARK.H"

#include "NamespaceHeader.H"
namespace CFG = CFG_NAMESPACE;
namespace VEL = VEL_NAMESPACE;

/**
 * Gyrokinetic System class.
 *
 * A concrete implementation of the ODEInterface<Vector<SpeciesModel>>
 * interface, this class owns the simulation data and the Vlasov and Poisson
 * operator classes and orchestrates the time advancement.
 *
 * GKSystem implements parsing of some of the input data for gksystem.
 * Presently the list
 * for gksystem includes "verbosity", controlling the amount of diagnostic output,
 * "num_cells", the number of cells in the radial, poloidal, vparallel, and mu directions,
 * "is_periodic", an integer array of zeros and ones for periodicity in the coordinate
 * directions (same order). some parameters to control specification of what hdf5
 * files to write for plotting (see below), and "magneic_geometry_mapping", the name of the
 * magnetic geometry.   Specific parameters of the geometry are parsed in the geometry
 * implementation.  The plotting parameters are hdf_density (default true),
 * df_vpartheta (default false), and hdf_frtheta (default false), to control creation
 * of hdf5 files for density, f versus vparallel and theta,  and f versus r and theta, as well
 * as m_fixed_plotindices, an array of length 5 denoting the radial, poloidal, toroidal,
 * vparallel and mu indices
 *
 * Sample input:
 * \verbatim
 * gksystem.verbosity   = 1
 * gksystem.num_cells   = 32 128 4 4
 * gksystem.is_periodic =  0  1  0  0
 * gksystem.magnetic_geometry_mapping = "Miller"
 * gksystem.hdf_density = true
 * gkysytem.hdf_vpartheta = false
 * gkysytem.hdf_frtheta = true
 * gksystem.fixed_plot_indices = 3 1 0 2 2
 *
 */

class GKOps
{
public:

   /// Constructor with initialization.
   /**
    *  Pure virtual function that will be resposible for setting
    *  initial conditions on the soln data.
    *
    *  @param[in] pp the input database.
    */

   void initializeElectricField( const GKState& state, const int cur_step );

   inline
   void initializeState( GKState& a_state, const Real a_time )
   {
// JAFH: need to generalize ICs to all of state
      m_initial_conditions->initializeSystem( a_state, m_phi, a_time );
      enforceQuasiNeutrality( a_state.dataKinetic(), m_phi );
   }

  inline
   void initializePotential( const Real a_time )
   {
      m_initial_conditions->initializePotentialField( m_phi, a_time );
   }
   
   /// Destructor.
   /**
    */
   virtual ~GKOps();

   /// Returns the Vlasov time scale.
   /**
    * Returns the Vlasov time scale.
    */
   Real dtScaleVlasov( const GKState& state, const int step_number )
   {
      return m_time_scale_vlasov;
   }

   /// Returns the collisions time scale.
   /**
    * Returns the collisions time scale.
    */
   Real dtScaleCollisions( const GKState& a_state, const int step_number )
   {
      return m_time_scale_collisions;
   }

   /// Returns the transport time scale.
   /**
    * Returns the transport time scale.
    */
   Real dtScaleTransport( const GKState& a_state,  const int step_number )
   {
      return m_time_scale_transport;
   }

   /// Returns the time scale for the field components.
   /**
    * Returns the time scale for the field components.
    */
   Real dtScaleFields( const GKState& a_state,  const int step_number )
   {
      return m_time_scale_fields;
   }
   
   /// Returns the time scale for the fluid components.
   /**
    * Returns the time scale for the fluid components.
    */
   Real dtScaleFluids( const GKState& a_state,  const int step_number )
   {
      return m_time_scale_fluids;
   }
   
   /// Returns the neutrals time scale.
   /**
    * Returns the neutrals time scale.
    */
   Real dtScaleNeutrals( const GKState& a_state, const int step_number )
   {
      return m_time_scale_neutrals;
   }

   /// Compute a stable time step for explicit time integration
   /**
    * Computes and returns a stable time step estimate.
    */
   Real stableDtExpl( const GKState& a_state, const int step_number );

   /// Compute a stable time step for IMEX time integration
   /**
    * Computes and returns a stable time step estimate.
    */
   Real stableDtImEx( const GKState& a_state, const int step_number );

   /// Evaluates the RHS of the ODE.
   /**
    * Concrete implementation of pure virtual base class member function.
    * Given the current time and solution, evaluates the RHS of the ODE.
    *
    * @param[out] rhs data holder for rhs (without ghost cells).
    * @param[in] soln current solution with which to evaluate RHS.
    * @param[in] time current time.
    * @param[in] stage  stage number
    */
   
   GKOps() :
      m_vlasov(NULL),
      m_collisions(NULL),
      m_neutrals(NULL),
      m_transport(NULL),
      m_fieldOp(NULL),
      m_fluidOp(NULL),
      m_poisson(NULL),
      m_boltzmann_electron(NULL),
      m_units(NULL),
      m_initial_conditions(NULL),
      m_boundary_conditions(NULL),
      m_dt(0.0),
      m_dt_scale(1.0),
      m_is_defined(false),
      m_ghost_vect(4*IntVect::Unit),
      m_cfg_nghosts(2),
      m_fixed_efield(false),
      m_transport_model_on(false),     
      m_initializedE(false),
      m_enforce_quasineutrality(false),
      m_history(false),
      m_hist_freq(1),
      m_last_hist(0),
      m_hist_count(0),
      m_expand_incr(100),
      m_hist_fieldname("field"),
      m_dt_vlasov(DBL_MAX),
      m_dt_collisions(DBL_MAX),
      m_dt_transport(DBL_MAX),
      m_dt_fields(DBL_MAX),
      m_dt_fluids(DBL_MAX),
      m_dt_neutrals(DBL_MAX),
      m_time_scale_vlasov(-1),
      m_time_scale_collisions(-1),
      m_time_scale_transport(-1),
      m_time_scale_fields(-1),
      m_time_scale_fluids(-1),
      m_time_scale_neutrals(-1),
      m_count_vlasov(0),
      m_count_collision(0),
      m_count_transport(0),
      m_count_fields(0),
      m_count_fluids(0),
      m_count_neutrals(0),
      m_verbosity(0) {;}   
   
   void define( const GKState& a_state, const Real a_dt );
   
   bool isDefined() const { return m_is_defined; }
   
   void resetDt( const Real a_dt )
   {
      CH_assert( a_dt>0.0 );
      CH_assert( isDefined() );
      m_dt = a_dt;
   }

   void preTimeStep  (const int,const Real,const GKState&,const GKState&);
   void postTimeStep (const int,const Real,const GKState&);
   void postTimeStage(const int,const Real,const GKState&, const int);

   void setElectricField ( const Real a_time, const GKState& a_state );

   void explicitOp( GKRHSData& a_rhs,
                    const Real a_time,
                    const GKState& a_state,
                    const int a_stage = 1 );

   void explicitOpImEx( GKRHSData& a_rhs,
                        const Real a_time,
                        const GKState& a_state,
                        const int a_stage = 1 );

   void implicitOpImEx( GKRHSData& a_rhs,
                        const Real a_time,
                        const GKState& a_state,
                        const int a_stage = 1,
                        const int a_flag = 1);
   
   void explicitOp( GKRHSData& a_rhs,
                    const Real a_time,
                    const GKRHSData& a_state,
                    const int a_stage = 1 );

   void explicitOpImEx( GKRHSData& a_rhs,
                        const Real a_time,
                        const GKRHSData& a_state,
                        const int a_stage = 1 );

   void implicitOpImEx( GKRHSData& a_rhs,
                        const Real a_time,
                        const GKRHSData& a_state,
                        const int a_stage = 1,
                        const int a_flag = 1);

   bool setupPCImEx (void*,GKState&);
   bool setupPCImEx (void*,GKRHSData&);

   void assemblePCImEx( void*,const GKState& ) ;
   void assemblePCImEx( void*,const GKRHSData& );
   
   void computeElectricField( LevelData<FluxBox>&              a_E_field,
                              const KineticSpeciesPtrVect&     a_kinetic_species,
                              const CFG::FluidSpeciesPtrVect&  a_fluid_species,
                              const CFG::FieldPtrVect&         a_fields,
                              const int                        a_step_number );
    
   void divideJ( const KineticSpeciesPtrVect& soln_mapped,
                 KineticSpeciesPtrVect&       soln_physical );

   void divideJ( const CFG::FluidSpeciesPtrVect& soln_mapped,
                 CFG::FluidSpeciesPtrVect&       soln_physical );

   void divideJ( const CFG::FieldPtrVect& soln_mapped,
                 CFG::FieldPtrVect&       soln_physical );

   void divideJ( const GKState& soln_mapped, GKState& soln_physical );

   inline
   bool usingBoltzmannElectrons() const
   {
      return (m_boltzmann_electron!=NULL);
   };

   inline
   bool fixedEField() const
   {
      return m_fixed_efield;
   };

   inline
   bool usingAmpereLaw() const
   {
      return m_ampere_law;
   }

   double getLoRadialField() const {return m_Er_lo;}
   void setLoRadialField(double Er_lo) {m_Er_lo = Er_lo;}
   double getHiRadialField() const {return m_Er_hi;}
   void setHiRadialField(double Er_hi) {m_Er_hi = Er_hi;}

   const CFG::LevelData<CFG::FluxBox>& getErAverageFace() const {return m_Er_average_face;}
   const CFG::LevelData<CFG::FArrayBox>& getErAverageCell() const {return m_Er_average_cell;}
   const CFG::LevelData<CFG::FluxBox>& getETildeFace() const {return m_E_tilde_face;}
   const CFG::LevelData<CFG::FArrayBox>& getETildeCell() const {return m_E_tilde_cell;}

   void setErAverage(const LevelData<FluxBox>& Er_face_injected);
   void setErAverage(const LevelData<FArrayBox>& Er_cell_injected);
   void setETilde( const LevelData<FluxBox>& E_tilde_face_injected );
   void setETilde( const LevelData<FArrayBox>& E_tilde_cell_injected);
    
   void computePhiTilde( const KineticSpeciesPtrVect&          kinetic_species,
                         const CFG::BoltzmannElectron&         ne,
                         CFG::LevelData<CFG::FArrayBox>&       phi_tilde ) const; 

   // Updates FS-averaged E-field

   void updateAveragedEfield( CFG::LevelData<CFG::FluxBox>&   Er_average_face,
                              CFG::LevelData<CFG::FArrayBox>& Er_average_cell,
                              CFG::LevelData<CFG::FArrayBox>& flux_divergence,
                              double&                         flux_divergence_lo,
                              double&                         flux_divergence_hi,
                              const double                    dt ) const;

   // Updates E-field due to the presence of poloidal variation
   // Includes poloidal E-field and also correction to the averaged radial component

   void updateEfieldPoloidalVariation( const CFG::LevelData<CFG::FluxBox>&   E_tilde_mapped_face,
                                       const CFG::LevelData<CFG::FArrayBox>& E_tilde_mapped_cell,
                                       CFG::LevelData<CFG::FluxBox>&         E_tilde_phys_face,
                                       CFG::LevelData<CFG::FArrayBox>&       E_tilde_phys_cell,
                                       CFG::LevelData<CFG::FArrayBox>&       a_phi_tilde_fs_average,
                                       double&                               a_phi_tilde_fs_average_lo,
                                       double&                               a_phi_tilde_fs_average_hi ) const;
    
   void plotDistributionFunction( const std::string&    filename,
                                  const KineticSpecies& soln_species,
                                  const double&         time ) const;

   void plotFluids( const std::string&       filename,
                    const CFG::FluidSpecies& fluid_species,
                    const double&            time ) const;

   void plotFields( const std::string&    filename,
                    const CFG::Field&     soln_species,
                    const double&         time ) const;

   void plotDeltaF( const std::string&    filename,
                    const KineticSpecies& soln_species,
                    const double&         time ) const;

   void plotDistributionFunctionAtMu( const std::string&    filename,
                                      const KineticSpecies& soln_species,
                                      const int             mu,
                                      const double&         time ) const;

   void plotBStarParallel( const std::string&    filename,
                           const KineticSpecies& soln_species,
                           const double&         time ) const;

   void plotVParallelTheta( const std::string&    filename,
                            const KineticSpecies& soln_species,
                            const int             radial_index,
                            const int             toroidal_index,
                            const int             mu_index,
                            const double&         time ) const;

   void plotRTheta( const std::string&    filename,
                    const KineticSpecies& soln_species,
                    const int             vpar_index,
                    const int             mu_index,
                    const double&         time ) const;

   void plotVParallelMu( const std::string&    filename,
                         const KineticSpecies& soln_species,
                         const int             radial_index,
                         const int             poloidal_index,
                         const double&         time ) const;

   void plotChargeDensity( const std::string&    filename,
                           const KineticSpecies& soln_species,
                           const double&         time ) const;

   void plotChargeDensity( const std::string&     filename,
                           const KineticSpeciesPtrVect& species,
                           const double&          time ) const;

   void plotParallelMomentum( const std::string&    filename,
                              const KineticSpecies& soln_species,
                              const double&         time ) const;

   void plotPoloidalMomentum( const std::string&    filename,
                              const KineticSpecies& kinetic_species,
                              const double&         time ) const;
   
   void plotPressure( const std::string&    filename,
                      const KineticSpecies& soln_species,
                      const double&         time ) const;
   
   void plotParallelHeatFlux( const std::string&    filename,
                              const KineticSpecies& soln_species,
                              const double&         time ) const;
   

   void plotTemperature( const std::string&    filename,
                         const KineticSpecies& soln_species,
                         const double&         time ) const;

   void plotFourthMoment( const std::string&    filename,
                          const KineticSpecies& soln_species,
                          const double&         time ) const;

   void plotParticleFlux( const std::string&    filename,
                          const KineticSpecies& soln_species,
                          const double&         time ) const;

   void plotHeatFlux( const std::string&    filename,
                      const KineticSpecies& soln_species,
                      const double&         time ) const;

   void plotPotential( const std::string& filename,
                       const double&      time ) const;

   void plotEField( const std::string& filename,
                    const double&      time ) const;

   void plotVlasovDivergence( const std::string&    filename,
                              const KineticSpecies& kinetic_species,
                              const double&         time );

   void plotAmpereErIncrement( const std::string&   filename,
			       const KineticSpecies& kinetic_species,
			       const double&         time ) const;

   void setupFieldHistories( ParmParse& a_ppsim );

   void writeFieldHistory( int cur_step, double cur_time, bool startup_flag );
  
      /// Write checkpoint file.
      /**
       * Write checkpoint data to an output HDF5 file.
       *
       * @param[in] handle a reference to an HDF5 plot file.
       */
   void writeCheckpointFile( HDF5Handle& handle ) const;
   
      /// Read checkpoint file.
      /**
       * Read checkpoint data from an output HDF5 file and reinitialize.
       *
       * @param[in] handle a reference to an HDF5 plot file.
       */
   void readCheckpointFile( HDF5Handle& handle, const int& cur_step  );

   inline bool isLinear() { return(m_collisions->isLinear()); }

   void printFunctionCounts()
   {
    if (!procID()) {
      cout << "  Function counts:-\n";
      cout << "    Vlasov     : " << m_count_vlasov     << "\n";
      cout << "    Collision  : " << m_count_collision  << "\n";
      cout << "    Transport  : " << m_count_transport  << "\n";
      cout << "    Fields     : " << m_count_fields     << "\n";
      cout << "    Fluids     : " << m_count_fluids     << "\n";
      cout << "    Neutrals   : " << m_count_neutrals   << "\n";
    }
   }

private:
   
   /// Parse parameters.
   /**
    * Private method to obtain control parameters from "gksystem" section
    * of ParmParse database.
    *
    * \param[in] ppgksys gksystem input database.
    */
   void parseParameters( ParmParse& ppgksys );

   void createGKPoisson( const CFG::LevelData<CFG::FArrayBox>& initial_ion_charge_density );

   void createTemporarySpeciesVector( KineticSpeciesPtrVect& out,
                                      const KineticSpeciesPtrVect& in );
   
   void createTemporarySpeciesVector( CFG::FluidSpeciesPtrVect& out,
                                      const CFG::FluidSpeciesPtrVect& in );

   void createTemporaryFieldVector( CFG::FieldPtrVect& out,
                                      const CFG::FieldPtrVect& in );

   void createTemporaryState( GKState& out, const GKState& in );

   // JAFH: To be deprecated
   void fillGhostCells( KineticSpeciesPtrVect&       species_phys,
                        const LevelData<FluxBox>&    E_field,
                        const Real&                  time );

   void fillGhostCells( GKState&                  state_phys,
                        const LevelData<FluxBox>& E_field,
                        const Real&               time );

   void applyVlasovOperator( KineticSpeciesPtrVect&       rhs,
                             const KineticSpeciesPtrVect& soln,
                             const LevelData<FluxBox>&    E_field,
                             const Real&                  time);

   void applyCollisionOperator( KineticSpeciesPtrVect&       rhs,
                                const KineticSpeciesPtrVect& soln,
                                const Real&                  time,
                                const int                    flag = 1);

   void applyNeutralsOperator(   KineticSpeciesPtrVect&      rhs,
                                const KineticSpeciesPtrVect& soln,
                                const Real&                  time );
    

   void applyTransportOperator( KineticSpeciesPtrVect&       rhs,
                                const KineticSpeciesPtrVect& soln,
                                const Real&                  time );

   void applyFieldOperator( CFG::FieldPtrVect&                   rhs,
                            const CFG::FieldPtrVect&             fields,
                            const CFG::FluidSpeciesPtrVect&      fluids,
                            const KineticSpeciesPtrVect&         soln,
                            const CFG::LevelData<CFG::FluxBox>&  E_field,
                            const Real&                          time);

   void applyFluidOperator( CFG::FluidSpeciesPtrVect&            rhs,
                            const CFG::FieldPtrVect&             fields,
                            const CFG::FluidSpeciesPtrVect&      fluids,
                            const KineticSpeciesPtrVect&         soln,
                            const CFG::LevelData<CFG::FluxBox>&  E_field,
                            const Real&                          time);
   
   /// Compute the total charge density
   /**
    * Compute the total charge density 
    *
    * @param[out] charge_density     Charge density
    * @param[in]  species            Species vector
    */
   void computeTotalChargeDensity( CFG::LevelData<CFG::FArrayBox>& charge_density,
                                   const KineticSpeciesPtrVect&    species ) const;

   /// Compute the ion charge density
   /**
    * Compute the ion charge density 
    *
    * @param[out] ion_charge_density     Ion charge density
    * @param[in]  species            Species vector
    */
   void computeIonChargeDensity( CFG::LevelData<CFG::FArrayBox>& ion_charge_density,
                                   const KineticSpeciesPtrVect&  species ) const;

   /// Compute the ion parallel curent density
   /**
    * Compute the ion parallel curent density 
    *
    * @param[out] ion_parallel_current_density     Ion charge density
    * @param[in]  species            Species vector
    */
   void computeIonParallelCurrentDensity( CFG::LevelData<CFG::FArrayBox>& ion_current_density,
                                          const KineticSpeciesPtrVect&  species ) const;

   /// Compute the signed charge densities
   /**
    * Compute the signed charge densities
    *
    * @param[out] pos_charge_density    Ion charge density
    * @param[out] neg_charge_density    Electron charge density
    * @param[in]  species               Species vector
    */

   void computeSignedChargeDensities( CFG::LevelData<CFG::FArrayBox>& pos_charge_density,
                                      CFG::LevelData<CFG::FArrayBox>& neg_charge_density,
                                      const KineticSpeciesPtrVect&    species ) const;

   /// Compute the ion mass density
   /**
    * Compute the ion mass density 
    *
    * @param[out] mass_density     Mass density
    * @param[in]  species          Species vector
    */
   void computeIonMassDensity( CFG::LevelData<CFG::FArrayBox>& mass_density,
                              const KineticSpeciesPtrVect& species ) const;

   void enforceQuasiNeutrality( KineticSpeciesPtrVect&          species,
                                CFG::LevelData<CFG::FArrayBox>& potential ) const;
   
   inline
   void computeQuasiNeutralElectronDensity(
      CFG::LevelData<CFG::FArrayBox>&       a_quasineutral_density,
      CFG::LevelData<CFG::FArrayBox>&       a_potential,
      const CFG::PotentialBC&               a_bc, 
      const CFG::LevelData<CFG::FArrayBox>& a_ion_density) const;
   
   inline
   void renormalizeElectronDistributionFunction(
      KineticSpecies&                 a_electron_species,
      CFG::LevelData<CFG::FArrayBox>& a_initial_density,
      CFG::LevelData<CFG::FArrayBox>& a_quasineutral_density ) const;

   void computeEField( CFG::LevelData<CFG::FluxBox>&     E_field_face,
                       CFG::LevelData<CFG::FArrayBox>&   E_field_cell,
                       const KineticSpeciesPtrVect&      soln_kinetic,
                       const CFG::FluidSpeciesPtrVect&   soln_fluid,
                       const CFG::FieldPtrVect&          soln_field,
                       const int                         step_number );
   
   void setCoreBC( const double      core_inner_bv,
                   const double      core_outer_bv,
                   CFG::PotentialBC& bc ) const;

   GKVlasov*                      m_vlasov;
   GKCollisions*                  m_collisions;
   GKNeutrals*                    m_neutrals;
   GKTransport*                   m_transport;
   CFG::GKFieldOp*                m_fieldOp;
   CFG::GKFluidOp*                m_fluidOp;
   CFG::GKPoisson*                m_poisson;
   
   CFG::LevelData<CFG::FluxBox>   m_E_field_face;
   CFG::LevelData<CFG::FArrayBox> m_E_field_cell;
   CFG::LevelData<CFG::FArrayBox> m_phi;
   LevelData<FluxBox> m_E_field;

   CFG::BoltzmannElectron*             m_boltzmann_electron;
   double m_lo_radial_flux_divergence_average;
   double m_hi_radial_flux_divergence_average;
   double m_lo_radial_gkp_divergence_average;
   double m_hi_radial_gkp_divergence_average;
   double m_Er_lo;
   double m_Er_hi;
   double m_stage0_time;
    
   CFG::LevelData<CFG::FluxBox>     m_Er_average_face;
   CFG::LevelData<CFG::FArrayBox>   m_Er_average_cell;
   CFG::LevelData<CFG::FluxBox>     m_E_tilde_face;
   CFG::LevelData<CFG::FArrayBox>   m_E_tilde_cell;
    
   CFG::LevelData<CFG::FArrayBox>   m_radial_gkp_divergence_average;
   CFG::LevelData<CFG::FArrayBox>   m_radial_flux_divergence_average;

   const GKUnits*                 m_units;
   const GKSystemIC*              m_initial_conditions;
   const GKSystemBC*              m_boundary_conditions;
   RefCountedPtr<PhaseGeom>       m_phase_geometry;

   Real m_dt;
   Real m_dt_scale;
   bool m_is_defined;
   
   IntVect                        m_ghost_vect;
   const CFG::IntVect             m_cfg_nghosts;
      
   bool                           m_consistent_potential_bcs;
   bool                           m_ampere_law;
   bool                           m_ampere_cold_electrons;
   bool                           m_fixed_efield;
   bool                           m_transport_model_on;
   bool                           m_neutrals_model_on;
   bool                           m_initializedE;
   bool                           m_enforce_quasineutrality;
   bool                           m_Esol_extrapolation;
   bool                           m_dealignment_corrections;
   bool                           m_vorticity_model;
   
   GKState                        m_Y;

   bool m_history;   // whether to write out histories
   int m_hist_freq;  // how often to write out field history
   int m_last_hist;  // moribund, not used
   int m_hist_count; // how many watchpoints
   int m_expand_incr; // expansion increment for vectors in FieldHist
   CFG::IntVect m_hist_indices; // for indexing watchpoints, now a temporary
   std::string m_hist_fieldname; // type of field, e.g. "potential", now a temporary

   /* stable time step sizes and time scales */
   Real m_dt_vlasov;
   Real m_dt_collisions;
   Real m_dt_transport;
   Real m_dt_fields;
   Real m_dt_fluids;
   Real m_dt_neutrals;
   Real m_time_scale_vlasov;
   Real m_time_scale_collisions;
   Real m_time_scale_transport;
   Real m_time_scale_fields;
   Real m_time_scale_fluids;
   Real m_time_scale_neutrals;

   // stores data pertaining to a watchpoint history
   typedef struct {
      int hist_index; // numerical index of this record, zero-based
      CFG::IntVect grid_indices; // location of watchpoint in grid
      Vector<Real> *fieldvals; // field value, e.g., potential
      Vector<Real> *timevals; // time corresponding to above
      Vector<int> *timestep; // timestep corresponding to above
      int valsize; // length of above three fields
      int cur_entry; //index into fieldvars and timevals
      string fieldname; // e.g., "potential"
   } FieldHist;
   
   Vector<FieldHist> m_fieldHistLists; // for saving all histories internally
   
   /* Function counters */
   int m_count_vlasov;
   int m_count_collision;
   int m_count_transport;
   int m_count_fields;
   int m_count_fluids;
   int m_count_neutrals;

   int m_verbosity;

};

#include "NamespaceFooter.H"

#endif
