#include "GKPoissonBoltzmann.H"
#include "SingleNullBlockCoordSys.H"
#include "SingleNullCoordSys.H"


#include "Directions.H"
#include "AnnulusPotentialBC.H"
#include "SlabPotentialBC.H"
#include "SNCorePotentialBC.H"
#include "newMappedGridIO.H"

// Experimental preconditioner option.  Matches the design document when defined.
#define DIVIDE_M

#include "NamespaceHeader.H"

extern "C" {
   void dgtsv_(const int&, const int &, double *, double *, double *, double *, const int &, int &);
   void dgesv_(const int&, const int &, double *, const int &, int *, double *, const int &, int &);
}


const char* GKPoissonBoltzmann::pp_name = {"gkpoissonboltzmann"};

GKPoissonBoltzmann::GKPoissonBoltzmann( ParmParse&                  a_pp,
                                        const MagGeom&              a_geom,
                                        const Real                  a_larmor_number,
                                        const Real                  a_debye_number,
                                        const LevelData<FArrayBox>& a_initial_ion_charge_density )
   : GKPoisson(a_pp, a_geom, a_larmor_number, a_debye_number),
     m_nonlinear_relative_tolerance(1.e-5),
     m_nonlinear_change_tolerance(1.e-5),
     m_nonlinear_max_iterations(5),
     m_jacobian_solve_tolerance(1.e-4),
     m_flux_surface(a_geom),
     m_precond_Psolver(a_geom,2),
     m_precond_Qsolver(a_geom,2),
     m_Zni_outer_plate(NULL),
     m_Zni_inner_plate(NULL),
     m_phi_outer_plate(NULL),
     m_phi_inner_plate(NULL)
{
   //CH_assert(m_geometry.getBlockCoordSys(0).geometryType() == "Miller");

   // Read input

   if (a_pp.contains("verbose")) {
      a_pp.get("verbose", m_verbose);
   }
   else {
      m_verbose = false;
   }

   // Determine the form of the Boltzmann prefactor if specified in the input;
   // otherwise, assume global neutrality
   if (a_pp.contains("prefactor")) {
      string prefactor;
      a_pp.get("prefactor", prefactor);

      if( prefactor == "global_neutrality" ) {
         MayDay::Error("GKPoissonBoltzmann: GLOBAL_NEUTRALITY option is currently disabled");
         m_prefactor_strategy = GLOBAL_NEUTRALITY;
      }
      else if( prefactor == "global_neutrality_initial" ) {
         MayDay::Error("GKPoissonBoltzmann: GLOBAL_NEUTRALITY_INITIAL option is currently disabled");
         m_prefactor_strategy = GLOBAL_NEUTRALITY_INITIAL;
      }
      else if ( prefactor == "fs_neutrality" ) {
         m_prefactor_strategy = FS_NEUTRALITY;
      }
      else if ( prefactor == "fs_neutrality_global_ni" ) {
         m_prefactor_strategy = FS_NEUTRALITY_GLOBAL_NI;
      }
      else if ( prefactor == "fs_neutrality_initial_global_ni" ) {
         m_prefactor_strategy = FS_NEUTRALITY_INITIAL_GLOBAL_NI;
      }
      else if ( prefactor == "fs_neutrality_initial_fs_ni" ) {
         m_prefactor_strategy = FS_NEUTRALITY_INITIAL_FS_NI;
      }
      else {
         MayDay::Error( "gkpoissonboltzmann.prefactor must be one of: ""global_neutrality"", ""global_neutrality_initial"", ""fs_neutrality"", ""fs_neutrality_initial"", ""fs_neutrality_global_ni"", ""fs_neutrality_initial_global_ni"" or ""fs_neutrality_initial_fs_ni""" );
      }
   }
   else {
      m_prefactor_strategy = GLOBAL_NEUTRALITY;
   }

#if 0
   m_recompute_prefactor = !( (m_prefactor_strategy == GLOBAL_NEUTRALITY_INITIAL )
                              && !a_first_step);
#else
   m_recompute_prefactor = true;
#endif

   if (a_pp.contains("nonlinear_relative_tolerance")) {
      a_pp.get("nonlinear_relative_tolerance", m_nonlinear_relative_tolerance);
   }

   if (a_pp.contains("nonlinear_change_tolerance")) {
      a_pp.get("nonlinear_change_tolerance", m_nonlinear_change_tolerance);
   }

   if (a_pp.contains("nonlinear_maximum_iterations")) {
      a_pp.get("nonlinear_maximum_iterations", m_nonlinear_max_iterations);
   }

   if (a_pp.contains("jacobian_solve_tolerance")) {
      a_pp.get("jacobian_solve_tolerance", m_jacobian_solve_tolerance);
   }

   if (m_verbose && procID()==0) {
      cout << "GKPoissonAdiabaticElectron parameters:" << endl;
      cout << "   Debye number squared = "<< m_debye_number2 << endl;
      cout << "   Larmor number squared = " << m_larmor_number2 << endl;
      cout << "   prefactor_strategy = " << m_prefactor_strategy << endl;
      cout << "   nonlinear_relative_tolerance = " << m_nonlinear_relative_tolerance << endl;
      cout << "   nonlinear_maximum_iterations = " << m_nonlinear_max_iterations << endl;
   }

   // This is an algorithm tweak to address problems with constant null spaces resulting
   // from periodic and/or homogeneous Neumann boundary conditions
   if (a_pp.contains("preserve_initial_ni_average")) {
      a_pp.get("preserve_initial_ni_average", m_preserve_initial_ni_average);
   }
   else {
      m_preserve_initial_ni_average = false;
   }

   if ( a_pp.contains("radial_solve_only") ) {
      a_pp.get("radial_solve_only", m_radial_solve_only);
   }
   else {
      m_radial_solve_only = false;
   }

   if ( a_pp.contains("subspace_iteration_solve") ) {
      a_pp.get("subspace_iteration_solve", m_subspace_iteration_solve);

      if ( a_pp.contains("max_subspace_iterations") ) {
         a_pp.get("max_subspace_iterations", m_max_subspace_iterations);
      }
      else {
         m_max_subspace_iterations = 1;
      }
      if ( a_pp.contains("subspace_iteration_tol") ) {
         a_pp.get("subspace_iteration_tol", m_subspace_iteration_tol);
      }
      else {
         m_subspace_iteration_tol = 1.e-6;
      }
   }
   else {
      m_subspace_iteration_solve = false;
   }

   if (m_radial_solve_only && m_subspace_iteration_solve) {
      MayDay::Error("Specify either radial or subspace iteration solve, but not both");
   }

   // Create the local arrays

   const DisjointBoxLayout& grids = m_geometry.grids();
   m_boltzmann_prefactor.define(m_flux_surface.grids(), 1, IntVect::Zero);

   if ( m_prefactor_strategy == FS_NEUTRALITY_INITIAL_FS_NI ||
        m_prefactor_strategy == FS_NEUTRALITY_INITIAL_GLOBAL_NI ) {

      computePrefactorNumerator( a_initial_ion_charge_density );

      m_initial_ion_charge_density_average = averageMapped(a_initial_ion_charge_density);
   }

   m_M.define(grids, 1, IntVect::Zero);
   m_D.define(grids, 1, IntVect::Zero);

   m_pbsolver.define(this, true);
   m_pbsolver.m_verbosity = 4;
   m_pbsolver.m_imax = 20;

}



GKPoissonBoltzmann::~GKPoissonBoltzmann()
{
  if (m_Zni_outer_plate) delete [] m_Zni_outer_plate;
  if (m_Zni_inner_plate) delete [] m_Zni_inner_plate;
  if (m_phi_outer_plate) delete [] m_phi_outer_plate;
  if (m_phi_inner_plate) delete [] m_phi_inner_plate;
}



void
GKPoissonBoltzmann::computePotentialAndElectronDensity(
   LevelData<FArrayBox>&       a_phi,
   BoltzmannElectron&          a_ne,
   const LevelData<FArrayBox>& a_ni,
   const PotentialBC&          a_bc,
   const bool                  a_first_step)
{
   if ( m_radial_solve_only ) {
      solveRadial( a_ni, a_bc, a_ne, a_phi );
   }
   else if ( m_subspace_iteration_solve ) {
      solveSubspaceIteration( a_ni, a_bc, a_ne, a_phi );
   }
   else {
      solve( a_ni, a_bc, a_ne, a_phi );
   }

   fillGhosts(a_phi);
}



void
GKPoissonBoltzmann::setOperatorCoefficients( const LevelData<FArrayBox>& a_ion_mass_density,
                                             const PotentialBC&          a_bc )

{
   computeCoefficients( a_ion_mass_density, m_mapped_coefficients, m_unmapped_coefficients );

   setBcDivergence( a_bc, m_bc_divergence );

   m_core_radial_or_neumannbc = isCoreRadialPeriodicOrNeumannBC(a_bc);
}



