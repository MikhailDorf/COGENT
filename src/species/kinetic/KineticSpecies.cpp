#include "KineticSpecies.H"
#include "MomentOp.H"
#include "Misc.H"

#undef CH_SPACEDIM
#define CH_SPACEDIM CFG_DIM
#include "FluxSurface.H"
#include "FourthOrderUtil.H"
#include "MagGeom.H"
#include "MagBlockCoordSys.H"
#undef CH_SPACEDIM
#define CH_SPACEDIM PDIM

#include "NamespaceHeader.H"

KineticSpecies::KineticSpecies(
         const string&              a_name,
         const Real                 a_mass,
         const Real                 a_charge,
         const PhaseGeom&           a_geometry
   )
  : m_geometry( a_geometry ),
    m_name( a_name ),
    m_mass( a_mass ),
    m_charge( a_charge ),
    m_moment_op( MomentOp::instance() )
{
}


KineticSpecies::KineticSpecies( const KineticSpecies& a_foo )
  : m_geometry( a_foo.m_geometry ),
    m_name( a_foo.m_name ),
    m_mass( a_foo.m_mass ),
    m_charge( a_foo.m_charge ),
    m_moment_op( MomentOp::instance() )
{
   m_dist_func.define( a_foo.m_dist_func );
}

void KineticSpecies::numberDensity( CFG::LevelData<CFG::FArrayBox>& a_rho ) const
{
   m_moment_op.compute( a_rho, *this, DensityKernel() );
}


void KineticSpecies::massDensity( CFG::LevelData<CFG::FArrayBox>& a_rho ) const
{
   m_moment_op.compute( a_rho, *this, MassDensityKernel() );
}


void KineticSpecies::chargeDensity( CFG::LevelData<CFG::FArrayBox>& a_rho ) const
{
   m_moment_op.compute( a_rho, *this, ChargeDensityKernel() );
}


void KineticSpecies::momentumDensity( CFG::LevelData<CFG::FArrayBox>& a_momentum ) const
{
   m_moment_op.compute( a_momentum, *this, MomentumDensityKernel() );
}

void KineticSpecies::ParallelMomentum( CFG::LevelData<CFG::FArrayBox>& a_ParallelMom ) const
{
   m_moment_op.compute( a_ParallelMom, *this, ParallelMomKernel() );
}

void KineticSpecies::PoloidalMomentum( CFG::LevelData<CFG::FArrayBox>& a_PoloidalMom,
                                       const LevelData<FluxBox>& field,
                                       const double larmor  ) const 
{


   const CFG::MagGeom& mag_geom = m_geometry.magGeom();
   const CFG::MagCoordSys& coords = *mag_geom.getCoordSys();

   CFG::LevelData<CFG::FArrayBox> guidingCenterPoloidalMom;
   guidingCenterPoloidalMom.define(a_PoloidalMom); 
   m_moment_op.compute( guidingCenterPoloidalMom, *this, GuidingCenterPoloidalMomKernel(field) );
   
   CFG::LevelData<CFG::FArrayBox> magnetization;
   magnetization.define(a_PoloidalMom);
   m_moment_op.compute( magnetization, *this, MagnetizationKernel() );

   CFG::LevelData<CFG::FArrayBox> magnetization_grown(a_PoloidalMom.disjointBoxLayout(),1,
                                                      a_PoloidalMom.ghostVect()+2*CFG::IntVect::Unit);
   
   //Extrapolating magnetization moment into the two layers of ghost cells
   for (CFG::DataIterator dit(magnetization.dataIterator()); dit.ok(); ++dit) {
      int block_number( coords.whichBlock( mag_geom.gridsFull()[dit] ) );
      const CFG::MagBlockCoordSys& block_coord_sys = (const CFG::MagBlockCoordSys&)(*(coords.getCoordSys(block_number)));
      const CFG::ProblemDomain& block_domain = block_coord_sys.domain();
      magnetization_grown[dit].copy(magnetization[dit]);
      fourthOrderCellExtrapAtDomainBdry(magnetization_grown[dit], block_domain, mag_geom.gridsFull()[dit]);
   }
   mag_geom.fillInternalGhosts(magnetization_grown);
   
   //Compute radial component of the magnetization gradient (part of curlM calculation)
   CFG::LevelData<CFG::FArrayBox> gradM_mapped(a_PoloidalMom.disjointBoxLayout(), 2,
                                               a_PoloidalMom.ghostVect()+CFG::IntVect::Unit);
  
   mag_geom.computeMappedPoloidalGradientWithGhosts(magnetization_grown,gradM_mapped,2);
   
   CFG::LevelData<CFG::FArrayBox> gradM_phys;
   gradM_phys.define(gradM_mapped);
   mag_geom.unmapPoloidalGradient(gradM_mapped, gradM_phys);
   
   CFG::LevelData<CFG::FArrayBox> gradM_r(gradM_phys.disjointBoxLayout(),1, gradM_phys.ghostVect());
   mag_geom.computeRadialProjection(gradM_r, gradM_phys);
   
   //Get magnetic geometry data
   const CFG::LevelData<CFG::FArrayBox>& bunit = mag_geom.getCCBFieldDir();
   const CFG::LevelData<CFG::FArrayBox>& curlb = mag_geom.getCCCurlBFieldDir();
   CFG::LevelData<CFG::FArrayBox> curlb_pol(curlb.disjointBoxLayout(),1,curlb.ghostVect());
   mag_geom.computePoloidalProjection(curlb_pol, curlb);

   
   double fac = larmor / 2.0 / m_charge;

   for (CFG::DataIterator dit(a_PoloidalMom.dataIterator()); dit.ok(); ++dit) {
      
      //Compute curl of Magnetization vector
      curlb_pol[dit].mult(magnetization[dit]);
      curlb_pol[dit].mult(fac);
      gradM_r[dit].mult(bunit[dit],1,0,1);
      gradM_r[dit].mult(fac);
      
      //Add the poloidal component of the guiding center velocity and the curl of magnetization
      a_PoloidalMom[dit].copy(guidingCenterPoloidalMom[dit]);
      a_PoloidalMom[dit].plus(curlb_pol[dit]);
      a_PoloidalMom[dit].plus(gradM_r[dit]);
   }


}

