#ifdef CH_LANG_CC
/*
 *      _______              __
 *     / ___/ /  ___  __ _  / /  ___
 *    / /__/ _ \/ _ \/  V \/ _ \/ _ \
 *    \___/_//_/\___/_/_/_/_.__/\___/
 *    Please refer to Copyright.txt, in Chombo's root directory.
 */
#endif

#include "altFaceAverages.H"
#include "altFaceAveragesF_F.H"

#include "NamespaceHeader.H"

void
bwenoFaceAverages( LevelData<FluxBox>&           a_face_phi,
                   const LevelData<FArrayBox>&   a_cell_phi,
                   const LevelData<FluxBox>&     a_face_vel,
                   const MagGeom&                a_geom )
{
   CH_assert( a_cell_phi.ghostVect()>=IntVect::Unit*2 );

   // In order to do upwinding, need normal velocities in
   // computational space.  We just need a sign, so we don't need fourth
   // order for this.
   const DisjointBoxLayout& grids( a_face_phi.getBoxes() );
   LevelData<FluxBox> normal_vel( grids, 1, IntVect::Unit*2 );
   for (DataIterator dit( grids.dataIterator() ); dit.ok(); ++dit) {
      normal_vel[dit].setVal( 0.0 );
   }
   a_geom.computeMetricTermProductAverage( normal_vel, a_face_vel, false );

   for (DataIterator dit( grids.dataIterator() ); dit.ok(); ++dit) {
      const FArrayBox& this_cell_phi( a_cell_phi[dit] );
      const FluxBox& this_normal_vel( normal_vel[dit] );
      FluxBox& this_face_phi( a_face_phi[dit] );

      for (int dir(0); dir<SpaceDim; dir++) {

         // for 4th order, we need two extra faces in the mapped-grid,
         // transverse directions to handle 4th-order products
         Box face_box( grids[dit] );
         for (int tdir(0); tdir<SpaceDim; tdir++) {
            if (tdir!=dir) {
               const int TRANSVERSE_GROW(2);
               face_box.grow( tdir, TRANSVERSE_GROW );
            }
         }
         face_box.surroundingNodes( dir );
         face_box.grow( dir, 1 );

         // now compute limited face value
         FArrayBox& this_face_phi_dir( this_face_phi[dir] );
         const FArrayBox& this_normal_vel_dir( this_normal_vel[dir] );
         FORT_BWENOFACEVALUES( CHF_FRA( this_face_phi_dir ),
                               CHF_CONST_FRA( this_cell_phi ),
                               CHF_CONST_FRA1( this_normal_vel_dir, 0 ),
                               CHF_BOX( face_box ),
                               CHF_CONST_INT( dir ) );

      } // end loop over directions
   } // end loop over grids
}

void
uw5FaceAverages( LevelData<FluxBox>&           a_face_phi,
                 const LevelData<FArrayBox>&   a_cell_phi,
                 const LevelData<FluxBox>&     a_face_vel,
                 const MagGeom&                a_geom )
{
   CH_assert( a_cell_phi.ghostVect()>=IntVect::Unit*2 );

   // In order to do upwinding, need normal velocities in
   // computational space.  We just need a sign, so we don't need fourth
   // order for this.
   const DisjointBoxLayout& grids( a_face_phi.getBoxes() );
   LevelData<FluxBox> normal_vel( grids, 1, IntVect::Unit*2 );
   for (DataIterator dit( grids.dataIterator() ); dit.ok(); ++dit) {
      normal_vel[dit].setVal( 0.0 );
   }
   a_geom.computeMetricTermProductAverage( normal_vel, a_face_vel, false );

   for (DataIterator dit( grids.dataIterator() ); dit.ok(); ++dit) {
      const FArrayBox& this_cell_phi( a_cell_phi[dit] );
      const FluxBox& this_normal_vel( normal_vel[dit] );
      FluxBox& this_face_phi( a_face_phi[dit] );

      for (int dir(0); dir<SpaceDim; dir++) {

         // for 4th order, we need two extra faces in the mapped-grid,
         // transverse directions to handle 4th-order products
         Box face_box( grids[dit] );
         for (int tdir(0); tdir<SpaceDim; tdir++) {
            if (tdir!=dir) {
               const int TRANSVERSE_GROW(2);
               face_box.grow( tdir, TRANSVERSE_GROW );
            }
         }
         face_box.surroundingNodes( dir );
         face_box.grow( dir, 1 );

         // now compute limited face value
         FArrayBox& this_face_phi_dir( this_face_phi[dir] );
         const FArrayBox& this_normal_vel_dir( this_normal_vel[dir] );
         FORT_UW5FACEVALUES( CHF_FRA( this_face_phi_dir ),
                             CHF_CONST_FRA( this_cell_phi ),
                             CHF_CONST_FRA1( this_normal_vel_dir, 0 ),
                             CHF_BOX( face_box ),
                             CHF_CONST_INT( dir ) );

      } // end loop over directions
   } // end loop over grids
}

void
weno5FaceAverages( LevelData<FluxBox>&           a_face_phi,
                   const LevelData<FArrayBox>&   a_cell_phi,
                   const LevelData<FluxBox>&     a_face_vel,
                   const MagGeom&                a_geom )
{
   CH_assert( a_cell_phi.ghostVect()>=IntVect::Unit*2 );

   // In order to do upwinding, need normal velocities in
   // computational space.  We just need a sign, so we don't need fourth
   // order for this.
   const DisjointBoxLayout& grids( a_face_phi.getBoxes() );
   LevelData<FluxBox> normal_vel( grids, 1, IntVect::Unit*2 );
   for (DataIterator dit( grids.dataIterator() ); dit.ok(); ++dit) {
      normal_vel[dit].setVal( 0.0 );
   }
   a_geom.computeMetricTermProductAverage( normal_vel, a_face_vel, false );

   for (DataIterator dit( grids.dataIterator() ); dit.ok(); ++dit) {
      const FArrayBox& this_cell_phi( a_cell_phi[dit] );
      const FluxBox& this_normal_vel( normal_vel[dit] );
      FluxBox& this_face_phi( a_face_phi[dit] );

      for (int dir(0); dir<SpaceDim; dir++) {

         // for 4th order, we need two extra faces in the mapped-grid,
         // transverse directions to handle 4th-order products
         Box face_box( grids[dit] );
         for (int tdir(0); tdir<SpaceDim; tdir++) {
            if (tdir!=dir) {
               const int TRANSVERSE_GROW(2);
               face_box.grow( tdir, TRANSVERSE_GROW );
            }
         }
         face_box.surroundingNodes( dir );
         face_box.grow( dir, 1 );

         // now compute limited face value
         FArrayBox& this_face_phi_dir( this_face_phi[dir] );
         const FArrayBox& this_normal_vel_dir( this_normal_vel[dir] );
         FORT_WENO5FACEVALUES( CHF_FRA( this_face_phi_dir ),
                               CHF_CONST_FRA( this_cell_phi ),
                               CHF_CONST_FRA1( this_normal_vel_dir, 0 ),
                               CHF_BOX( face_box ),
                               CHF_CONST_INT( dir ) );

      } // end loop over directions
   } // end loop over grids
}

#include "NamespaceFooter.H"