void
GKPoissonBoltzmann::setOperatorCoefficients( const LevelData<FArrayBox>& a_ion_mass_density,
                                             const PotentialBC&          a_bc,
                                             double&                     a_lo_value,
                                             double&                     a_hi_value,
                                             LevelData<FArrayBox>&       a_radial_gkp_divergence_average)
{
   setOperatorCoefficients(a_ion_mass_density, a_bc);

   const DisjointBoxLayout& grids = m_geometry.grids();
   const MagCoordSys& coords = *m_geometry.getCoordSys();

   LevelData<FluxBox> ones( m_geometry.grids(), 1, IntVect::Zero);
   for (DataIterator dit( ones.dataIterator() ); dit.ok(); ++dit) {
      int block_number = coords.whichBlock(grids[dit]);

      for (int dir=0; dir<SpaceDim; ++dir) {
         FArrayBox& this_fab = ones[dit][dir];
         this_fab.setVal(0.);

         if ( dir == RADIAL_DIR && block_number < 2 ) {
            ones[dit].setVal(1.,dir);
         }
      }
   }

   //Compute <c^2*n*m/(e*B^2)*|grad(Psi)|^2>     
   computeRadialFSAverage(ones, a_lo_value, a_hi_value, a_radial_gkp_divergence_average);
}



void
GKPoissonBoltzmann::computePrefactorNumerator( const LevelData<FArrayBox>& a_ion_charge_density )
{
   if ( m_boltzmann_prefactor_saved_numerator.isDefined() ) {
      MayDay::Error( "GKPoissonBoltzmann::computeSavedPrefactorNumerator(): Prefactor numerator is already defined" );
   }
   else {
      m_boltzmann_prefactor_saved_numerator.define( m_flux_surface.grids(), 1, IntVect::Zero );
      m_flux_surface.average( a_ion_charge_density,
                              m_boltzmann_prefactor_saved_numerator );
   }
}



void
GKPoissonBoltzmann::updateLinearSystem( const BoltzmannElectron& a_ne,
                                        const PotentialBC&       a_bc )
{
   const DisjointBoxLayout& grids = m_geometry.grids();
   LevelData<FArrayBox> alpha(grids, 1, IntVect::Zero);
   LevelData<FArrayBox> beta(grids, 1, IntVect::Zero);

   if ( m_radial_solve_only || m_subspace_iteration_solve ) {

      for (DataIterator dit(grids.dataIterator()); dit.ok(); ++dit) {
         alpha[dit].setVal(1.);
         beta[dit].setVal(0.);
      }

      m_precond_Psolver.constructMatrix(alpha, m_mapped_coefficients, beta, a_bc, true);

   }
   else {

      // Calculate the Boltzmann derivative factor
      computeBoltzmannDerivative(a_ne, m_M);

#ifdef DIVIDE_M

      // Set alpha = M^{-1}
      for (DataIterator dit(grids.dataIterator()); dit.ok(); ++dit) {
         alpha[dit].setVal(1.);
      }
      divideM(alpha);

      // Construct beta = I - D
      for (DataIterator dit(grids.dataIterator()); dit.ok(); ++dit) {
         beta[dit].setVal(-1.);
      }
      multiplyD(beta);

      for (DataIterator dit(grids.dataIterator()); dit.ok(); ++dit) {
         beta[dit] += 1.;
      }

      m_precond_Psolver.constructMatrix(alpha, m_mapped_coefficients, beta, a_bc, true);

#else

      for (DataIterator dit(grids.dataIterator()); dit.ok(); ++dit) {
         alpha[dit].setVal(1.);
         beta[dit].setVal(0.);
      }

      m_precond_Psolver.constructMatrix(alpha, m_mapped_coefficients, beta, a_bc, true);

#endif

#ifdef DIVIDE_M

      // Set alpha = M^{-1}
      for (DataIterator dit(grids.dataIterator()); dit.ok(); ++dit) {
         alpha[dit].setVal(1.);
      }
      divideM(alpha);

#if 0
      double anorm = L2Norm(alpha);
      cout.precision(20);
      if(procID()==0) cout <<"anorm = " << anorm << endl;
      cout.precision();
#endif

      // Set beta = I
      for (DataIterator dit(grids.dataIterator()); dit.ok(); ++dit) {
         beta[dit].setVal(1.);
      }

      m_precond_Qsolver.constructMatrix(alpha, m_mapped_coefficients, beta, a_bc, false);

#else

      // Set alpha = I
      for (DataIterator dit(grids.dataIterator()); dit.ok(); ++dit) {
         alpha[dit].setVal(1.);
      }

      // Set beta = M
      for (DataIterator dit(grids.dataIterator()); dit.ok(); ++dit) {
         beta[dit].setVal(1.);
      }
      multiplyM(beta);

      m_precond_Qsolver.constructMatrix(alpha, m_mapped_coefficients, beta, a_bc, false);

#endif
   }
}



void
GKPoissonBoltzmann::solveJacobian( const LevelData<FArrayBox>& a_r,
                                   LevelData<FArrayBox>&       a_z,
                                   const bool                  a_use_absolute_tolerance ) const
{
   if ( a_use_absolute_tolerance ) {
      // Set an absolute tolerance
      m_pbsolver.setConvergenceMetrics(1., m_jacobian_solve_tolerance);
   }
   else {
      // Set a relative tolerance based on the right-hand side norm
      m_pbsolver.setConvergenceMetrics(-1., m_jacobian_solve_tolerance);
   }
   
   setZero(a_z);
   m_pbsolver.solve(a_z, a_r);
}


void
GKPoissonBoltzmann::solve( const LevelData<FArrayBox>& a_Zni,
                           const PotentialBC&          a_bc,
                           BoltzmannElectron&          a_ne,
                           LevelData<FArrayBox>&       a_phi )
{
   const DisjointBoxLayout& grids = m_geometry.grids();
   LevelData<FArrayBox> correction(grids, 1, IntVect::Zero);
   LevelData<FArrayBox> residual(grids, 1, IntVect::Zero);
   LevelData<FArrayBox> old_phi(grids, 1, IntVect::Zero);

   // Compute the norm of the right-hand side and absolute tolerance
   double rhs_norm = L2Norm(a_Zni);
   double change_norm = DBL_MAX;
   double res_tol = m_nonlinear_relative_tolerance * rhs_norm;
   double change_tol = m_nonlinear_change_tolerance;

   int iter = 0;

   // Compute the initial nonlinear residual and its norm
   double residual_norm = computeResidual(a_Zni, a_phi, a_bc, a_ne, residual);

   bool test_flux_surface_neutrality =
      m_prefactor_strategy == FS_NEUTRALITY ||
      m_prefactor_strategy == FS_NEUTRALITY_GLOBAL_NI ||
      m_prefactor_strategy == FS_NEUTRALITY_INITIAL_GLOBAL_NI ||
      m_prefactor_strategy == FS_NEUTRALITY_INITIAL_FS_NI;

   double neutrality_error = test_flux_surface_neutrality?
      fluxSurfaceNeutralityRelativeError(a_Zni, a_ne.numberDensity()):
      globalNeutralityRelativeError(a_Zni, a_ne.numberDensity());

   // Save the current iterate and norm
   DataIterator dit = a_phi.dataIterator();
   for (dit.begin(); dit.ok(); ++dit) {
      old_phi[dit].copy(a_phi[dit]);
   }
   double old_norm = L2Norm(old_phi);

   if (m_verbose && procID()==0) {
#if 0
      cout << "   Newton iteration " << iter << ": relative residual = "
           << residual_norm / rhs_norm << endl
           << "          neutrality relative error = " << neutrality_error << endl;
#else
      cout << "   Newton iteration " << iter << ": relative residual = "
           << residual_norm / rhs_norm << endl;
#endif
   }

   bool residual_tolerance_satisfied = residual_norm <= res_tol;
   bool change_tolerance_satisfied = change_norm <= change_tol;

   while ( iter++ < m_nonlinear_max_iterations && !(residual_tolerance_satisfied && change_tolerance_satisfied) ) {

      // Update the Jacobian
      updateLinearSystem(a_ne, a_bc);

      // Solve for the next Newton iterate.  If the residual tolerance is already
      // satisfied but we're still iterating to also make the solution change
      // small, then we may have a nearly singular or otherwise ill-conditioned problem,
      // in which case we use an absolute tolerance for the Jacobian solve.

      solveJacobian(residual, correction, residual_tolerance_satisfied);

      // Update solution
      for (dit.begin(); dit.ok(); ++dit) {
         a_phi[dit].minus(correction[dit]);
      }

      // Compute the new nonlinear residual and its norm
      residual_norm = computeResidual(a_Zni, a_phi, a_bc, a_ne, residual);

      residual_tolerance_satisfied = residual_norm <= res_tol;

      neutrality_error = test_flux_surface_neutrality?
         fluxSurfaceNeutralityRelativeError(a_Zni, a_ne.numberDensity()):
         globalNeutralityRelativeError(a_Zni, a_ne.numberDensity());

      // Compute the relative solution change and its norm
      for (dit.begin(); dit.ok(); ++dit) {
         old_phi[dit].minus(a_phi[dit]);
      }
      double diff_norm = L2Norm(old_phi);
      double new_norm = L2Norm(a_phi);

      change_norm = 2. * diff_norm / (old_norm + new_norm);

      change_tolerance_satisfied = change_norm <= change_tol;

      // Prepare for the next iteration
      for (dit.begin(); dit.ok(); ++dit) {
         old_phi[dit].copy(a_phi[dit]);
      }
      old_norm = new_norm;

      if (m_verbose && procID()==0) {
#if 1
         cout << "   Newton iteration " << iter << ": relative residual = "
              << residual_norm / rhs_norm << endl
            //        << "          neutrality relative error = " << neutrality_error << endl
              << "                       relative solution change = " << change_norm << endl;
#else
         cout << "   Newton iteration " << iter << ": relative residual = "
              << residual_norm / rhs_norm << endl;
#endif
      }
   }
}