void KineticSpecies::ParticleFlux( CFG::LevelData<CFG::FArrayBox>& a_ParticleFlux,
                                   const LevelData<FluxBox>& field  ) const

{
   m_moment_op.compute( a_ParticleFlux, *this, ParticleFluxKernel(field) );

   //Calculate flux average
   const CFG::MagGeom& mag_geom = m_geometry.magGeom();
   CFG::FluxSurface m_flux_surface(mag_geom, false);
   CFG::LevelData<CFG::FArrayBox> FluxAver_tmp;
   FluxAver_tmp.define(a_ParticleFlux);
   m_flux_surface.averageAndSpread(a_ParticleFlux, FluxAver_tmp);
   m_flux_surface.averageAndSpread(FluxAver_tmp,a_ParticleFlux);

}

void KineticSpecies::HeatFlux( CFG::LevelData<CFG::FArrayBox>& a_HeatFlux,
                                   const LevelData<FluxBox>& field,
                                   const LevelData<FArrayBox>& phi  ) const

{
   m_moment_op.compute( a_HeatFlux, *this, HeatFluxKernel(field, phi) );

   //Calculate flux average
   const CFG::MagGeom& mag_geom = m_geometry.magGeom();
   CFG::FluxSurface m_flux_surface(mag_geom, false);
   CFG::LevelData<CFG::FArrayBox> FluxAver_tmp;
   FluxAver_tmp.define(a_HeatFlux);
   m_flux_surface.averageAndSpread(a_HeatFlux, FluxAver_tmp);
   m_flux_surface.averageAndSpread(FluxAver_tmp,a_HeatFlux);

}

void KineticSpecies::parallelHeatFluxMoment( CFG::LevelData<CFG::FArrayBox>& a_parallelHeatFlux,
                                             CFG::LevelData<CFG::FArrayBox>& a_vparshift ) const
{
   m_moment_op.compute( a_parallelHeatFlux, *this, ParallelHeatFluxKernel(a_vparshift) );
}

void KineticSpecies::perpCurrentDensity( CFG::LevelData<CFG::FArrayBox>& a_PerpCurrentDensity,
                                         const LevelData<FluxBox>& field  ) const

{
   m_moment_op.compute( a_PerpCurrentDensity, *this, PerpCurrentDensityKernel(field) );
   
}

void KineticSpecies::pressureMoment( CFG::LevelData<CFG::FArrayBox>& a_pressure,
                                     CFG::LevelData<CFG::FArrayBox>& a_vparshift ) const
{
   m_moment_op.compute( a_pressure, *this, PressureKernel(a_vparshift) );
}

void KineticSpecies::fourthMoment( CFG::LevelData<CFG::FArrayBox>& a_fourth ) const
{
   m_moment_op.compute( a_fourth, *this, FourthMomentKernel() );
}

bool KineticSpecies::isSpecies( const string& a_name ) const
{
   if (m_name == a_name) return true;
   return false;
}


const KineticSpecies& KineticSpecies::operator=( const KineticSpecies& a_rhs )
{
   if (&a_rhs != this)
   {
      m_name = a_rhs.m_name;
      m_mass = a_rhs.m_mass;
      m_charge = a_rhs.m_charge;
      m_dist_func.define( a_rhs.m_dist_func );
   }
   return *this;
}


void KineticSpecies::copy( const KineticSpecies& a_rhs )
{
   if (&a_rhs != this)
   {
      m_name = a_rhs.m_name;
      m_mass = a_rhs.m_mass;
      m_charge = a_rhs.m_charge;

      DataIterator dit( m_dist_func.dataIterator() );
      for (dit.begin(); dit.ok(); ++dit) {
         m_dist_func[dit].copy( a_rhs.m_dist_func[dit] );
      }
   }
}


