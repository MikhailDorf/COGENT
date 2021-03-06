#ifndef _POISSON_H_
#define _POISSON_H_

#include "FieldSolver.H"
#ifdef with_petsc
#include "MBPETScSolver.H"
#else
#include "MBHypreSolver.H"
#endif

#include "NamespaceHeader.H"

/// Poisson solver class.
/**
 * This operator class solves the mapped Poisson equation:
 *
 * \f[
 *  {\nabla} \cdot \left ( N^T N/J  {\nabla} \Phi \right ) = \rho,
 * \f]
*/
class Poisson
   : public FieldSolver
{
public:

   /// Constructor with initialization.
   /**
    *  @param[in] pp the input database.
    *  @param[in] geom the geometry
    */
   Poisson( const ParmParse& pp,
            const MagGeom&   geom );
      
   /// Destructor.
   /**
    */
   virtual ~Poisson();

   /// Construct the operator coefficients
   /**
    * Computes the operator coefficients.
    */
   void setOperatorCoefficients( const PotentialBC& bc );

   void computeCoefficients( LevelData<FluxBox>& coefficients );

   virtual void setPreconditionerConvergenceParams( const double tol,
                                                    const int    max_iter,
                                                    const double precond_tol,
                                                    const int    precond_max_iter );

   virtual void solvePreconditioner( const LevelData<FArrayBox>& in,
                                     LevelData<FArrayBox>&       out );

   // Multiply by the unmapped coefficients
   virtual void multiplyUnmappedCoefficients( LevelData<FluxBox>& a_data ) const {;}

   /// ParmParse object name
   /**
    * ParmParse object name.  Used to locate input file parameters of interest to
    * this class.
    */
   static const std::string pp_name;

protected:

#ifdef with_petsc
   MBPETScSolver* m_preconditioner;
#else
   MBHypreSolver* m_preconditioner;
#endif

   LevelData<FluxBox> m_mapped_coefficients;
};



#include "NamespaceFooter.H"

#endif