void
GKPoissonBoltzmann::solveRadial( const LevelData<FArrayBox>& a_Zni,
                                 const PotentialBC&          a_bc,
                                 BoltzmannElectron&          a_ne,
                                 LevelData<FArrayBox>&       a_phi )
{
   // Construct the matrix
   updateLinearSystem(a_ne, a_bc);

   // Solve for the flux surface average

   LevelData<FArrayBox> rhs;
   rhs.define(a_Zni);
   subtractBcDivergence(rhs);

   const DisjointBoxLayout& fs_grids = m_flux_surface.grids();
   LevelData<FArrayBox> phi_fs(fs_grids, 1, IntVect::Zero);
   m_flux_surface.average(rhs, phi_fs);

   for (DataIterator dit(fs_grids.dataIterator()); dit.ok(); ++dit) {
      phi_fs[dit] -= m_boltzmann_prefactor_saved_numerator[dit];
   }

   solveFluxSurfaceAverage(phi_fs);

   m_flux_surface.spread(phi_fs, a_phi );

   fillGhosts(a_phi);

   computeElectronDensity(a_phi, a_Zni, a_ne);
}


void
GKPoissonBoltzmann::solveSubspaceIteration( const LevelData<FArrayBox>& a_Zni,
                                            const PotentialBC&          a_bc,
                                            BoltzmannElectron&          a_ne,
                                            LevelData<FArrayBox>&       a_phi )
{
   // Subspace iteration, solving for the flux surface average first, then
   // the poloidal variation.

   const DisjointBoxLayout& grids = m_geometry.grids();
   LevelData<FArrayBox> temp(grids, 1, IntVect::Zero);

   const DisjointBoxLayout& fs_grids = m_flux_surface.grids();
   LevelData<FArrayBox> temp_fs(fs_grids, 1, IntVect::Zero);

   double charge_correction = 0.;
#if 0
   if (m_preserve_initial_ni_average) {
      LevelData<FArrayBox> tmp_Zni;
      tmp_Zni.define(a_Zni);
      subtractBcDivergence(tmp_Zni);

      charge_correction = averageMapped(tmp_Zni) - m_initial_ion_charge_density_average;

      if (procID()==0) cout << "charge correction = " << charge_correction/m_initial_ion_charge_density_average << endl;
   }
#endif

   int iter = 0;
   bool converged = false;
   double change_norm;

   setZero(a_phi);

   LevelData<FArrayBox> phi_bar;
   phi_bar.define(a_phi);

   LevelData<FArrayBox> phi_tilde;
   phi_tilde.define(a_phi);

   LevelData<FArrayBox> phi_old;
   phi_old.define(a_phi);
   double norm_phi_old = L2Norm(phi_old);

   computeElectronDensity(a_phi, a_Zni, a_ne);

   const LevelData<FArrayBox>& ne = a_ne.numberDensity();

   // Construct the tridiagonal system
   updateLinearSystem(a_ne, a_bc);

   while ( !converged && iter < m_max_subspace_iterations ) {

      // Solve for the poloidal variation phi_tilde

      applyOperator(a_phi, a_bc, temp, true);  // Homogeneous bcs
      addBcDivergence(temp);  // Add bcs
      for (DataIterator dit(grids); dit.ok(); ++dit) {
         temp[dit].negate();
         temp[dit] += a_Zni[dit];
         temp[dit] -= charge_correction;
      }

      getPhiTilde(temp, a_ne, phi_tilde); 

      // Update phi_bar (the flux surface average) given the current
      // estimate of phi_tilde

      computeElectronDensity(phi_tilde, a_Zni, a_ne);

      applyOperator(phi_tilde, a_bc, temp, true);  // Homogeneous bcs
      addBcDivergence(temp);  // Add bcs
      for (DataIterator dit(grids); dit.ok(); ++dit) {
         temp[dit].negate();
         temp[dit] += a_Zni[dit];
         temp[dit] -= ne[dit];
         temp[dit] -= charge_correction;
      }

      m_flux_surface.average(temp, temp_fs);
      
      solveFluxSurfaceAverage(temp_fs);

      m_flux_surface.spread(temp_fs, phi_bar );

      for (DataIterator dit(grids); dit.ok(); ++dit) {
         a_phi[dit].copy(phi_bar[dit]);
         a_phi[dit] += phi_tilde[dit];
      }
      fillGhosts(a_phi);

      double norm_phi_new = L2Norm(a_phi);

      for (DataIterator dit(grids); dit.ok(); ++dit) {
         temp[dit].copy(a_phi[dit]);
         temp[dit] -= phi_old[dit];
         phi_old[dit].copy(a_phi[dit]);
      }

      change_norm = L2Norm(temp) / ( 0.5 * (norm_phi_old + norm_phi_new) );

      converged = change_norm < m_subspace_iteration_tol;

      norm_phi_old = norm_phi_new;

      iter++;
   }

   if ( procID()==0 ) cout << "  Number of subspace iterations = " << iter << ", final solution change norm = " << change_norm << endl;
}



double
GKPoissonBoltzmann::computeResidual( const LevelData<FArrayBox>& a_Zni,
                                     const LevelData<FArrayBox>& a_phi,
                                     const PotentialBC&          a_bc,
                                     BoltzmannElectron&          a_ne,
                                     LevelData<FArrayBox>&       a_residual )
{
   /*
     For the input Zni and phi, compute the residual:

     R = GKP_Operator_Matrix * phi + n_e - Zni

     where n_e is the adiabatic electron density.
   */

   // Initialize the residual with the GK Poisson operator evaluated at phi
   applyOperator(a_phi, a_bc, a_residual, true);  // Homogeneous bcs
   addBcDivergence(a_residual);  // Add bcs

   // Compute the electron density
   computeElectronDensity(a_phi, a_Zni, a_ne);
   const LevelData<FArrayBox>& ne = a_ne.numberDensity();

   for (DataIterator dit(a_residual.dataIterator()); dit.ok(); ++dit) {
      FArrayBox& this_residual = a_residual[dit];
      this_residual.plus(ne[dit]);
      this_residual.minus(a_Zni[dit]);
   }

   if (m_preserve_initial_ni_average) {
      LevelData<FArrayBox> tmp_Zni;
      tmp_Zni.define(a_Zni);
      subtractBcDivergence(tmp_Zni);

      double total_charge = averageMapped(tmp_Zni) - m_initial_ion_charge_density_average;

      for (DataIterator dit(a_residual.dataIterator()); dit.ok(); ++dit) {
         a_residual[dit] += total_charge;
      }

      //      if (procID()==0) cout << "charge correction = " << total_charge/m_initial_ion_charge_density_average << endl;
   }

   // Return the residual L2 norm
   return L2Norm(a_residual);
}


void
GKPoissonBoltzmann::computeChargeDensity( const LevelData<FArrayBox>& a_Zni,
                                          const LevelData<FArrayBox>& a_phi,
                                          BoltzmannElectron&          a_ne,
                                          LevelData<FArrayBox>&       a_charge_density )
{
   /*
     For the input Zni and phi, compute the charge density:

     R = n_e - Zni

     where n_e is the adiabatic electron density.
   */

   // Compute the electron density
   computeElectronDensity(a_phi, a_Zni, a_ne);
   const LevelData<FArrayBox>& ne = a_ne.numberDensity();

   for (DataIterator dit(a_charge_density.dataIterator()); dit.ok(); ++dit) {
      FArrayBox& this_charge_density = a_charge_density[dit];
      this_charge_density.copy(ne[dit]);
      this_charge_density.minus(a_Zni[dit]);
   }
}