void KineticSpecies::zeroData()
{
   DataIterator dit( m_dist_func.dataIterator() );
   for (dit.begin(); dit.ok(); ++dit) {
      m_dist_func[dit].setVal( 0.0 );
   }
}


void KineticSpecies::addData( const KineticSpecies& a_rhs,
                              const Real a_factor )
{
   try {
      DataIterator dit( m_dist_func.dataIterator() );
      for (dit.begin(); dit.ok(); ++dit) {
         m_dist_func[dit].plus( a_rhs.m_dist_func[dit], a_factor );
      }
   }
   catch (std::bad_cast) {
      MayDay::Error( "Invalid SpeciesModel passed to KineticSpecies::addData!" );
   }
}

bool KineticSpecies::conformsTo( const KineticSpecies& a_rhs,
                                 const bool a_include_ghost_cells ) const
{
   try {
      const LevelData<FArrayBox>& thisData = m_dist_func;
      const LevelData<FArrayBox>& rhsData = a_rhs.m_dist_func;

      const DisjointBoxLayout& thisBoxes = thisData.disjointBoxLayout();
      const DisjointBoxLayout& rhsBoxes = rhsData.disjointBoxLayout();

      bool status( true );
      status &= thisBoxes.compatible( rhsBoxes );
      status &= ( thisData.nComp() == rhsData.nComp() );

      if ( a_include_ghost_cells) {
         status &= ( thisData.ghostVect() == rhsData.ghostVect() );
      }

      return status;
   }
   catch (std::bad_cast) {
      MayDay::Error( "Invalid SpeciesModel passed to KineticSpecies::comformsTo!" );
   }
   return false;
}

RefCountedPtr<KineticSpecies>
KineticSpecies::clone( const IntVect ghostVect,
                       const bool copy_soln_data ) const
{
   RefCountedPtr<KineticSpecies> result
      = RefCountedPtr<KineticSpecies>(
              new KineticSpecies( m_name, m_mass, m_charge, m_geometry ) );

   result->m_dist_func.define( m_dist_func.disjointBoxLayout(),
                               m_dist_func.nComp(),
                               ghostVect );

   if (copy_soln_data) {
      LevelData<FArrayBox>& result_dfn = result->m_dist_func;
      DataIterator dit( result_dfn.dataIterator() );
      for (dit.begin(); dit.ok(); ++dit) {
         result_dfn[dit].copy( m_dist_func[dit] );
      }
   }

   return result;
}


Real KineticSpecies::maxValue() const
{
   const DisjointBoxLayout& grids( m_dist_func.getBoxes() );
   Real local_maximum( -CH_BADVAL );
   DataIterator dit( m_dist_func.dataIterator() );
   for (dit.begin(); dit.ok(); ++dit) {
      Box box( grids[dit] );
      Real box_max( m_dist_func[dit].max( box ) );
      local_maximum = Max( local_maximum, box_max );
   }

   Real maximum( local_maximum );
#ifdef CH_MPI
   MPI_Allreduce( &local_maximum, &maximum, 1, MPI_CH_REAL, MPI_MAX, MPI_COMM_WORLD );
#endif

   return maximum;
}

Real KineticSpecies::minValue() const
{
   const DisjointBoxLayout& grids( m_dist_func.getBoxes() );
   Real local_minimum( CH_BADVAL );
   DataIterator dit( m_dist_func.dataIterator() );
   for (dit.begin(); dit.ok(); ++dit) {
      Box box( grids[dit] );
      Real box_min( m_dist_func[dit].min( box ) );
      if (box_min<0.0) {
         IntVect box_min_loc = m_dist_func[dit].minIndex( box );
         pout() << box_min_loc << ":  " << box_min << endl;
      }
      local_minimum = Min( local_minimum, box_min );
   }
   Real minimum(local_minimum);
#ifdef CH_MPI
   MPI_Allreduce( &local_minimum, &minimum, 1, MPI_CH_REAL, MPI_MIN, MPI_COMM_WORLD );
#endif
   return minimum;
}


void KineticSpecies::computeVelocity(
   LevelData<FluxBox>& a_velocity,
   const LevelData<FluxBox>& a_E_field ) const
{
   const DisjointBoxLayout& dbl( m_dist_func.getBoxes() );
   a_velocity.define( dbl, SpaceDim, IntVect::Unit );
   m_geometry.updateVelocities( a_E_field, a_velocity, true );
}


void KineticSpecies::computeMappedVelocity(
   LevelData<FluxBox>& a_velocity,
   const LevelData<FluxBox>& a_E_field ) const
{
   const DisjointBoxLayout& dbl( m_dist_func.getBoxes() );
   a_velocity.define( dbl, SpaceDim, IntVect::Unit );
   m_geometry.updateMappedVelocities( a_E_field, a_velocity );
}


#include "NamespaceFooter.H"
