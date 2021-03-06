#ifndef _ADVECT_H_
#define _ADVECT_H_

#include "ParmParse.H"
#include "LevelData.H"
#include "FluxBox.H"
#include "MultiBlockLevelGeom.H"

#include "NamespaceHeader.H"

/**
 * Generic advection class.
*/
class Advect
{
public:

   /// Constructor with initialization.
   /**
    *  @param[in] pp the input database.
    */
   Advect( ParmParse  pp);

   /// Destructor.
   /**
    */
   virtual ~Advect();

   /// Evaluates the RHS of the semi-discrete advection operator.
   /**
    * @param[out] rhs data holder for rhs (without ghost cells).
    * @param[in] soln current solution with which to evaluate RHS.
    * @param[in] time current time.
    */
   virtual void evalRHS( const MultiBlockLevelGeom& geom,
                         LevelData<FArrayBox>& rhs,
                         const LevelData<FArrayBox>& soln,
                         const Real            time );

   /// Compute a stable time step.
   /**
    * Computes and returns an estimate of the maximum stable time step.
    */
   Real computeDt(const MultiBlockLevelGeom& geom, LevelData<FArrayBox>& soln);

   Real computeMappedDtSpecies(const LevelData<FluxBox>&  faceVel,
                               const MultiBlockLevelGeom& geom,
                               Real                       cfl);

   void applyMappedLimiter( LevelData<FluxBox>&            facePhi,
                            const LevelData<FArrayBox>&    cellPhi,
                            const LevelData<FluxBox>&      faceVel,
                            const MultiBlockCoordSys&      coords);

   void updateVelocity(const MultiBlockLevelGeom& geom, LevelData<FluxBox>& velocity);

   void mappedGridDivergenceWithoutNT( const MultiBlockCoordSys* coord_sys,
                                       LevelData<FArrayBox>& a_divF,
                                       const LevelData<FluxBox>& a_F ) const;

   void plotData( const string               a_file_name,
                  const MultiBlockLevelGeom& a_geom,
                  const LevelData<FluxBox>&  a_data ) const;

   void plotData( const string               a_file_name,
                  const MultiBlockLevelGeom& a_geom,
                  const LevelData<FArrayBox>&  a_data ) const;

   static char* pp_name;

private:

   void computeFlux( const LevelData<FArrayBox>& dist_fn,
                     const LevelData<FluxBox>&   velocity,
                     LevelData<FluxBox>&         flux,
                     const MultiBlockLevelGeom&  geom);

   /// Initializes the soln
   /**
    * Working through the vector, initializes solution
    * the associated initial data.
    *
    * @param[out] soln solution to initialize.
    * @param[in] time time at which to initialize.
    */
   void initialize( LevelData<FArrayBox>& soln,
                    const Real      time );

   inline void computeFaceAverages(LevelData<FluxBox>& a_face_data,
                                   const LevelData<FArrayBox>& a_cell_data) const;

   bool m_limit_face_values;

   // where we save the advective dt computed during the RHS computation.
   Real m_saved_dt;

   RealVect m_velocity;

   bool m_verbose;
};

#include "NamespaceFooter.H"

#endif