void
GKPoissonBoltzmann::computeElectronDensity( const LevelData<FArrayBox>& a_phi,
                                            const LevelData<FArrayBox>& a_Zni,
                                            BoltzmannElectron&      a_ne )
{
   /*
     Compute the Boltzmann electron density

             ne = prefactor * exp (phi / Te)

   */


   m_boltzmann_relation.updateDensity(a_phi, a_ne);

   LevelData<FArrayBox>& ne = a_ne.numberDensity();

   if (m_recompute_prefactor) {

      DataIterator pdit = m_boltzmann_prefactor.dataIterator();

      // Set the prefactor numerator
      if ( m_prefactor_strategy == GLOBAL_NEUTRALITY ||
           m_prefactor_strategy == GLOBAL_NEUTRALITY_INITIAL ||
           m_prefactor_strategy == FS_NEUTRALITY_GLOBAL_NI ||
           m_prefactor_strategy == FS_NEUTRALITY_INITIAL_GLOBAL_NI ) {

         double numerator;
         if (m_prefactor_strategy == FS_NEUTRALITY_INITIAL_GLOBAL_NI) {
            numerator = m_initial_ion_charge_density_average;
         }
         else {
            numerator = averageMapped(a_Zni);
         }
         for (pdit.begin(); pdit.ok(); ++pdit) {
            m_boltzmann_prefactor[pdit].setVal(numerator);
         }
      }
      else if (m_prefactor_strategy == FS_NEUTRALITY_INITIAL_FS_NI) {

         for (pdit.begin(); pdit.ok(); ++pdit) {
            m_boltzmann_prefactor[pdit].copy(m_boltzmann_prefactor_saved_numerator[pdit]);
         }
      }
      else {
         m_flux_surface.average(a_Zni, m_boltzmann_prefactor);
      }

      // Divide the prefactor denominator
      if ( m_prefactor_strategy == GLOBAL_NEUTRALITY ||
           m_prefactor_strategy == GLOBAL_NEUTRALITY_INITIAL ) {

         double denominator = averageMapped(ne);
         for (pdit.begin(); pdit.ok(); ++pdit) {
            m_boltzmann_prefactor[pdit] /= denominator;
         }
      }
      else {

         LevelData<FArrayBox> epot_average(m_flux_surface.grids(), 1, IntVect::Zero);
         m_flux_surface.average(ne, epot_average);

         for (pdit.begin(); pdit.ok(); ++pdit) {
            m_boltzmann_prefactor[pdit].divide(epot_average[pdit]);
         }

         // POSSIBLE UNINTENDED SIDE EFFECTS HERE?
         // Compute the D matrix in the Jacobian
         DataIterator dit = m_D.dataIterator();
         for (dit.begin(); dit.ok(); ++dit) {
            m_D[dit].copy(ne[dit]);
         }
         m_flux_surface.divide(epot_average, m_D);
      }
   }

   m_flux_surface.multiply(m_boltzmann_prefactor, ne);

#if 0

   //Create electron density for a single-null geometry
   const MagCoordSys& coord_sys( *(m_geometry.getCoordSys()) );
   bool not_single_null = ( typeid(coord_sys) != typeid(SingleNullCoordSys) );

   if (!not_single_null) {
     
     m_compute_core_prefactor = true;

     //Clean previous calculations
     m_boltzmann_relation.updateDensity(a_phi, a_ne);
     const DisjointBoxLayout& grids = m_geometry.grids();

     //Compute core prefactor
     if (m_compute_core_prefactor) {
               
        //Temporary. Later, to be consistent with restart, 
        //will be changed to reading initial density from the input file
        
        m_core_prefactor_numerator.define( grids, 1, IntVect::Zero );
        m_flux_surface.averageAndSpread(a_Zni, m_core_prefactor_numerator);
        m_compute_core_prefactor = false;
     }

     LevelData<FArrayBox> core_prefactor_denominator;
     core_prefactor_denominator.define( grids, 1, IntVect::Zero );
     m_flux_surface.averageAndSpread(ne, core_prefactor_denominator);

     DataIterator dit = grids.dataIterator();
     for (dit.begin(); dit.ok(); ++dit) {
        int block_number = coord_sys.whichBlock( grids[dit] );

        if ((block_number == RCORE)||(block_number == LCORE)) {

          //Compute the electron density inside LCFS         
          ne[dit].mult(m_core_prefactor_numerator[dit]);             
          ne[dit].divide(core_prefactor_denominator[dit]);

        }

        else {
          
          //Compute the electron density outside LCFS
          double echarge = a_ne.charge();

          //TEMPORARY USE CONSTANT TE (UNITY)
          double Te = 1.0;

          for (BoxIterator bit( grids[dit] ); bit.ok(); ++bit) {

            IntVect iv( bit() );
            int irad = getConsequtiveRadialIndex(iv[RADIAL_DIR], block_number);
            FArrayBox& this_ne( ne[dit] );
            this_ne(iv,0)*=m_Zni_outer_plate[irad];
            this_ne(iv,0)*=exp(echarge/Te * m_phi_outer_plate[irad]);

          }
        }
     } 
   }
#endif
}



void
GKPoissonBoltzmann::computeBoltzmannDerivative( const BoltzmannElectron& a_ne,
                                                LevelData<FArrayBox>&    a_boltz_deriv ) const
{
   // Computes ne / Te

   m_boltzmann_relation.phiDerivative(a_ne, a_boltz_deriv);
}



void
GKPoissonBoltzmann::getPhiTilde( const LevelData<FArrayBox>& a_Zni,
                                 const BoltzmannElectron&    a_ne,
                                 LevelData<FArrayBox>&       a_phi_tilde )
{
   int maa = 20;
   double tol = 1.e-10;
   int max_iter = maa;
   double change_norm;
   int num_iters;

   const DisjointBoxLayout& grids = m_geometry.grids();

   LevelData<FArrayBox> change(grids, 1, IntVect::Zero);
   LevelData<FArrayBox> gval(grids, 1, IntVect::Zero);
   LevelData<FArrayBox> phi(grids, 1, IntVect::Zero);
   LevelData<FArrayBox> phi_tilde_new(grids, 1, IntVect::Zero);

   double Zni_norm = L2Norm(a_Zni);

   BoltzmannElectron ne( a_ne );

   const LevelData<FArrayBox>& Te = a_ne.temperature();
   double Te_norm = L2Norm(Te);

   double fp_convergence_rate = 1.e-3;

   double omega = (1. - fp_convergence_rate) * Te_norm / Zni_norm;

   for (DataIterator dit(grids); dit.ok(); ++dit) {
      a_phi_tilde[dit].setVal(0.);
   }

   num_iters = 0;

   if (procID()==0) {
      cout << "     Fixed point iteration with omega = " << omega << endl;
   }

   bool keep_iterating = true;
   while ( keep_iterating ) {

      num_iters++;

      // Compute function whose fixed point is being sought
      computeChargeDensity(a_Zni, a_phi_tilde, ne, gval);
      applyQ(gval);

      double res_norm = L2Norm(gval);

      for (DataIterator dit(grids); dit.ok(); ++dit) {
         gval[dit] *= omega;
      }

      // Compute g(phi) = phi + f(phi)
      for (DataIterator dit(grids); dit.ok(); ++dit) {
         gval[dit].negate();
         gval[dit] += a_phi_tilde[dit];
      }

      for (DataIterator dit(change.dataIterator()); dit.ok(); ++dit) {
         phi_tilde_new[dit].copy(gval[dit]);
      }

      for (DataIterator dit(change.dataIterator()); dit.ok(); ++dit) {
         change[dit].copy(phi_tilde_new[dit]);
         change[dit] -= a_phi_tilde[dit];
      }

      change_norm = L2Norm(change) / (0.5 * (L2Norm(phi_tilde_new) + L2Norm(a_phi_tilde)));

      if (procID()==0) {
         cout << "     iter = " << num_iters << ", change_norm = " << change_norm << ", relative res_norm = " << res_norm / Zni_norm << endl;
      }

      keep_iterating = change_norm > tol && num_iters < max_iter;

      if ( keep_iterating ) {
         for (DataIterator dit(grids); dit.ok(); ++dit) {
            a_phi_tilde[dit].copy(phi_tilde_new[dit]);
         }
      }
   }
}


double
GKPoissonBoltzmann::globalNeutralityRelativeError( const LevelData<FArrayBox>& a_Zni,
                                                   const LevelData<FArrayBox>& a_ne ) const
{
   const DisjointBoxLayout& grids = m_geometry.grids();
   LevelData<FArrayBox> error(grids, 1, IntVect::Zero);

   for (DataIterator dit(a_Zni.dataIterator()); dit.ok(); ++dit) {
      error[dit].copy(a_Zni[dit]);
      error[dit].minus(a_ne[dit]);
   }

   return fabs( integrateMapped(error) / integrateMapped(a_Zni) );
}



double
GKPoissonBoltzmann::fluxSurfaceNeutralityRelativeError( const LevelData<FArrayBox>& a_Zni,
                                                        const LevelData<FArrayBox>& a_ne ) const
{
   const DisjointBoxLayout& flux_surface_grids = m_flux_surface.grids();

   LevelData<FArrayBox> error(flux_surface_grids, 1, IntVect::Zero);
   m_flux_surface.average(a_ne, error);

   if (m_prefactor_strategy == FS_NEUTRALITY) {
      LevelData<FArrayBox> Zni_integrated(flux_surface_grids, 1, IntVect::Zero);
      m_flux_surface.average(a_Zni, Zni_integrated);

      for (DataIterator dit(flux_surface_grids.dataIterator()); dit.ok(); ++dit) {
         error[dit] -= Zni_integrated[dit];
         error[dit] /= Zni_integrated[dit];
      }
   }
   else if (m_prefactor_strategy == FS_NEUTRALITY_GLOBAL_NI) {
      double ni_global_average = averageMapped(a_Zni);
      for (DataIterator dit(flux_surface_grids.dataIterator()); dit.ok(); ++dit) {
         error[dit] -= ni_global_average;
         error[dit] /= ni_global_average;
      }
   }
   else if (m_prefactor_strategy == FS_NEUTRALITY_INITIAL_GLOBAL_NI) {
      for (DataIterator dit(flux_surface_grids.dataIterator()); dit.ok(); ++dit) {
         error[dit] -= m_initial_ion_charge_density_average;
         error[dit] /= m_initial_ion_charge_density_average;
      }
   }
   else if (m_prefactor_strategy == FS_NEUTRALITY_INITIAL_FS_NI) {
      for (DataIterator dit(flux_surface_grids.dataIterator()); dit.ok(); ++dit) {
         error[dit] -= m_boltzmann_prefactor_saved_numerator[dit];
         error[dit] /= m_boltzmann_prefactor_saved_numerator[dit];
      }
   }
   else {
      MayDay::Error("GKPoissonBoltzmann::fluxSurfaceNeutralityRelativeError(): Unrecognized prefactor strategy");
   }

   double local_max = -DBL_MAX;

   for (DataIterator dit(error.dataIterator()); dit.ok(); ++dit) {
      FArrayBox& this_error = error[dit];
      this_error.abs();

      double this_max = this_error.max();
      if (this_max > local_max) local_max = this_max;
   }

   double global_max;

#ifdef CH_MPI
   MPI_Allreduce(&local_max, &global_max, 1, MPI_DOUBLE, MPI_MAX, MPI_COMM_WORLD);
#else
   global_max = local_max;
#endif

   return global_max;
}



double
GKPoissonBoltzmann::integrateMapped( const LevelData<FArrayBox>& a_data ) const
{
   const DisjointBoxLayout& grids = a_data.getBoxes();

   double local_sum = 0.;
   for (DataIterator dit(grids.dataIterator()); dit.ok(); ++dit) {
      FArrayBox tmp(grids[dit],1);
      tmp.copy(m_volume[dit]);
      tmp.mult(a_data[dit]);
      local_sum += tmp.sum(grids[dit],0);
   }

   double global_sum;
#ifdef CH_MPI
   MPI_Allreduce(&local_sum, &global_sum, 1, MPI_DOUBLE, MPI_SUM, MPI_COMM_WORLD);
#else
   global_sum = local_sum;
#endif

   return global_sum;
}



double
GKPoissonBoltzmann::averageMapped( const LevelData<FArrayBox>& a_data ) const
{
   const DisjointBoxLayout& grids = a_data.getBoxes();

   double local_volume = 0.;
   for (DataIterator dit(grids.dataIterator()); dit.ok(); ++dit) {
      local_volume += m_volume[dit].sum(grids[dit],0);
   }

   double volume;
#ifdef CH_MPI
   MPI_Allreduce(&local_volume, &volume, 1, MPI_DOUBLE, MPI_SUM, MPI_COMM_WORLD);
#else
   volume = local_volume;
#endif

   return integrateMapped(a_data) / volume;
}



void
GKPoissonBoltzmann::residual(  LevelData<FArrayBox>&       a_lhs,
                               const LevelData<FArrayBox>& a_phi,
                               const LevelData<FArrayBox>& a_rhs,
                               bool a_homogeneous)
{
   applyOp(a_lhs, a_phi, a_homogeneous );

   for (DataIterator dit(a_lhs.dataIterator()); dit.ok(); ++dit) {
      a_lhs[dit].negate();
      a_lhs[dit].plus(a_rhs[dit]);
   }
}



void
GKPoissonBoltzmann::preCond( LevelData<FArrayBox>&       a_z,
                             const LevelData<FArrayBox>& a_r)
{
#ifndef DIVIDE_M
   // Flux surface average the residual
   const DisjointBoxLayout& fs_grids = m_flux_surface.grids();
   LevelData<FArrayBox> Prz(fs_grids, 1, IntVect::Zero);
   m_flux_surface.average(a_r, Prz);

   // Compute the complement of the residual flux surface average
   LevelData<FArrayBox> Qr;
   Qr.define(a_r);
   m_flux_surface.subtract(Prz, Qr);

   // Solve for the flux surface average
   solveFluxSurfaceAverage(Prz);

   // Solve for the complement of the flux surface average
   setZero(a_z);
   m_precond_Qsolver.solveWithMultigrid(Qr, 1.9e-6, 1, true, a_z);

   // Combine components
   m_flux_surface.add(Prz, a_z);
#else

   LevelData<FArrayBox> Minverser;
   Minverser.define(a_r);
   divideM(Minverser);

   // Compute Q M^-1 r
   LevelData<FArrayBox> temp;
   temp.define(Minverser);
   applyQ(temp);

   // Solve for Qz
   setZero(a_z);
   m_precond_Qsolver.solveWithMultigrid(temp, 1.e-2, 10, true, a_z);

   // Compute M^-1 r + DQz
   for (DataIterator dit(temp.dataIterator()); dit.ok(); ++dit) {
      temp[dit].copy(a_z[dit]);
   }
   multiplyD(temp);
   for (DataIterator dit(temp.dataIterator()); dit.ok(); ++dit) {
      temp[dit] += Minverser[dit];
   }

   // Compute P(M^-1 r + DQz)
   LevelData<FArrayBox> Prz(m_flux_surface.grids(), 1, IntVect::Zero);
   m_flux_surface.average(temp, Prz);

   // Solve for the flux surface average
   solveFluxSurfaceAverage(Prz);

   // Combine components
   m_flux_surface.add(Prz, a_z);

#endif
}



void
GKPoissonBoltzmann::solveFluxSurfaceAverage( LevelData<FArrayBox>& a_data ) const
{
   const ProblemDomain& domain = m_geometry.getBlockCoordSys(0).domain();
   const Box& domain_box = domain.domainBox();
   const bool radially_periodic = domain.isPeriodic(RADIAL_DIR);
   const int n = domain_box.size(RADIAL_DIR);

   int radial_dir = RADIAL_DIR;
   int radial_lo = domain_box.smallEnd(radial_dir);

   int poloidal_dir = POLOIDAL_DIR;
   int poloidal_lo = domain_box.smallEnd(poloidal_dir);

   const DisjointBoxLayout& fs_grids = m_flux_surface.grids();
   LevelData<FArrayBox> Pr(fs_grids, 3, IntVect::Zero);
   m_flux_surface.average(m_precond_Psolver.getARadial(), Pr);

   double * temp  = new double[4*n];
   for (int i=0; i<4*n; ++i) temp[i] = 0.;

   double * ldiag = &(temp[0]);
   double * diag  = &(temp[n]);
   double * udiag = &(temp[2*n]);
   double * bx    = &(temp[3*n]);

   double *send_buffer = new double[n];

   DataIterator dit = fs_grids.dataIterator();
   for (int comp=0; comp<Pr.nComp(); ++comp) {

      for (int i=0; i<n; ++i) send_buffer[i] = 0.;

      for (dit.begin(); dit.ok(); ++dit) {
         FArrayBox& this_fab = Pr[dit];
         BoxIterator bit(fs_grids[dit]);
         for (bit.begin(); bit.ok(); ++bit) {
            IntVect iv = bit();
            if (iv[poloidal_dir] == poloidal_lo) {
               send_buffer[iv[radial_dir]-radial_lo] = this_fab(iv,comp);
            }
         }
      }

#ifdef CH_MPI
      MPI_Allreduce(send_buffer, &temp[comp*n], n, MPI_DOUBLE, MPI_SUM, MPI_COMM_WORLD);
#else
      for (int i=0; i<n; ++i) {
        temp[comp*n+i] = send_buffer[i];
      }
#endif
   }

   for (int i=0; i<n; ++i) send_buffer[i] = 0.;

   for (dit.begin(); dit.ok(); ++dit) {
      FArrayBox& this_fab = a_data[dit];
      BoxIterator bit(fs_grids[dit]);
      for (bit.begin(); bit.ok(); ++bit) {
         IntVect iv = bit();
         if (iv[poloidal_dir] == poloidal_lo) {
            send_buffer[iv[radial_dir]-radial_lo] = this_fab(iv);
         }
      }
   }

#ifdef CH_MPI
   MPI_Allreduce(send_buffer, bx, n, MPI_DOUBLE, MPI_SUM, MPI_COMM_WORLD);
#else
   for (int i=0; i<n; ++i) {
     bx[i] = send_buffer[i];
   }
#endif

   if ( m_core_radial_or_neumannbc ) {

      // With Neumann radial boundary conditions, the resulting tridiagonal
      // matrix is singular, which is problematic for the direct solver.  We
      // therefore solve the system as a quadratic minimization problem
      // with a linear constraint, using a Lagrange multiplier.  The constraint
      // is that the integral of the solution over the radial domain is zero,
      // so we need the flux surface areas to compute the integral.

      const LevelData<FArrayBox>& fs_areas = m_flux_surface.areas();

      for (dit.begin(); dit.ok(); ++dit) {
         const FArrayBox& this_fab = fs_areas[dit];
         BoxIterator bit(fs_grids[dit]);
         for (bit.begin(); bit.ok(); ++bit) {
            IntVect iv = bit();
            if (iv[poloidal_dir] == poloidal_lo) {
               send_buffer[iv[radial_dir]-radial_lo] = this_fab(iv);
            }
         }
      }

      double * weights = new double[n];

#ifdef CH_MPI
      MPI_Allreduce(send_buffer, weights, n, MPI_DOUBLE, MPI_SUM, MPI_COMM_WORLD);
#else
      for (int i=0; i<n; ++i) {
         weights[i] = send_buffer[i];
      }
#endif

      double total_weight = 0.;
      for (int i=0; i<n; ++i) total_weight += weights[i];
      for (int i=0; i<n; ++i) weights[i] /= total_weight;

      solveTridiagonalNeumann(n, diag, udiag, ldiag, bx, weights);

      delete [] weights;
   }
   else {
      solveTridiagonal(radially_periodic, n, diag, udiag, ldiag, bx);
   }

   for (dit.begin(); dit.ok(); ++dit) {
      FArrayBox& this_fab = a_data[dit];
      BoxIterator bit(fs_grids[dit]);
      for (bit.begin(); bit.ok(); ++bit) {
         IntVect iv = bit();
         this_fab(iv) = bx[iv[radial_dir]-radial_lo];
      }
   }

   delete [] send_buffer;
   delete [] temp;
}



void
GKPoissonBoltzmann::solveTridiagonal( const bool    a_periodic,
                                      const int            a_n,
                                      double *          a_diag,
                                      double *         a_udiag,
                                      double *         a_ldiag,
                                      double *    a_fs_average ) const
{
  int i, info, nrhs, ldb;

  if (!a_periodic) {

    nrhs = 1;
    ldb = a_n;

    // Solve tridiagonal
    dgtsv_(a_n, nrhs, a_ldiag+1, a_diag, a_udiag, a_fs_average, ldb, info);

    if (info != 0) {
       cout << "dgtsv failed, returning " << info << ", n = " << a_n << endl;
    }
  }
  else {

    int nm1 = a_n-1;
    ldb = nm1;

    double a11 = a_diag[0];
    double a21 = a_ldiag[1];
    double am1 = a_udiag[nm1];
    double a12 = a_udiag[0];
    double a1m = a_ldiag[0];
    double b1 = a_fs_average[0];

    nrhs = 2;
    double * rhs = new double[nrhs*nm1];
    double * y = rhs;
    double * z = rhs + nm1;
    for (i=0; i<nm1; i++) {
      y[i] = a_fs_average[i+1];
    }
    z[0] = -a21;
    for (i=1; i<nm1-1; i++) {
      z[i] = 0.;
    }
    z[nm1-1] = -am1;

    // Solve tridiagonals
    dgtsv_(nm1, nrhs, a_ldiag+2, a_diag+1, a_udiag+1, rhs, ldb, info);

    if (info != 0) {
       cout << "dgtsv failed, returning " << info << ", n = " << a_n << endl;
    }

    double denom = a11 + a12*z[0] + a1m*z[nm1-1];
    double beta;

    if (denom != 0.) {
      beta = (b1 - a12*y[0] - a1m*y[nm1-1]) / (a11 + a12*z[0] + a1m*z[nm1-1]);
      }
    else {
      cout << "Zero denominator in tridiagonal solve" << endl;
      exit(1);
    }

    a_fs_average[0] = beta;
    for (i=1; i<a_n; i++) {
      a_fs_average[i] = y[i-1] + beta * z[i-1];
    }

    delete [] rhs;
  }
}



void
GKPoissonBoltzmann::solveTridiagonalNeumann( const int            a_n,
                                             double *          a_diag,
                                             double *         a_udiag,
                                             double *         a_ldiag,
                                             double *    a_fs_average,
                                             const double * a_weights ) const
{
  int n_augmented = a_n + 1;
  int num_mat_elems = n_augmented * n_augmented;

  double * A = new double[num_mat_elems];
  for (int k=0; k<num_mat_elems; ++k) A[k] = 0.;

  // Load the diagonal
  for (int i=0, k=0; i<a_n; ++i, k+=n_augmented+1) {
     A[k] = a_diag[i];
  }

  // Load the lower diagonal
  for (int i=1, k=1; i<a_n; ++i, k+=n_augmented+1) {
     A[k] = a_ldiag[i];
  }

  // Load the upper diagonal
  for (int i=0, k=n_augmented; i<a_n-1; ++i, k+=n_augmented+1) {
     A[k] = a_udiag[i];
  }

  // Load the right-hand border
  for (int k=n_augmented*a_n, i=0; k<num_mat_elems-1; ++k, ++i) {
     A[k] = a_weights[i];
  }

  for (int k=a_n, i=0; k<n_augmented*a_n; k+=n_augmented, ++i) {
     A[k] = a_weights[i];
  }
  // Load the lower border

#if 0
  if(procID()==0) {
     for (int i = 0; i<n_augmented; ++i) {
        for (int j=0; j<n_augmented; ++j) {
           cout << A[j*n_augmented+i] << " ";
        }
     cout << endl;
     }
  }
  //  exit(1);
#endif

  // Create the augmented rhs
  int nrhs = 1;
  double * b = new double[n_augmented];
  for (int k=0; k<a_n; ++k) b[k] = a_fs_average[k];
  b[a_n] = 0.;

  // Remove the average to make well-posed
  double b_av = 0.;
  for (int k=0; k<a_n; ++k) {
     b_av += a_weights[k]*b[k];
  }
  for (int k=0; k<a_n; ++k) b[k] -= b_av;

  int * ipvt = new int[n_augmented];
  int info;

  dgesv_( n_augmented, nrhs, A, n_augmented, ipvt, b, n_augmented, info);

  if (info != 0) {
     cout << "dgesv failed, returning " << info << endl;
  }

  for (int i=0; i<a_n; ++i) a_fs_average[i] = b[i];

  if( m_verbose && procID()==0 ) {
     //     cout << "      Lagrange multiplier = " << b[a_n] << endl;
  }

  delete [] ipvt;
  delete [] b;
  delete [] A;
}



void
GKPoissonBoltzmann::applyP( LevelData<FArrayBox>& a_data ) const
{
   LevelData<FArrayBox> temp(m_flux_surface.grids(), 1, IntVect::Zero);
   m_flux_surface.average(a_data, temp);
   m_flux_surface.spread(temp, a_data);
}



void
GKPoissonBoltzmann::applyQ( LevelData<FArrayBox>& a_data ) const
{
   LevelData<FArrayBox> temp(m_flux_surface.grids(), 1, IntVect::Zero);
   m_flux_surface.average(a_data, temp);
   m_flux_surface.subtract(temp, a_data);
}



void
GKPoissonBoltzmann::multiplyM( LevelData<FArrayBox>& a_data ) const
{
   const DisjointBoxLayout& grids = a_data.disjointBoxLayout();

   for (DataIterator dit(grids.dataIterator()); dit.ok(); ++dit) {
      a_data[dit].mult(m_M[dit]);
   }
}



void
GKPoissonBoltzmann::divideM( LevelData<FArrayBox>& a_data ) const
{
   for (DataIterator dit(a_data.dataIterator()); dit.ok(); ++dit) {
      a_data[dit].divide(m_M[dit]);
   }
}



void
GKPoissonBoltzmann::multiplyD( LevelData<FArrayBox>& a_data ) const
{
   for (DataIterator dit(a_data.dataIterator()); dit.ok(); ++dit) {
      a_data[dit].mult(m_D[dit]);
   }
}



void
GKPoissonBoltzmann::applyOp( LevelData<FArrayBox>&       a_out,
                             const LevelData<FArrayBox>& a_in,
                             bool                        a_homogeneous )
{
   /*
     Computes a_out = (G + M(I - PD)) * a_in
   */

#if 0
   double inorm = L2Norm(a_in);
   cout.precision(20);
   if (procID()==0) cout << "applyop in = " << inorm << endl;
   cout.precision();
#endif

   // Multiply by G + M
   m_precond_Qsolver.multiplyMatrix(a_in, a_out);
#ifdef DIVIDE_M
   multiplyM(a_out);
#endif

   LevelData<FArrayBox> temp(a_out.disjointBoxLayout(), 1, IntVect::Zero);
   for (DataIterator dit(a_in.dataIterator()); dit.ok(); ++dit) {
      temp[dit].copy(a_in[dit]);
   }

   // Multiply by MPD
   multiplyD(temp);
   applyP(temp);
   multiplyM(temp);

   for (DataIterator dit(a_out.dataIterator()); dit.ok(); ++dit) {
      a_out[dit].minus(temp[dit]);
   }

#if 0
   double onorm = L2Norm(a_out);
   cout.precision(20);
   if (procID()==0) cout << "applyop out = " << onorm << endl;
   cout.precision();
#endif
}



void
GKPoissonBoltzmann::create( LevelData<FArrayBox>&       a_lhs,
                            const LevelData<FArrayBox>& a_rhs )
{
   a_lhs.define(a_rhs.disjointBoxLayout(), a_rhs.nComp(), a_rhs.ghostVect());
}



void
GKPoissonBoltzmann::assign( LevelData<FArrayBox>&       a_lhs,
                            const LevelData<FArrayBox>& a_rhs )
{
   for (DataIterator dit(a_lhs.dataIterator()); dit.ok(); ++dit) {
      a_lhs[dit].copy(a_rhs[dit]);
   }
}



Real
GKPoissonBoltzmann::dotProduct( const LevelData<FArrayBox>& a_1,
                                const LevelData<FArrayBox>& a_2 )
{
   const DisjointBoxLayout& grids = a_1.disjointBoxLayout();

   double local_sum = 0.;
   for (DataIterator dit(grids.dataIterator()); dit.ok(); ++dit) {
      local_sum += a_1[dit].dotProduct(a_2[dit],grids[dit]);
   }

   double global_sum;
#ifdef CH_MPI
   MPI_Allreduce(&local_sum, &global_sum, 1, MPI_DOUBLE, MPI_SUM, MPI_COMM_WORLD);
#else
   global_sum = local_sum;
#endif

   return global_sum;
}



void
GKPoissonBoltzmann::incr( LevelData<FArrayBox>&       a_lhs,
                          const LevelData<FArrayBox>& a_x,
                          Real                        a_scale )
{
   for (DataIterator dit(a_lhs.dataIterator()); dit.ok(); ++dit) {
      a_lhs[dit].plus(a_x[dit], a_scale);
   }
}



void
GKPoissonBoltzmann::axby( LevelData<FArrayBox>&       a_lhs,
                          const LevelData<FArrayBox>& a_x,
                          const LevelData<FArrayBox>& a_y,
                          Real                        a_a,
                          Real                        a_b )
{
   for (DataIterator dit(a_lhs.dataIterator()); dit.ok(); ++dit) {
      FArrayBox& this_fab = a_lhs[dit];
      this_fab.copy(a_x[dit]);
      this_fab *= a_a;
      this_fab.plus(a_y[dit], a_b);
   }
}



void
GKPoissonBoltzmann::scale( LevelData<FArrayBox>& a_lhs,
                           const Real&           a_scale )
{
   for (DataIterator dit(a_lhs.dataIterator()); dit.ok(); ++dit) {
      a_lhs[dit] *= a_scale;
   }
}



Real
GKPoissonBoltzmann::norm( const LevelData<FArrayBox>& a_rhs,
                          int                         a_ord )
{
   return CH_XD::norm(a_rhs, a_rhs.interval(), a_ord);
}



void
GKPoissonBoltzmann::setToZero( LevelData<FArrayBox>& a_lhs )
{
   setZero(a_lhs);
}


bool
GKPoissonBoltzmann::isCoreRadialPeriodicOrNeumannBC( const PotentialBC& a_bc ) const
{
   bool flag = false;

   if ( typeid(a_bc) == typeid(AnnulusPotentialBC) ) {
      const ProblemDomain& domain = m_geometry.getBlockCoordSys(0).domain();

      if ( domain.isPeriodic(RADIAL_DIR) ||
           (a_bc.getBCType(0, RADIAL_DIR, 0) == PotentialBC::NEUMANN &&
            a_bc.getBCType(0, RADIAL_DIR, 1) == PotentialBC::NEUMANN) ) {
         flag = true;
      }
   }

   if ( typeid(a_bc) == typeid(SlabPotentialBC) ) {
      const ProblemDomain& domain = m_geometry.getBlockCoordSys(0).domain();

      if ( domain.isPeriodic(RADIAL_DIR) ||
           (a_bc.getBCType(0, RADIAL_DIR, 0) == PotentialBC::NEUMANN &&
            a_bc.getBCType(0, RADIAL_DIR, 1) == PotentialBC::NEUMANN) ) {
         flag = true;
      }
   }

   if ( typeid(a_bc) == typeid(SNCorePotentialBC) ) {
      if ( a_bc.getBCType(0, RADIAL_DIR, 0) == PotentialBC::NEUMANN &&
           a_bc.getBCType(0, RADIAL_DIR, 1) == PotentialBC::NEUMANN ) {
         flag = true;
      }
   }

   return flag;
}


void
GKPoissonBoltzmann::computeBoundaryData( FArrayBox& a_inner_divertor_bvs,
                                         FArrayBox& a_outer_divertor_bvs,
                                         const BoltzmannElectron&      a_ne,
                                         const LevelData<FArrayBox>& a_Zni,
                                         const LevelData<FArrayBox>& a_Jpar)
{

  double norm_dir_loc = 0;
  double norm_dir_glob = 0;

  int nrad = m_geometry.getBlockCoordSys(RSOL).domain().domainBox().size(RADIAL_DIR)
             + m_geometry.getBlockCoordSys(RPF).domain().domainBox().size(RADIAL_DIR);  

  //Initialize 1D arrays for boundary data
  m_Zni_outer_plate = new double[nrad];
  m_Zni_inner_plate = new double[nrad];
  m_phi_outer_plate = new double[nrad];
  m_phi_inner_plate = new double[nrad];

  //Creating temporaries
  double *zni_outer_plate_loc = new double[nrad];
  double *zni_inner_plate_loc = new double[nrad];

  double *jpar_outer_plate_loc = new double[nrad];
  double *jpar_inner_plate_loc = new double[nrad];
  double *jpar_outer_plate_glob = new double[nrad];
  double *jpar_inner_plate_glob = new double[nrad];

  double *bmag_outer_plate_loc = new double[nrad];
  double *bmag_inner_plate_loc = new double[nrad];
  double *bmag_outer_plate_glob = new double[nrad];
  double *bmag_inner_plate_glob = new double[nrad];

  //Initilizing everything with zeros
  for (int i=0; i<nrad; ++i) {
    m_Zni_outer_plate[i] = 0.0;
    zni_outer_plate_loc[i] = 0.0;

    m_Zni_inner_plate[i] = 0.0;
    zni_inner_plate_loc[i] = 0.0;

    m_phi_inner_plate[i] = 0.0;
    m_phi_outer_plate[i] = 0.0;

    jpar_outer_plate_glob[i] = 0.0;
    jpar_outer_plate_loc[i] = 0.0;
    jpar_inner_plate_glob[i] = 0.0;
    jpar_inner_plate_loc[i] = 0.0;

    bmag_outer_plate_glob[i] = 0.0;
    bmag_outer_plate_loc[i] = 0.0;
    bmag_inner_plate_glob[i] = 0.0;
    bmag_inner_plate_loc[i] = 0.0;

  }


  const ProblemDomain& domain_RSOL = m_geometry.getBlockCoordSys(RSOL).domain();
  const Box& domain_RSOL_box = domain_RSOL.domainBox();

  const ProblemDomain& domain_LSOL = m_geometry.getBlockCoordSys(LSOL).domain();
  const Box& domain_LSOL_box = domain_LSOL.domainBox();

  const MagCoordSys& coord_sys( *(m_geometry.getCoordSys()) );
  const LevelData<FArrayBox>& BFieldMag = m_geometry.getCCBFieldMag();
  const LevelData<FArrayBox>& BField = m_geometry.getCCBField();


  const DisjointBoxLayout& grids = m_geometry.grids();
  DataIterator dit = grids.dataIterator();
  
  for (dit.begin(); dit.ok(); ++dit) {

    int block_number = coord_sys.whichBlock( grids[dit] );

    //Obtaining boundary data at the outer plate
    if ( grids[dit].smallEnd(POLOIDAL_DIR) == domain_RSOL_box.smallEnd(POLOIDAL_DIR) ) {

       int lo_bnd = grids[dit].smallEnd(RADIAL_DIR);   
       int hi_bnd = grids[dit].bigEnd(RADIAL_DIR);   

       IntVect iv;
       int ipol = grids[dit].smallEnd(POLOIDAL_DIR);
       iv[POLOIDAL_DIR] = ipol;

       for ( int rad_index = lo_bnd; rad_index <= hi_bnd; rad_index++) {

         iv[RADIAL_DIR] = rad_index;
         int irad_conseq = getConsequtiveRadialIndex(rad_index, block_number);

         const FArrayBox& this_Zni( a_Zni[dit] );
         zni_outer_plate_loc[irad_conseq]=this_Zni(iv,0);

         const FArrayBox& this_Jpar( a_Jpar[dit] );
         jpar_outer_plate_loc[irad_conseq]= this_Jpar(iv,0);

         const FArrayBox& this_Bmag( BFieldMag[dit] );
         bmag_outer_plate_loc[irad_conseq]= this_Bmag(iv,0);

         const FArrayBox& this_B( BField[dit] );
         norm_dir_loc = this_B(iv,2);
         
       }
    }

    //Obtaining boundary data at the inner plate
    if ( grids[dit].bigEnd(POLOIDAL_DIR) == domain_LSOL_box.bigEnd(POLOIDAL_DIR) ) {

       int lo_bnd = grids[dit].smallEnd(RADIAL_DIR);   
       int hi_bnd = grids[dit].bigEnd(RADIAL_DIR);   

       IntVect iv;
       int ipol = grids[dit].bigEnd(POLOIDAL_DIR);
       iv[POLOIDAL_DIR] = ipol;

       for ( int rad_index = lo_bnd; rad_index <= hi_bnd; rad_index++) {

         iv[RADIAL_DIR] = rad_index;
         int irad_conseq = getConsequtiveRadialIndex(rad_index, block_number);

         const FArrayBox& this_Zni( a_Zni[dit] );
         zni_inner_plate_loc[irad_conseq]=this_Zni(iv,0);

         const FArrayBox& this_Jpar( a_Jpar[dit] );
         jpar_inner_plate_loc[irad_conseq]= this_Jpar(iv,0);

         const FArrayBox& this_Bmag( BFieldMag[dit] );
         bmag_inner_plate_loc[irad_conseq]= this_Bmag(iv,0);

       }
    }
  }


#ifdef CH_MPI

 MPI_Allreduce(zni_outer_plate_loc, m_Zni_outer_plate, nrad, MPI_DOUBLE, MPI_SUM, MPI_COMM_WORLD);
 MPI_Allreduce(zni_inner_plate_loc, m_Zni_inner_plate, nrad, MPI_DOUBLE, MPI_SUM, MPI_COMM_WORLD);

 MPI_Allreduce(jpar_outer_plate_loc, jpar_outer_plate_glob, nrad, MPI_DOUBLE, MPI_SUM, MPI_COMM_WORLD);
 MPI_Allreduce(jpar_inner_plate_loc, jpar_inner_plate_glob, nrad, MPI_DOUBLE, MPI_SUM, MPI_COMM_WORLD);

 MPI_Allreduce(bmag_outer_plate_loc, bmag_outer_plate_glob, nrad, MPI_DOUBLE, MPI_SUM, MPI_COMM_WORLD);
 MPI_Allreduce(bmag_inner_plate_loc, bmag_inner_plate_glob, nrad, MPI_DOUBLE, MPI_SUM, MPI_COMM_WORLD);

 MPI_Allreduce(&norm_dir_loc, &norm_dir_glob, 1, MPI_DOUBLE, MPI_SUM, MPI_COMM_WORLD);

#else

 for (int i=0; i<nrad; ++i) {
   m_Zni_outer_plate[i] = zni_outer_plate_loc[i];
   m_Zni_inner_plate[i] = zni_inner_plate_loc[i];

   jpar_outer_plate_glob[i] = jpar_outer_plate_loc[i];
   jpar_inner_plate_glob[i] = jpar_inner_plate_loc[i];

   bmag_outer_plate_glob[i] = jpar_outer_plate_loc[i];
   bmag_inner_plate_glob[i] = jpar_inner_plate_loc[i];
 }
  
 norm_dir_glob = norm_dir_local;

#endif

  delete [] zni_outer_plate_loc;
  delete [] zni_inner_plate_loc;

  delete [] jpar_outer_plate_loc;
  delete [] jpar_inner_plate_loc;

  delete [] bmag_outer_plate_loc;
  delete [] bmag_inner_plate_loc;

  
  int norm_dir = (norm_dir_glob > 0) ? 1 : -1;
 
  computeSheathPotentialBC(a_ne, jpar_outer_plate_glob, jpar_inner_plate_glob,
                           bmag_outer_plate_glob, bmag_inner_plate_glob, norm_dir);


  delete [] jpar_outer_plate_glob;
  delete [] jpar_inner_plate_glob;

  delete [] bmag_outer_plate_glob;
  delete [] bmag_inner_plate_glob;

  if (procID()==0) cout << "before hi_end_lo_end " <<  endl;

  //Fill the BC arrays
  //We use the fact that the inner and outer blocks has the same radial index structure

  int hi_bnd_PF = m_geometry.getBlockCoordSys(RPF).domain().domainBox().bigEnd(RADIAL_DIR); 
  int lo_bnd_SOL = m_geometry.getBlockCoordSys(RSOL).domain().domainBox().smallEnd(RADIAL_DIR);

  int i = 0;
  for (BoxIterator bit( a_inner_divertor_bvs.box() ); bit.ok(); ++bit) {
       IntVect iv = bit();
       if ((iv[RADIAL_DIR]<=hi_bnd_PF)||(iv[RADIAL_DIR]>=lo_bnd_SOL))  {           
          a_inner_divertor_bvs(iv,0) = m_phi_inner_plate[i];
          i++;
       }
  }

  i = 0;
  for (BoxIterator bit( a_outer_divertor_bvs.box() ); bit.ok(); ++bit) {
       IntVect iv = bit();
       if ((iv[RADIAL_DIR]<=hi_bnd_PF)||(iv[RADIAL_DIR]>=lo_bnd_SOL))  {           
          a_outer_divertor_bvs(iv,0) = m_phi_outer_plate[i];
          i++;
       }
  }

}


void
GKPoissonBoltzmann::computeSheathPotentialBC( const BoltzmannElectron& a_ne,
                                              const double * a_jpar_outer_plate, 
                                              const double * a_jpar_inner_plate,
                                              const double * a_bmag_outer_plate, 
                                              const double * a_bmag_inner_plate,
                                              const int norm_dir )
{
  //Convention for norm_dir. norm_dir is equal to +1 (-1) 
  // if the z-component of Bpol at the outer plate is up (down) 

  double me = a_ne.mass();
  double echarge = a_ne.charge();

  //TEMPORARY USE CONSTANT TE (UNITY)
  double Te = 1.0;

  int nrad = m_geometry.getBlockCoordSys(RSOL).domain().domainBox().size(RADIAL_DIR)
             + m_geometry.getBlockCoordSys(RPF).domain().domainBox().size(RADIAL_DIR);  

  double pi = 3.14159265359;
  double v_thermal_e = sqrt(Te/(2.0 * pi * me));
  double e = -echarge;


  for ( int irad = 0; irad < nrad; irad++) {

   double parallel_ion_loss = (-norm_dir) * a_jpar_outer_plate[irad] / a_bmag_outer_plate[irad] 
                            + (norm_dir) * a_jpar_inner_plate[irad] / a_bmag_inner_plate[irad];
  
   double fac_outer = (1/a_bmag_outer_plate[irad] + 1/a_bmag_inner_plate[irad]);
   fac_outer *=  e * v_thermal_e * m_Zni_outer_plate[irad];
   m_phi_outer_plate[irad] = -(Te/e) * log(parallel_ion_loss / fac_outer);

   double fac_inner = (1/a_bmag_outer_plate[irad] + 1/a_bmag_inner_plate[irad]);
   fac_inner *=  e * v_thermal_e * m_Zni_inner_plate[irad];
   m_phi_inner_plate[irad] = -(Te/e) * log(parallel_ion_loss / fac_inner);

  }

}


int
GKPoissonBoltzmann::getConsequtiveRadialIndex( const int a_mapped_index,
                                               const int a_block_number ) const
{
   int irad;

   if (a_block_number == RPF || a_block_number == LPF) {
     int i_ref = m_geometry.getBlockCoordSys(a_block_number).domain().domainBox().smallEnd(RADIAL_DIR);
     irad = a_mapped_index - i_ref;
   }

   else {
    int i_ref = m_geometry.getBlockCoordSys(a_block_number).domain().domainBox().bigEnd(RADIAL_DIR);

    int nrad = m_geometry.getBlockCoordSys(RSOL).domain().domainBox().size(RADIAL_DIR)
               + m_geometry.getBlockCoordSys(RPF).domain().domainBox().size(RADIAL_DIR);  

    irad = (nrad - 1) - (i_ref - a_mapped_index) ;
   }

   return irad;
}


#include "NamespaceFooter.H"

